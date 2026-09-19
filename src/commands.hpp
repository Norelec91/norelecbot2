#ifndef NORELECBOT_COMMANDS_HPP
#define NORELECBOT_COMMANDS_HPP

#include "config.hpp"
#include "game.hpp"
#include "storage.hpp"

#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <string_view>

namespace norelecbot {

inline constexpr std::string_view conquister_place = "@TheConquister37";
inline constexpr std::string_view conquister_trigger = "We @TheConquister37";
/* "We @someone" sends the player to rob them instead. */
inline constexpr std::string_view raid_trigger = "We @";

/* Says something where only one player reads it: in private on Telegram, in query on IRC. */
using Whisper = std::function<void(std::string_view name, std::string_view text)>;

struct CommandContext {
    Storage &storage;
    const AppConfig &config;
    std::int64_t user_id = 0;
    std::string_view username;
    /* The front end decides both: the chat or channel where the game is played, and who owns the bot. */
    bool claims_allowed = true;
    bool owner = false;
    /* Empty when the front end has no way to reach one player alone. */
    Whisper whisper;
};

/* What the bot says when it opens one of the games, and when it settles it. */
[[nodiscard]] std::string game_opened_reply(const GameOpened &opened);
[[nodiscard]] std::string game_closed_reply(const GameClosed &closed);
[[nodiscard]] std::string game_ticked_reply(const GameTicked &ticked);

/* What the bot says when something happens to the world at large. */
[[nodiscard]] std::string happening_reply(const HappeningResult &what);

/* What the bot says when one of the mishaps happens to somebody. */
[[nodiscard]] std::string mishap_reply(const MishapResult &mishap);

/* What the bot says when somebody leaves on a raid. */
[[nodiscard]] std::string raid_started_reply(
    const std::string &raider,
    const std::string &target,
    std::int64_t seconds
);

/* What the bot says when a raid reaches its target or comes home. */
[[nodiscard]] std::string raid_event_reply(const RaidEvent &event, const zodiac::Overrides &signs);

/* Whether the text is a claim or a known command, without running it. */
[[nodiscard]] bool command_is_for_bot(std::string_view text);

/* The reply to send, or nothing when the message is not for the bot; internal failures get a generic error reply. */
[[nodiscard]] std::optional<std::string> command_dispatch(const CommandContext &context, std::string_view text);

}

#endif
