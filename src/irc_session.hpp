#ifndef NORELECBOT_IRC_SESSION_HPP
#define NORELECBOT_IRC_SESSION_HPP

#include "irc_protocol.hpp"

#include <cstdint>
#include <deque>
#include <functional>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

namespace norelecbot::irc {

struct SessionConfig {
    std::string nick;
    std::string user;
    std::string realname;
    std::string nickserv_password;
    std::string channel;
    std::string owner_nick;
    std::int64_t registration_seconds = 300;
    std::int64_t whois_seconds = 10;
};

/* Answers a player who is registered with NickServ, or nothing when there is nothing to say. */
using Responder = std::function<std::optional<std::string>(std::string_view nick, bool owner, std::string_view text)>;

/* The protocol side of the bot: it turns incoming messages into the lines to send back. */
class Session {
public:
    Session(SessionConfig config, Responder responder);

    [[nodiscard]] std::vector<std::string> connected();
    [[nodiscard]] std::vector<std::string> handle(const Message &message, std::int64_t now);
    /* Gives up on the WHOIS replies that never came and forgets the stale answers. */
    void tick(std::int64_t now);
    [[nodiscard]] bool joined() const { return joined_; }
    [[nodiscard]] const std::string &nick() const { return nick_; }

private:
    struct Registration {
        bool registered = false;
        std::int64_t expires = 0;
        std::int64_t told = 0;
    };
    struct Waiting {
        std::string nick;
        std::string text;
        std::int64_t deadline = 0;
    };

    void answer(std::string_view nick, std::string_view text, std::vector<std::string> &lines);
    void release(const std::string &lowered, bool registered, std::int64_t now, std::vector<std::string> &lines);
    void say(std::string_view text, std::vector<std::string> &lines) const;

    SessionConfig config_;
    Responder responder_;
    std::string nick_;
    bool joined_ = false;
    std::unordered_map<std::string, Registration> registrations_;
    /* Nicks with a WHOIS in flight, mapped to whether numeric 307 has arrived. */
    std::unordered_map<std::string, bool> asked_;
    std::deque<Waiting> waiting_;
};

}

#endif
