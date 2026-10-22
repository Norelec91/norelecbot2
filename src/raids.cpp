#include "raids.hpp"

#include "commands.hpp"
#include "game.hpp"
#include "irc.hpp"
#include "logging.hpp"
#include "sentinel.hpp"
#include "telegram.hpp"

#include <chrono>
#include <exception>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <map>
#include <optional>
#include <string>
#include <system_error>
#include <thread>
#include <vector>

namespace norelecbot {
namespace {

constexpr std::chrono::seconds tick{5};
/* How long the sentinel keeps a script out of the game. */
constexpr std::int64_t block_seconds = 24 * 60 * 60;

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

/* Once a day, at its hour, the sentinel tells every owner in private what it saw. */
void sentinel_round(Storage &storage, const AppConfig &config, std::int64_t now) {
    if (config.owner_ids.empty() || config.bot_token.empty() || local_hour(now) != config.sentinel_hour ||
        sentinel().reported_day() == local_day(now)) {
        return;
    }
    sentinel().set_reported_day(local_day(now));
    const std::map<std::string, std::string> names = player_names(storage);
    /* The scripts it is sure of are kept out of the game for a day, until an owner lets them back in. */
    for (const std::string &script : sentinel().scripts(now)) {
        sentinel().block(script, now + block_seconds);
        log_info("Sentinel blocked user={} until={}", script, now + block_seconds);
    }
    const std::string report = sentinel().report(now, [&names](const std::string &key) {
        const auto found = names.find(key);
        return found != names.end() ? found->second : key;
    });
    for (const std::int64_t owner : config.owner_ids) {
        telegram_say(config, owner, report);
    }
    sentinel().save(config.sentinel_path);
    log_info("Sentinel report sent to {} owners", config.owner_ids.size());
}

}

void raids_run(Storage &storage, const AppConfig &config, const std::atomic<bool> &stop) {
    log_info("Raid keeper started");
    const RaidRules rules{
        .loot_divisor = config.loot_divisor,
        .travel_divisor = config.travel_divisor,
        .signs = config.zodiac_signs,
        .furniture_limit = static_cast<std::size_t>(config.furniture_limit),
        .smeared_seconds = config.smeared_seconds,
        .rocket_percent = config.rocket_percent,
        .pleading_percent = config.pleading_percent,
        .bomb_dud_percent = config.bomb_dud_percent,
        .dog_percent = config.dog_percent,
        .mailbox_percent = config.mailbox_percent,
        .ninja_percent = config.ninja_percent,
        .alarm_percent = config.alarm_percent,
        .pirate_percent = config.pirate_percent,
        .pregnancy_seconds = config.pregnancy_seconds,
        .child_stage_seconds = config.child_stage_seconds,
        .hen_per_minute = config.hen_per_minute,
        .mating_percent = config.mating_percent,
        .dino_percent = config.dino_percent,
        .salt_percent = config.salt_percent,
        .frozen_seconds = config.frozen_seconds,
        .fire_percent = config.fire_percent,
    };
    /* The news waits for the second round: by then IRC has had a tick to connect. */
    int rounds = 0;
    std::int64_t kept = seconds_now();
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
                if (const std::optional<std::string> reply = raid_event_reply(event, config)) {
                    announce(config, *reply);
                }
            }
        } catch (const std::exception &error) {
            log_warning("A raid could not be settled: {}", error.what());
        }
        try {
            sentinel_round(storage, config, seconds_now());
            if (seconds_now() - kept >= 600) {
                kept = seconds_now();
                sentinel().save(config.sentinel_path);
            }
        } catch (const std::exception &error) {
            log_warning("The sentinel could not do its round: {}", error.what());
        }
        for (int waited = 0; waited < tick.count() && !stop.load(std::memory_order_relaxed); ++waited) {
            std::this_thread::sleep_for(std::chrono::seconds{1});
        }
    }
    sentinel().save(config.sentinel_path);
    log_info("Raid keeper stopped");
}

}
