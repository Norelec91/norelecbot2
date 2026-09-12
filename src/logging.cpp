#include "logging.hpp"

#include <spdlog/pattern_formatter.h>
#include <spdlog/sinks/stdout_sinks.h>
#include <spdlog/spdlog.h>

#include <cstdio>
#include <cstdlib>
#include <memory>
#include <string>
#include <utility>

namespace norelecbot {
namespace {

spdlog::level::level_enum spdlog_level(LogLevel level) {
    switch (level) {
    case LogLevel::warning:
        return spdlog::level::warn;
    case LogLevel::error:
        return spdlog::level::err;
    case LogLevel::info:
        break;
    }
    return spdlog::level::info;
}

/* journald reads this digit and drops it from the message, so journalctl -p err works. */
class PriorityFlag : public spdlog::custom_flag_formatter {
public:
    void format(const spdlog::details::log_msg &message, const std::tm &, spdlog::memory_buf_t &destination) override {
        const char priority = message.level == spdlog::level::err ? '3'
            : message.level == spdlog::level::warn                ? '4'
                                                                  : '6';
        destination.push_back(priority);
    }

    [[nodiscard]] std::unique_ptr<custom_flag_formatter> clone() const override {
        return std::make_unique<PriorityFlag>();
    }
};

/* spdlog is a compiled library here, so its lowercase level names cannot be redefined from our side;
   the bot has always printed them uppercase and padded to the same width. */
class LevelFlag : public spdlog::custom_flag_formatter {
public:
    void format(const spdlog::details::log_msg &message, const std::tm &, spdlog::memory_buf_t &destination) override {
        const std::string_view name = message.level == spdlog::level::err ? "ERROR  "
            : message.level == spdlog::level::warn                        ? "WARNING"
                                                                          : "INFO   ";
        destination.append(name.data(), name.data() + name.size());
    }

    [[nodiscard]] std::unique_ptr<custom_flag_formatter> clone() const override {
        return std::make_unique<LevelFlag>();
    }
};

spdlog::logger &logger() {
    static const std::shared_ptr<spdlog::logger> instance = [] {
        // systemd sets JOURNAL_STREAM when it collects our stderr; elsewhere the prefix would just be noise.
        const bool journald = std::getenv("JOURNAL_STREAM") != nullptr;
        auto formatter = std::make_unique<spdlog::pattern_formatter>();
        formatter->add_flag<PriorityFlag>('*').add_flag<LevelFlag>('$').set_pattern(
            journald ? "<%*>%Y-%m-%d %H:%M:%S %$ %v" : "%Y-%m-%d %H:%M:%S %$ %v"
        );
        auto created = std::make_shared<spdlog::logger>("norelecbot", std::make_shared<spdlog::sinks::stderr_sink_mt>());
        created->set_formatter(std::move(formatter));
        created->set_level(spdlog::level::info);
        created->flush_on(spdlog::level::info);
        return created;
    }();
    return *instance;
}

}

void log_line(LogLevel level, std::string_view format, std::format_args arguments) noexcept {
    try {
        logger().log(spdlog_level(level), std::vformat(format, arguments));
    } catch (...) {
        std::fputs("Could not write a log line\n", stderr);
    }
}

}
