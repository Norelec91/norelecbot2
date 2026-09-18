#include "virus.hpp"

#include "logging.hpp"
#include "text.hpp"

#include <algorithm>
#include <iterator>
#include <utility>

namespace norelecbot {
namespace {

VirusPlayers::iterator find_player(VirusPlayers &players, std::string_view name) {
    return std::ranges::find_if(players, [name](const VirusPlayers::value_type &entry) {
        return text::equals_ignore_case(entry.first, name);
    });
}

std::size_t count_alive(const VirusPlayers &players, bool doronzo) {
    return static_cast<std::size_t>(std::ranges::count_if(players, [doronzo](const auto &entry) {
        return entry.second.alive && entry.second.doronzo == doronzo;
    }));
}

/* Half of what he had, and a debt stays a debt. */
std::int64_t take_half(ConquisterState &state, const std::string &name) {
    const auto score = std::ranges::find_if(state.scores, [&name](const Counters::value_type &entry) {
        return entry.first == name;
    });
    if (score == state.scores.end() || score->second <= 0) {
        return 0;
    }
    const std::int64_t lost = score->second / 2;
    score->second -= lost;
    return lost;
}

void settle(Virus &virus, VirusOutcome &outcome) {
    outcome.doronzi_left = count_alive(virus.players, true);
    outcome.healthy_left = count_alive(virus.players, false);
    if (outcome.doronzi_left != 0 && outcome.healthy_left != 0) {
        return;
    }
    outcome.over = true;
    outcome.doronzi_won = outcome.healthy_left == 0;
    virus.running = false;
}

}

VirusStart virus_start(Storage &storage, std::int64_t now) {
    const VirusStart result = storage.transaction([now](StorageSession &session) {
        ConquisterState &state = session.state();
        VirusStart start;
        if (state.virus.running) {
            start.status = VirusStatus::already_running;
            return start;
        }
        std::vector<std::string> names;
        std::ranges::transform(state.scores, std::back_inserter(names), [](const auto &entry) {
            return entry.first;
        });
        if (names.size() < 2) {
            start.status = VirusStatus::too_few_players;
            return start;
        }
        const std::size_t doronzi = std::max<std::size_t>(1, names.size() / virus_healthy_per_doronzo);
        /* Drawn one by one, so that nobody can work out the roles from the order of the file. */
        std::vector<std::string> chosen;
        while (chosen.size() < doronzi) {
            const std::size_t which = session.random_index(names.size());
            chosen.push_back(names[which]);
            names.erase(names.begin() + static_cast<std::ptrdiff_t>(which));
        }

        state.virus = Virus{.running = true, .infections = 0, .players = {}};
        for (const Counters::value_type &entry : state.scores) {
            const bool doronzo = std::ranges::find(chosen, entry.first) != chosen.end();
            state.virus.players.emplace(
                entry.first,
                VirusPlayer{.doronzo = doronzo, .alive = true, .vaccines = 0, .acted = now}
            );
            start.roles.emplace_back(entry.first, doronzo);
        }
        start.players = state.virus.players.size();
        start.doronzi = doronzi;
        return start;
    });

    if (result.status == VirusStatus::done) {
        log_info("virus started players={} doronzi={}", result.players, result.doronzi);
    }
    return result;
}

VirusOutcome virus_move(
    Storage &storage,
    const std::string &username,
    std::string_view target,
    VirusAction action,
    std::int64_t now,
    int cooldown_seconds
) {
    const VirusOutcome result = storage.transaction([&](StorageSession &session) {
        ConquisterState &state = session.state();
        Virus &virus = state.virus;
        VirusOutcome outcome;
        if (!virus.running) {
            outcome.status = VirusStatus::not_running;
            return outcome;
        }
        const auto mover = find_player(virus.players, username);
        if (mover == virus.players.end()) {
            outcome.status = VirusStatus::not_playing;
            return outcome;
        }
        if (!mover->second.alive) {
            outcome.status = VirusStatus::dead;
            return outcome;
        }
        if (const std::int64_t ready = mover->second.acted + cooldown_seconds; ready > now) {
            outcome.status = VirusStatus::too_soon;
            outcome.wait_seconds = ready - now;
            return outcome;
        }
        if (action == VirusAction::infect && !mover->second.doronzo) {
            outcome.status = VirusStatus::wrong_role;
            return outcome;
        }
        if (action != VirusAction::infect && mover->second.doronzo) {
            outcome.status = VirusStatus::wrong_role;
            return outcome;
        }
        if (action == VirusAction::cure && mover->second.vaccines <= 0) {
            outcome.status = VirusStatus::no_vaccine;
            return outcome;
        }
        const auto victim = find_player(virus.players, target);
        if (victim == virus.players.end()) {
            outcome.status = VirusStatus::unknown_target;
            return outcome;
        }
        if (text::equals_ignore_case(victim->first, username)) {
            outcome.status = VirusStatus::oneself;
            return outcome;
        }
        if (!victim->second.alive) {
            outcome.status = VirusStatus::target_dead;
            outcome.target = victim->first;
            return outcome;
        }

        outcome.target = victim->first;
        outcome.target_was_doronzo = victim->second.doronzo;
        mover->second.acted = now;
        switch (action) {
        case VirusAction::infect:
            victim->second.doronzo = true;
            if (!outcome.target_was_doronzo) {
                virus.infections += 1;
                if (virus.infections % virus_infections_per_vaccine == 0) {
                    std::vector<std::string> healthy;
                    for (const auto &[name, player] : virus.players) {
                        if (player.alive && !player.doronzo) {
                            healthy.push_back(name);
                        }
                    }
                    if (!healthy.empty()) {
                        outcome.vaccinated = healthy[session.random_index(healthy.size())];
                        find_player(virus.players, outcome.vaccinated)->second.vaccines += 1;
                    }
                }
            }
            break;
        case VirusAction::shoot:
            victim->second.alive = false;
            outcome.target_died = true;
            outcome.palle_lost = take_half(state, victim->first);
            break;
        case VirusAction::cure:
            mover->second.vaccines -= 1;
            victim->second.doronzo = false;
            break;
        }
        settle(virus, outcome);
        return outcome;
    });

    if (result.status == VirusStatus::done) {
        log_info(
            "virus move user={} action={} target={} was_doronzo={} died={} over={}",
            username,
            static_cast<int>(action),
            result.target,
            result.target_was_doronzo ? 1 : 0,
            result.target_died ? 1 : 0,
            result.over ? 1 : 0
        );
    }
    return result;
}

VirusReport virus_report(Storage &storage) {
    return storage.transaction([](StorageSession &session) {
        const Virus &virus = session.state().virus;
        VirusReport report;
        report.running = virus.running;
        report.infections = virus.infections;
        for (const auto &[name, player] : virus.players) {
            if (player.alive) {
                report.alive += 1;
            } else {
                report.dead += 1;
                report.fallen.push_back(name);
            }
        }
        return report;
    });
}

std::optional<VirusPlayer> virus_role(Storage &storage, const std::string &username) {
    return storage.transaction([&username](StorageSession &session) -> std::optional<VirusPlayer> {
        Virus &virus = session.state().virus;
        const auto found = find_player(virus.players, username);
        if (found == virus.players.end()) {
            return std::nullopt;
        }
        return found->second;
    });
}

}
