#ifndef NORELECBOT_LOGGING_HPP
#define NORELECBOT_LOGGING_HPP

#include <format>
#include <string_view>

namespace norelecbot {

enum class LogLevel { info, warning, error };

/* One line on stderr, prefixed with the syslog priority so journalctl -p can filter it. */
void log_line(LogLevel level, std::string_view format, std::format_args arguments) noexcept;

template <typename... Args>
void log_info(std::format_string<Args...> format, Args &&...arguments) noexcept {
    log_line(LogLevel::info, format.get(), std::make_format_args(arguments...));
}

template <typename... Args>
void log_warning(std::format_string<Args...> format, Args &&...arguments) noexcept {
    log_line(LogLevel::warning, format.get(), std::make_format_args(arguments...));
}

template <typename... Args>
void log_error(std::format_string<Args...> format, Args &&...arguments) noexcept {
    log_line(LogLevel::error, format.get(), std::make_format_args(arguments...));
}

}

#endif
