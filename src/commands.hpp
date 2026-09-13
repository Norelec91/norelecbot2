#ifndef NORELECBOT_COMMANDS_HPP
#define NORELECBOT_COMMANDS_HPP

#include "config.hpp"
#include "storage.hpp"

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

namespace norelecbot {

inline constexpr std::string_view conquister_place = "@TheConquister37";
inline constexpr std::string_view conquister_trigger = "We @TheConquister37";

struct CommandContext {
    Storage &storage;
    const AppConfig &config;
    std::int64_t user_id = 0;
    std::string_view username;
    /* The front end decides both: the chat or channel where the game is played, and who owns the bot. */
    bool claims_allowed = true;
    bool owner = false;
};

/* Whether the text is a claim or a known command, without running it. */
[[nodiscard]] bool command_is_for_bot(std::string_view text);

/* The reply to send, or nothing when the message is not for the bot; internal failures get a generic error reply. */
[[nodiscard]] std::optional<std::string> command_dispatch(const CommandContext &context, std::string_view text);

}

#endif
