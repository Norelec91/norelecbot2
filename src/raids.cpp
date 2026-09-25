#include "raids.hpp"

#include "commands.hpp"
#include "game.hpp"
#include "irc.hpp"
#include "logging.hpp"
#include "telegram.hpp"

#include <chrono>
#include <exception>
#include <optional>
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
        .loot_divisor = config.loot_divisor,
        .travel_divisor = config.travel_divisor,
        .signs = config.zodiac_signs,
        .furniture_limit = static_cast<std::size_t>(config.furniture_limit),
    };
    /* The closing of the bank is told once, on the second round: by then IRC has had a tick to connect. */
    int rounds = 0;
    while (!stop.load(std::memory_order_relaxed)) {
        if (++rounds == 2) {
            try {
                if (const std::vector<Refund> refunds = take_bank_refunds(storage); !refunds.empty()) {
                    announce(config, bank_closed_announcement(refunds));
                }
            } catch (const std::exception &error) {
                log_warning("The closing of the bank could not be told: {}", error.what());
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
