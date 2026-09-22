#include "irc_session.hpp"

#include "commands.hpp"
#include "logging.hpp"

#include <algorithm>
#include <format>
#include <iterator>
#include <utility>

namespace norelecbot::irc {
namespace {

constexpr std::string_view unregistered_reply =
    "{} devi essere registrato e identificato con NickServ per giocare.";
/* On IRC a leading slash belongs to the client, so the commands are typed with an exclamation mark. */
constexpr char irc_command_prefix = '!';

std::string with_slash(std::string_view text) {
    std::string command{text};
    if (command.starts_with(irc_command_prefix)) {
        command.front() = '/';
    }
    return command;
}

std::string line(std::string_view command, std::initializer_list<std::string_view> params) {
    return command_line(command, std::span<const std::string_view>{params.begin(), params.size()});
}

std::string line(std::string_view command, std::initializer_list<std::string_view> params, std::string_view text) {
    return command_line(command, std::span<const std::string_view>{params.begin(), params.size()}, text);
}

}

Session::Session(SessionConfig config, Responder responder)
    : config_{std::move(config)}, responder_{std::move(responder)}, nick_{config_.nick} {}

std::vector<std::string> Session::connected() {
    joined_ = false;
    nick_ = config_.nick;
    registrations_.clear();
    asked_.clear();
    waiting_.clear();
    return {line("NICK", {nick_}), line("USER", {config_.user, "0", "*"}, config_.realname)};
}

/* Marked for the bridge: whatever the bot says here, it says on Telegram itself, formatted for Telegram. */
void Session::say(std::string_view text, std::vector<std::string> &lines) const {
    const std::vector<std::string> parts = split_text(text, max_text_bytes - config_.no_forward_prefix.size());
    std::ranges::transform(parts, std::back_inserter(lines), [this](const std::string &part) {
        return line("PRIVMSG", {config_.channel}, config_.no_forward_prefix + part);
    });
}

void Session::answer(std::string_view nick, std::string_view account, std::string_view text,
                     std::vector<std::string> &lines) {
    const bool owner = !config_.owner_nick.empty() && same_name(nick, config_.owner_nick);
    if (const std::optional<std::string> reply = responder_(nick, account, owner, with_slash(text))) {
        say(*reply, lines);
    }
}

void Session::release(
    const std::string &lowered,
    bool registered,
    std::string_view account,
    std::int64_t now,
    std::vector<std::string> &lines
) {
    Registration &entry = registrations_[lowered];
    entry.registered = registered;
    entry.account = account.empty() ? lowered : std::string{account};
    entry.expires = now + config_.registration_seconds;
    for (auto waiting = waiting_.begin(); waiting != waiting_.end();) {
        if (to_lower(waiting->nick) != lowered) {
            ++waiting;
            continue;
        }
        if (registered) {
            answer(waiting->nick, entry.account, waiting->text, lines);
        } else if (entry.told + config_.registration_seconds <= now) {
            entry.told = now;
            say(std::vformat(unregistered_reply, std::make_format_args(waiting->nick)), lines);
        }
        waiting = waiting_.erase(waiting);
    }
}

std::vector<std::string> Session::handle(const Message &message, std::int64_t now) {
    std::vector<std::string> lines;
    const std::string_view command = message.command;
    const std::string_view sender = nick_of(message.prefix);

    if (command == "PING") {
        lines.push_back(line("PONG", {}, message.param(0)));
        return lines;
    }
    if (command == "ERROR") {
        log_warning("IRC server closing the link: {}", message.param(0));
        return lines;
    }
    if (command == "001") {
        nick_ = message.param(0);
        if (!config_.nickserv_password.empty()) {
            lines.push_back(line("PRIVMSG", {"NickServ"}, std::format("IDENTIFY {}", config_.nickserv_password)));
        }
        return lines;
    }
    if (command == "376" || command == "422") {
        lines.push_back(line("JOIN", {config_.channel}));
        return lines;
    }
    if (command == "433" || command == "437") {
        nick_ += "_";
        lines.push_back(line("NICK", {nick_}));
        return lines;
    }
    if (command == "JOIN" && same_name(sender, nick_)) {
        joined_ = true;
        return lines;
    }
    if (command == "KICK" && same_name(message.param(1), nick_)) {
        joined_ = false;
        lines.push_back(line("JOIN", {config_.channel}));
        return lines;
    }
    if (command == "307") {
        if (const auto asked = asked_.find(to_lower(message.param(1))); asked != asked_.end()) {
            asked->second.registered = true;
        }
        return lines;
    }
    if (command == "330") {
        if (const auto asked = asked_.find(to_lower(message.param(1))); asked != asked_.end() &&
            !message.param(2).empty()) {
            asked->second.registered = true;
            asked->second.account = message.param(2);
        }
        return lines;
    }
    if (command == "318") {
        const std::string lowered = to_lower(message.param(1));
        if (const auto asked = asked_.find(lowered); asked != asked_.end()) {
            const Identity identity = asked->second;
            asked_.erase(asked);
            release(lowered, identity.registered, identity.account, now, lines);
        }
        return lines;
    }
    /* The nick is gone: nothing to answer and nothing worth remembering. */
    if (command == "401") {
        const std::string lowered = to_lower(message.param(1));
        asked_.erase(lowered);
        std::erase_if(waiting_, [&lowered](const Waiting &waiting) { return to_lower(waiting.nick) == lowered; });
        return lines;
    }
    if (command == "NICK" || command == "QUIT" || command == "PART" || command == "KICK") {
        registrations_.erase(to_lower(sender));
        return lines;
    }
    if (command != "PRIVMSG" || !same_name(message.param(0), config_.channel)) {
        return lines;
    }
    if (sender.empty() || same_name(sender, nick_)) {
        return lines;
    }

    const std::string_view text = message.param(1);
    if (!command_is_for_bot(with_slash(text))) {
        return lines;
    }
    const std::string lowered = to_lower(sender);
    if (const auto known = registrations_.find(lowered);
        known != registrations_.end() && known->second.expires > now) {
        if (known->second.registered) {
            answer(sender, known->second.account, text, lines);
        } else if (known->second.told + config_.registration_seconds <= now) {
            known->second.told = now;
            say(std::vformat(unregistered_reply, std::make_format_args(sender)), lines);
        }
        return lines;
    }
    waiting_.push_back({std::string{sender}, std::string{text}, now + config_.whois_seconds});
    if (asked_.emplace(lowered, Identity{}).second) {
        lines.push_back(line("WHOIS", {sender}));
    }
    return lines;
}

std::vector<std::string> Session::announce(std::string_view text) const {
    std::vector<std::string> lines;
    say(text, lines);
    return lines;
}

void Session::tick(std::int64_t now) {
    for (auto waiting = waiting_.begin(); waiting != waiting_.end();) {
        if (waiting->deadline > now) {
            ++waiting;
            continue;
        }
        asked_.erase(to_lower(waiting->nick));
        waiting = waiting_.erase(waiting);
    }
    std::erase_if(registrations_, [now](const auto &entry) { return entry.second.expires <= now; });
}

}
