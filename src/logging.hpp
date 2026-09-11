#ifndef NORELECBOT_LOGGING_HPP
#define NORELECBOT_LOGGING_HPP

#include <format>
#include <string_view>

namespace norelecbot {

void log_line(std::string_view level, std::string_view format, std::format_args arguments) noexcept;

template <typename... Args>
void log_info(std::format_string<Args...> format, Args &&...arguments) noexcept {
    log_line("INFO", format.get(), std::make_format_args(arguments...));
}

template <typename... Args>
void log_warning(std::format_string<Args...> format, Args &&...arguments) noexcept {
    log_line("WARNING", format.get(), std::make_format_args(arguments...));
}

template <typename... Args>
void log_error(std::format_string<Args...> format, Args &&...arguments) noexcept {
    log_line("ERROR", format.get(), std::make_format_args(arguments...));
}

}

#endif
