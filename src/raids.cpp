#include "raids.hpp"

#include "commands.hpp"
#include "game.hpp"
#include "irc.hpp"
#include "logging.hpp"
#include "telegram.hpp"

#include <chrono>
#include <exception>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <optional>
#include <string>
#include <system_error>
#include <thread>
#include <vector>

namespace norelecbot {
namespace {

constexpr std::chrono::seconds tick{5};

std::int64_t seconds_now() {
    return std::chrono::duration_cast<std::chrono::seconds>(
               std::chrono::system_clock::now().time_since_epoch()
    ).count();
}

void announce(const AppConfig &config, const std::string &text) {
    if (config.conquister_chat_id != 0 && !config.bot_token.empty()) {
        telegram_say(config, config.conquister_chat_id, text);
    }
    if (config.irc_enabled) {
        irc_say(text);
    }
}

/* Tells both chats what changed in the game, once: the file is gone once it has been said. */
void announce_news(const AppConfig &config) {
    if (config.news_path.empty()) {
        return;
    }
    std::ifstream file{config.news_path, std::ios::binary};
    if (!file) {
        return;
    }
    std::string news{std::istreambuf_iterator<char>{file}, std::istreambuf_iterator<char>{}};
    file.close();
    while (!news.empty() && (news.back() == '\n' || news.back() == '\r' || news.back() == ' ')) {
        news.pop_back();
    }
    if (!news.empty()) {
        announce(config, news);
        log_info("News told: {} bytes", news.size());
    }
    std::error_code error;
    std::filesystem::remove(config.news_path, error);
    if (error) {
        log_warning("The news file {} could not be removed: {}", config.news_path, error.message());
    }
}

}

void raids_run(Storage &storage, const AppConfig &config, const std::atomic<bool> &stop) {
    log_info("Raid keeper started");
    const RaidRules rules{
        .loot_divisor = config.loot_divisor,
        .travel_divisor = config.travel_divisor,
        .signs = config.zodiac_signs,
        .furniture_limit = static_cast<std::size_t>(config.furniture_limit),
    };
    /* The news waits for the second round: by then IRC has had a tick to connect. */
    int rounds = 0;
    while (!stop.load(std::memory_order_relaxed)) {
        if (++rounds == 2) {
            try {
                announce_news(config);
            } catch (const std::exception &error) {
                log_warning("The news could not be told: {}", error.what());
            }
        }
        try {
            for (const RaidEvent &event : raid_due(storage, seconds_now(), rules)) {
                if (const std::optional<std::string> reply = raid_event_reply(event)) {
                    announce(config, *reply);
                }
            }
        } catch (const std::exception &error) {
            log_warning("A raid could not be settled: {}", error.what());
        }
        for (int waited = 0; waited < tick.count() && !stop.load(std::memory_order_relaxed); ++waited) {
            std::this_thread::sleep_for(std::chrono::seconds{1});
        }
    }
    log_info("Raid keeper stopped");
}

}
