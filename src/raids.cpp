#include "raids.hpp"

#include "commands.hpp"
#include "game.hpp"
#include "irc.hpp"
#include "logging.hpp"

#include <format>
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
    Clock reprogramming{config.reprogram_min_seconds, config.reprogram_max_seconds};
    const bool rules_shuffle = config.chaos_min_seconds > 0;
    Clock chaos{config.chaos_min_seconds, config.chaos_max_seconds};
    const bool games_run = config.game_min_seconds > 0;
    Clock games{config.game_min_seconds, config.game_max_seconds};
    const bool lottery_runs = config.lottery_min_seconds > 0;
    Clock lottery{config.lottery_min_seconds, config.lottery_max_seconds};
    const bool world_happens = config.happening_min_seconds > 0;
    Clock happenings{config.happening_min_seconds, config.happening_max_seconds};
    const bool flegyas_comes = config.flegyas_min_seconds > 0;
    Clock flegyas{config.flegyas_min_seconds, config.flegyas_max_seconds};
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
            if (const std::optional<TaxResult> taxed =
                    tax_the_leader(storage, config.tax_percent, seconds_now())) {
                announce(
                    config,
                    std::format(
                        "🧾 La Guardia di Finanza ha bussato a {}, primo in classifica: {} palle di tasse. "
                        "Gliene restano {}.",
                        taxed->player,
                        taxed->palle,
                        taxed->left
                    )
                );
            }
            if (ghost_plays && reprogramming.due(seconds_now())) {
                if (const std::optional<ReprogramResult> taken = reprogram(storage, seconds_now())) {
                    announce(
                        config,
                        std::format(
                            "🤖 {} ha riprogrammato {}: da adesso le sue azioni le decide lui, finché "
                            "qualcuno non lo libera con /libera {}.",
                            config.ghost_raider,
                            taken->player,
                            taken->player
                        )
                    );
                }
                reprogramming.rest();
            }
            if (rules_shuffle && chaos.due(seconds_now())) {
                /* Floors and ceilings wide enough to be unrecognisable, narrow enough to still be a game. */
                const Rules least{100, 100, 100, 2, 2, 100, 0, 0};
                const Rules most{5000, 5000, 5000, 10, 10, 2000, 1000, 1800};
                const Rules drawn = scramble_rules(storage, least, most);
                announce(
                    config,
                    std::format(
                        "🎲 LE REGOLE SONO CAMBIATE!\n"
                        "Citazione {} palle · palloncino {} · boost x{} a {} · le razzie prendono un "
                        "{}esimo · viaggi divisi per {} · attacco fallito {} palle · penalità {} secondi.",
                        drawn.quote_cost,
                        drawn.balloon_cost,
                        drawn.boost_multiplier,
                        drawn.boost_cost,
                        drawn.raid_share,
                        drawn.travel_divisor,
                        drawn.attack_cost,
                        drawn.cooldown_seconds
                    )
                );
                chaos.rest();
            }
            if (games_run) {
                if (const std::optional<GameTicked> ticked = game_tick(storage, seconds_now())) {
                    if (const std::string said = game_ticked_reply(*ticked); !said.empty()) {
                        announce(config, said);
                    }
                }
                if (const std::optional<GameClosed> closed = game_close(storage, seconds_now())) {
                    announce(config, game_closed_reply(*closed));
                }
                if (games.due(seconds_now())) {
                    if (const std::optional<GameOpened> opened = game_open(
                            storage,
                            seconds_now(),
                            config.game_open_seconds,
                            config.game_pot
                        )) {
                        announce(config, game_opened_reply(*opened));
                    }
                    games.rest();
                }
            }
            if (lottery_runs) {
                if (const std::optional<LotteryDraw> drawn = lottery_draw(storage, seconds_now())) {
                    announce(
                        config,
                        drawn->winner.empty()
                            ? std::string{"🎟️ Lotteria chiusa senza un biglietto venduto. Peggio per voi."}
                            : std::format(
                                  "🎉 LOTTERIA: ha vinto {} con {} biglietti su {}, e si porta a casa {} palle.",
                                  drawn->winner,
                                  drawn->tickets,
                                  drawn->players,
                                  drawn->pot
                              )
                    );
                }
                if (lottery.due(seconds_now()) &&
                    lottery_open(storage, seconds_now(), config.lottery_open_seconds)) {
                    announce(
                        config,
                        std::format(
                            "🎟️ È APERTA LA LOTTERIA! Chi scrive nel gruppo compra un biglietto da {} palle. "
                            "Si estrae fra {} minuti.",
                            config.lottery_ticket,
                            config.lottery_open_seconds / 60
                        )
                    );
                    lottery.rest();
                }
            }
            if (world_happens && happenings.due(seconds_now())) {
                if (const std::optional<HappeningResult> what = happening_strike(storage, seconds_now())) {
                    announce(config, happening_reply(*what));
                }
                happenings.rest();
            }
            if (flegyas_comes && flegyas.due(seconds_now())) {
                if (const std::optional<TaxResult> taken = flegyas_strike(storage, config.flegyas_share)) {
                    announce(
                        config,
                        std::format(
                            "😈 Flegiàs è sceso su {} e si è portato via {} palle. Gliene restano {}.",
                            taken->player,
                            taken->palle,
                            taken->left
                        )
                    );
                }
                flegyas.rest();
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
