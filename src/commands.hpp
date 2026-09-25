#ifndef NORELECBOT_COMMANDS_HPP
#define NORELECBOT_COMMANDS_HPP

#include "config.hpp"
#include "game.hpp"
#include "storage.hpp"

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

namespace norelecbot {

inline constexpr std::string_view conquister_place = "@TheConquister37";
inline constexpr std::string_view conquister_trigger = "We @TheConquister37";
/* "We @someone" names a Telegram player; "We someone" names an IRC player. */
inline constexpr std::string_view raid_trigger = "We ";

struct CommandContext {
    Storage &storage;
    const AppConfig &config;
    std::int64_t user_id = 0;
    std::string_view username;
    /* Bound by command_dispatch; never supplied by a client. */
    std::string_view player_key = {};
    std::string_view account_name = {};
    /* The front end decides both: the chat or channel where the game is played, and who owns the bot. */
    bool claims_allowed = true;
    bool owner = false;
    /* Trusted with the quotes; every owner is one, whatever the front end passes here. */
    bool admin = false;
};

/* What the bot says when a raid reaches its target or comes home; nothing for a homecoming empty-handed. */
[[nodiscard]] std::optional<std::string> raid_event_reply(const RaidEvent &event);

/* What the group is told once when the bank closes: who got back how much. */
[[nodiscard]] std::string bank_closed_announcement(const std::vector<Refund> &refunds);

/* Whether the text is a claim or a known command, without running it. */
[[nodiscard]] bool command_is_for_bot(std::string_view text);

/* The reply to send, or nothing when the message is not for the bot; internal failures get a generic error reply. */
[[nodiscard]] std::optional<std::string> command_dispatch(const CommandContext &context, std::string_view text);

}

#endif
