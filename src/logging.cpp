#include "logging.hpp"

#include <chrono>
#include <cstdio>
#include <print>
#include <stdexcept>
#include <string>

namespace norelecbot {
namespace {

std::string local_timestamp() {
    try {
        const std::chrono::zoned_time now{
            std::chrono::current_zone(),
            std::chrono::floor<std::chrono::seconds>(std::chrono::system_clock::now()),
        };
        return std::format("{:%Y-%m-%d %H:%M:%S}", now);
    } catch (const std::runtime_error &) {
        return "unknown-time";
    }
}

}

void log_line(std::string_view level, std::string_view format, std::format_args arguments) noexcept {
    try {
        std::print(stderr, "{} {:<7} {}\n", local_timestamp(), level, std::vformat(format, arguments));
    } catch (...) {
        std::fputs("Could not write a log line\n", stderr);
    }
}

}
