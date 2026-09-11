#ifndef NORELECBOT_TELEGRAM_COMMANDS_HPP
#define NORELECBOT_TELEGRAM_COMMANDS_HPP

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
    std::int64_t chat_id = 0;
    std::int64_t user_id = 0;
    std::string_view username;
};

/* The reply to send, or nothing when the message is not for the bot; internal failures get a generic error reply. */
[[nodiscard]] std::optional<std::string> telegram_command_dispatch(const CommandContext &context, std::string_view text);

}

#endif
