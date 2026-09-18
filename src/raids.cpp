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

/* Rings again after a wait drawn between the two bounds. */
class Clock {
public:
    Clock(int shortest, int longest)
        : shortest_{std::min(shortest, longest)},
          longest_{std::max(shortest, longest)},
          engine_{std::random_device{}()} {
        rest();
    }

    [[nodiscard]] bool due(std::int64_t now) const { return rings_ != 0 && now >= rings_; }

    void rest() {
        std::uniform_int_distribution<int> wait{shortest_, longest_};
        rings_ = seconds_now() + wait(engine_);
    }

private:
    int shortest_;
    int longest_;
    std::mt19937_64 engine_;
    std::int64_t rings_ = 0;
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
    const bool ghost_plays = !config.ghost_raider.empty();
    Clock ghost{config.ghost_min_seconds, config.ghost_max_seconds};
    const bool mishaps_happen = config.mishap_min_seconds > 0;
    Clock mishaps{config.mishap_min_seconds, config.mishap_max_seconds};
    while (!stop.load(std::memory_order_relaxed)) {
        try {
            for (const RaidEvent &event : raid_due(storage, seconds_now(), rules)) {
                announce(config, raid_event_reply(event, config.zodiac_signs));
            }
            if (ghost_plays && ghost.due(seconds_now())) {
                /* He writes what everybody writes, and is answered the same way. */
                const CommandContext context{
                    .storage = storage,
                    .config = config,
                    .user_id = 0,
                    .username = config.ghost_raider,
            .whisper = {},
        };
                if (const std::optional<std::string> said = command_dispatch(context, conquister_trigger)) {
                    announce(config, *said);
                }
                ghost.rest();
            }
            if (mishaps_happen && mishaps.due(seconds_now())) {
                if (const std::optional<MishapResult> mishap =
                        mishap_strike(storage, seconds_now(), config.boost_multiplier)) {
                    announce(config, mishap_reply(*mishap));
                }
                mishaps.rest();
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
