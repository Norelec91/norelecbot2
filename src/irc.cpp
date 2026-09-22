#include "irc.hpp"

#include "commands.hpp"
#include "game.hpp"
#include "irc_protocol.hpp"
#include "irc_session.hpp"
#include "logging.hpp"
#include "telegram.hpp"

#include <openssl/err.h>
#include <openssl/ssl.h>
#include <openssl/x509v3.h>

#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
#else
#include <arpa/inet.h>
#include <fcntl.h>
#include <netdb.h>
#include <poll.h>
#include <sys/socket.h>
#include <unistd.h>
#endif

#include <algorithm>
#include <array>
#include <chrono>
#include <cstring>
#include <deque>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <thread>
#include <utility>
#include <vector>

namespace norelecbot {
namespace {

constexpr int connect_timeout_milliseconds = 10000;
constexpr int handshake_timeout_seconds = 10;
/* Short, so that what Telegram left for the channel is picked up quickly. */
constexpr int read_timeout_microseconds = 200000;
constexpr int first_backoff_seconds = 5;
constexpr int max_backoff_seconds = 60;
constexpr std::size_t read_buffer_size = 4096;
/* Twice the 512 byte line limit: anything longer is not a line the server could have sent. */
constexpr std::size_t max_pending_bytes = 1024;
/* A disconnected channel must not pile up replies for ever. */
constexpr std::size_t max_waiting_replies = 32;

/* Answers to IRC commands, to be written in the Telegram group once they have left for the channel.
   Only the IRC thread touches this one. */
std::deque<std::string> &answers_for_telegram() {
    static std::deque<std::string> answers;
    return answers;
}

void send_answers_to_telegram(const AppConfig &config) {
    std::deque<std::string> &answers = answers_for_telegram();
    while (!answers.empty()) {
        telegram_say(config, config.conquister_chat_id, answers.front());
        answers.pop_front();
    }
}

/* What the bot answered on Telegram, waiting for the one IRC connection to repeat it. */
std::mutex waiting_mutex;

std::deque<std::string> &waiting_replies() {
    static std::deque<std::string> replies;
    return replies;
}

std::vector<std::string> take_waiting_replies() {
    const std::lock_guard guard{waiting_mutex};
    std::deque<std::string> &replies = waiting_replies();
    std::vector<std::string> taken{
        std::make_move_iterator(replies.begin()),
        std::make_move_iterator(replies.end())
    };
    replies.clear();
    return taken;
}

#ifdef _WIN32
using Handle = SOCKET;
constexpr Handle invalid_handle = INVALID_SOCKET;

void close_handle(Handle handle) {
    closesocket(handle);
}
#else
using Handle = int;
constexpr Handle invalid_handle = -1;

void close_handle(Handle handle) {
    ::close(handle);
}
#endif

class Socket {
public:
    Socket() = default;
    explicit Socket(Handle handle) : handle_{handle} {}
    Socket(const Socket &) = delete;
    Socket &operator=(const Socket &) = delete;
    Socket(Socket &&other) noexcept : handle_{std::exchange(other.handle_, invalid_handle)} {}
    Socket &operator=(Socket &&other) noexcept {
        std::swap(handle_, other.handle_);
        return *this;
    }
    ~Socket() {
        if (handle_ != invalid_handle) {
            close_handle(handle_);
        }
    }
    [[nodiscard]] Handle get() const { return handle_; }
    [[nodiscard]] bool valid() const { return handle_ != invalid_handle; }

private:
    Handle handle_ = invalid_handle;
};

struct ContextDeleter {
    void operator()(SSL_CTX *context) const { SSL_CTX_free(context); }
};

struct SslDeleter {
    void operator()(SSL *ssl) const { SSL_free(ssl); }
};

using ContextPointer = std::unique_ptr<SSL_CTX, ContextDeleter>;
using SslPointer = std::unique_ptr<SSL, SslDeleter>;

bool wait_writable(Handle handle, int milliseconds) {
#ifdef _WIN32
    WSAPOLLFD waiting{handle, POLLOUT, 0};
    return WSAPoll(&waiting, 1, milliseconds) > 0;
#else
    pollfd waiting{handle, POLLOUT, 0};
    return poll(&waiting, 1, milliseconds) > 0;
#endif
}

bool set_blocking(Handle handle, bool blocking) {
#ifdef _WIN32
    unsigned long mode = blocking ? 0UL : 1UL;
    return ioctlsocket(handle, FIONBIO, &mode) == 0;
#else
    const int flags = fcntl(handle, F_GETFL, 0);
    if (flags < 0) {
        return false;
    }
    const int wanted = blocking ? flags & ~O_NONBLOCK : flags | O_NONBLOCK;
    return fcntl(handle, F_SETFL, wanted) == 0;
#endif
}

void set_read_timeout(Handle handle, int seconds, int microseconds) {
    const timeval timeout{.tv_sec = seconds, .tv_usec = microseconds};
    setsockopt(handle, SOL_SOCKET, SO_RCVTIMEO, reinterpret_cast<const char *>(&timeout), sizeof(timeout));
}

/* Tries the addresses in the order getaddrinfo returns them, which /etc/gai.conf puts IPv4 first. */
Socket connect_to(const std::string &host, int port) {
    const addrinfo hints{
        .ai_flags = 0,
        .ai_family = AF_UNSPEC,
        .ai_socktype = SOCK_STREAM,
        .ai_protocol = 0,
        .ai_addrlen = 0,
        .ai_addr = nullptr,
        .ai_canonname = nullptr,
        .ai_next = nullptr,
    };
    addrinfo *found = nullptr;
    if (getaddrinfo(host.c_str(), std::to_string(port).c_str(), &hints, &found) != 0 || found == nullptr) {
        log_warning("IRC cannot resolve {}", host);
        return Socket{};
    }
    const std::unique_ptr<addrinfo, decltype(&freeaddrinfo)> addresses{found, &freeaddrinfo};
    for (const addrinfo *address = addresses.get(); address != nullptr; address = address->ai_next) {
        Socket socket{::socket(address->ai_family, address->ai_socktype, address->ai_protocol)};
        if (!socket.valid() || !set_blocking(socket.get(), false)) {
            continue;
        }
        const int started = ::connect(socket.get(), address->ai_addr, address->ai_addrlen);
        if (started != 0 && !wait_writable(socket.get(), connect_timeout_milliseconds)) {
            continue;
        }
        int error = 0;
        socklen_t length = sizeof(error);
        if (getsockopt(socket.get(), SOL_SOCKET, SO_ERROR, reinterpret_cast<char *>(&error), &length) != 0 ||
            error != 0 || !set_blocking(socket.get(), true)) {
            continue;
        }
        set_read_timeout(socket.get(), handshake_timeout_seconds, 0);
        return socket;
    }
    log_warning("IRC cannot connect to {}:{}", host, port);
    return Socket{};
}

ContextPointer make_context() {
    ContextPointer context{SSL_CTX_new(TLS_client_method())};
    if (!context) {
        return context;
    }
    SSL_CTX_set_min_proto_version(context.get(), TLS1_2_VERSION);
    SSL_CTX_set_verify(context.get(), SSL_VERIFY_PEER, nullptr);
    if (SSL_CTX_set_default_verify_paths(context.get()) != 1) {
        log_error("IRC cannot load the system certificates");
        context.reset();
    }
    return context;
}

SslPointer start_tls(SSL_CTX *context, const Socket &socket, const std::string &host) {
    SslPointer ssl{SSL_new(context)};
    if (!ssl) {
        return ssl;
    }
    SSL_set_fd(ssl.get(), static_cast<int>(socket.get()));
    SSL_set_hostflags(ssl.get(), X509_CHECK_FLAG_NO_PARTIAL_WILDCARDS);
    /* The SSL_set_tlsext_host_name macro casts the name to void *, so the call is spelled out here. */
    const long sni = SSL_ctrl(
        ssl.get(),
        SSL_CTRL_SET_TLSEXT_HOSTNAME,
        TLSEXT_NAMETYPE_host_name,
        const_cast<char *>(host.c_str())
    );
    if (SSL_set1_host(ssl.get(), host.c_str()) != 1 || sni != 1 || SSL_connect(ssl.get()) != 1) {
        log_warning("IRC TLS handshake with {} failed", host);
        ssl.reset();
    }
    return ssl;
}

bool send_line(SSL *ssl, const std::string &line) {
    std::size_t sent = 0;
    while (sent < line.size()) {
        const int written = SSL_write(ssl, line.data() + sent, static_cast<int>(line.size() - sent));
        if (written <= 0) {
            const int reason = SSL_get_error(ssl, written);
            if (reason == SSL_ERROR_WANT_READ || reason == SSL_ERROR_WANT_WRITE) {
                continue;
            }
            return false;
        }
        sent += static_cast<std::size_t>(written);
    }
    return true;
}

std::int64_t seconds_now() {
    return std::chrono::duration_cast<std::chrono::seconds>(
               std::chrono::system_clock::now().time_since_epoch()
    ).count();
}

irc::SessionConfig session_config(const AppConfig &config) {
    return {
        .nick = config.irc_nick,
        .user = config.irc_nick,
        .realname = config.irc_nick,
        .nickserv_password = config.irc_nickserv_password,
        .channel = config.irc_channel,
        .no_forward_prefix = config.irc_no_forward_prefix,
        .owner_nick = config.irc_owner_nick,
    };
}

class Connection {
public:
    Connection(SSL *ssl, irc::Session &session) : ssl_{ssl}, session_{session} {}

    void queue(const std::vector<std::string> &lines) {
        outbox_.insert(outbox_.end(), lines.begin(), lines.end());
    }

    /* Returns false when the connection is gone. */
    bool pump(std::int64_t now) {
        std::array<char, read_buffer_size> buffer{};
        const int received = SSL_read(ssl_, buffer.data(), static_cast<int>(buffer.size()));
        if (received > 0) {
            pending_.append(buffer.data(), static_cast<std::size_t>(received));
            consume(now);
        } else {
            const int reason = SSL_get_error(ssl_, received);
            if (reason != SSL_ERROR_WANT_READ && reason != SSL_ERROR_WANT_WRITE) {
                return false;
            }
        }
        session_.tick(now);
        if (session_.joined()) {
            for (const std::string &said : take_waiting_replies()) {
                queue(session_.announce(said));
            }
        }
        return flush();
    }

private:
    void consume(std::int64_t now) {
        while (true) {
            const std::size_t end = pending_.find('\n');
            if (end == std::string::npos) {
                if (pending_.size() > max_pending_bytes) {
                    pending_.clear();
                }
                return;
            }
            const std::string line = pending_.substr(0, end);
            pending_.erase(0, end + 1);
            if (const std::optional<irc::Message> message = irc::parse(line)) {
                queue(session_.handle(*message, now));
            }
        }
    }

    bool flush() {
        while (!outbox_.empty()) {
            const std::string line = std::move(outbox_.front());
            outbox_.pop_front();
            if (!send_line(ssl_, line)) {
                return false;
            }
        }
        return true;
    }

    SSL *ssl_;
    irc::Session &session_;
    std::string pending_;
    std::deque<std::string> outbox_;
};

}

void irc_say(std::string text) {
    const std::lock_guard guard{waiting_mutex};
    std::deque<std::string> &replies = waiting_replies();
    if (replies.size() >= max_waiting_replies) {
        replies.pop_front();
    }
    replies.push_back(std::move(text));
}

void irc_run(Storage &storage, const AppConfig &config, const std::atomic<bool> &stop) {
    const ContextPointer context = make_context();
    if (!context) {
        return;
    }
    irc::Session session{
        session_config(config),
        [&storage, &config](std::string_view nick, std::string_view account, bool owner,
                            std::string_view text) {
            const std::string username{nick};
            const CommandContext command{
                .storage = storage,
                .config = config,
                .user_id = 0,
                .username = username,
                .account_name = account,
                .claims_allowed = true,
                .owner = owner,
            };
            const std::optional<std::string> reply = command_dispatch(command, text);
            /* Answered in the group by the bot itself, so it reads there as Telegram writes: whole
               lines and working mentions, instead of the bridge's single relayed line. */
            if (reply && config.conquister_chat_id != 0 && !config.bot_token.empty()) {
                answers_for_telegram().push_back(*reply);
            }
            return reply;
        }
    };

    log_info("IRC client started (server={} channel={})", config.irc_server, config.irc_channel);
    int backoff = first_backoff_seconds;
    while (!stop.load(std::memory_order_relaxed)) {
        const Socket socket = connect_to(config.irc_server, config.irc_port);
        const SslPointer ssl = socket.valid() ? start_tls(context.get(), socket, config.irc_server) : SslPointer{};
        if (ssl) {
            /* The handshake is over: from here a read may return empty handed, often. */
            set_read_timeout(socket.get(), 0, read_timeout_microseconds);
            log_info("IRC connected to {}:{}", config.irc_server, config.irc_port);
            backoff = first_backoff_seconds;
            Connection connection{ssl.get(), session};
            connection.queue(session.connected());
            /* Ten minutes of old replies are of no use to anyone: start clean. */
            static_cast<void>(take_waiting_replies());
            answers_for_telegram().clear();
            /* pump writes the answer in the channel; the group gets it right after. */
            while (!stop.load(std::memory_order_relaxed) && connection.pump(seconds_now())) {
                send_answers_to_telegram(config);
            }
            if (stop.load(std::memory_order_relaxed)) {
                static_cast<void>(send_line(ssl.get(), "QUIT :ciao\r\n"));
            }
            SSL_shutdown(ssl.get());
            log_warning("IRC disconnected from {}", config.irc_server);
            continue;
        }
        for (int waited = 0; waited < backoff && !stop.load(std::memory_order_relaxed); ++waited) {
            std::this_thread::sleep_for(std::chrono::seconds{1});
        }
        backoff = std::min(backoff * 2, max_backoff_seconds);
    }
    log_info("IRC client stopped");
}

}
