#include "raids.hpp"

#include "commands.hpp"
#include "game.hpp"
#include "irc.hpp"
#include "logging.hpp"
#include "telegram.hpp"

#include <chrono>
#include <exception>
#include <string>
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

}

void raids_run(Storage &storage, const AppConfig &config, const std::atomic<bool> &stop) {
    log_info("Raid keeper started");
    const RaidRules rules{
        .travel_divisor = config.travel_divisor,
        .attack_cost = config.attack_cost,
        .signs = config.zodiac_signs,
    };
    while (!stop.load(std::memory_order_relaxed)) {
        try {
            for (const RaidEvent &event : raid_due(storage, seconds_now(), rules)) {
                announce(config, raid_event_reply(event, config.zodiac_signs));
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
