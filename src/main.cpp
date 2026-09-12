#include "config.hpp"
#include "http_server.hpp"
#include "logging.hpp"
#include "storage.hpp"
#include "telegram.hpp"

#include <atomic>
#include <chrono>
#include <csignal>
#include <cstdlib>
#include <exception>
#include <optional>
#include <thread>

namespace {

std::atomic<bool> stop_requested{false};
static_assert(std::atomic<bool>::is_always_lock_free, "a signal handler may only touch a lock-free flag");

extern "C" void request_stop(int) {
    stop_requested.store(true, std::memory_order_relaxed);
}

int run() {
    const std::optional<norelecbot::AppConfig> config = norelecbot::load_config(".env");
    if (!config) {
        return EXIT_FAILURE;
    }
    norelecbot::Storage storage{config->conquister_path, config->quotes_path};

    std::signal(SIGINT, request_stop);
    std::signal(SIGTERM, request_stop);
#ifdef SIGPIPE
    // A peer closing a TLS or HTTP connection must not terminate the process.
    std::signal(SIGPIPE, SIG_IGN);
#endif

    int result = EXIT_SUCCESS;
    {
        const norelecbot::HttpServer http{config->api_host, config->api_port, storage};
        if (!config->conquister_enabled) {
            norelecbot::log_info("Conquister disabled");
            while (!stop_requested.load(std::memory_order_relaxed)) {
                std::this_thread::sleep_for(std::chrono::seconds{1});
            }
        } else if (config->bot_token.empty()) {
            norelecbot::log_error(
                "NORELECBOT_CONQUISTER_ENABLED is set but NORELECBOT_TELEGRAM_TOKEN is missing"
            );
            result = EXIT_FAILURE;
        } else {
            norelecbot::telegram_run(storage, *config, stop_requested);
        }
    }
    norelecbot::log_info("NorelecBot stopped");
    return result;
}

}

int main() {
    try {
        return run();
    } catch (const norelecbot::StorageError &) {
        return EXIT_FAILURE;
    } catch (const std::exception &error) {
        norelecbot::log_error("{}", error.what());
        return EXIT_FAILURE;
    }
}
