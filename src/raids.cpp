#include "raids.hpp"

#include "commands.hpp"
#include "game.hpp"
#include "irc.hpp"
#include "logging.hpp"
#include "telegram.hpp"

#include <algorithm>
#include <chrono>
#include <exception>
#include <random>
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

/* A player of the bot's own, who leaves for the same house every so often. */
class Ghost {
public:
    explicit Ghost(const AppConfig &config)
        : shortest_{std::min(config.ghost_min_seconds, config.ghost_max_seconds)},
          longest_{std::max(config.ghost_min_seconds, config.ghost_max_seconds)},
          engine_{std::random_device{}()} {
        rest();
    }

    [[nodiscard]] bool due(std::int64_t now) const { return leaves_ != 0 && now >= leaves_; }

    void rest() {
        std::uniform_int_distribution<int> wait{shortest_, longest_};
        leaves_ = seconds_now() + wait(engine_);
    }

private:
    int shortest_;
    int longest_;
    std::mt19937_64 engine_;
    std::int64_t leaves_ = 0;
};

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
        .loot_share = config.raid_share,
        .attack_cost = config.attack_cost,
        .signs = config.zodiac_signs,
        .shadowed = config.shadowed,
    };
    const bool ghost_plays = !config.ghost_raider.empty() && !config.ghost_target.empty();
    Ghost ghost{config};
    while (!stop.load(std::memory_order_relaxed)) {
        try {
            for (const RaidEvent &event : raid_due(storage, seconds_now(), rules)) {
                announce(config, raid_event_reply(event, config.zodiac_signs));
            }
            if (ghost_plays && ghost.due(seconds_now())) {
                const RaidResult left =
                    raid_start(storage, 0, config.ghost_raider, config.ghost_target, seconds_now(), rules);
                if (left.status == RaidStatus::started) {
                    announce(config, raid_started_reply(config.ghost_raider, left.target, left.seconds));
                }
                ghost.rest();
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
