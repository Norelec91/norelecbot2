#ifndef NORELECBOT_TELEGRAM_COMMANDS_HPP
#define NORELECBOT_TELEGRAM_COMMANDS_HPP

#include "config.hpp"
#include "storage.hpp"

#include <cstdint>
#include <string>
#include <string_view>

namespace norelecbot {

inline constexpr std::string_view conquister_place = "@TheConquister37";
inline constexpr std::string_view conquister_trigger = "We @TheConquister37";

enum class CommandResult { ignored, replied, error };

struct CommandContext {
    Storage &storage;
    const AppConfig &config;
    std::int64_t chat_id = 0;
    std::int64_t user_id = 0;
    std::string_view username;
};

/* Ignored messages produce no reply; recognized messages always do. */
[[nodiscard]] CommandResult telegram_command_dispatch(
    const CommandContext &context,
    std::string_view text,
    std::string &reply
);

}

#endif
