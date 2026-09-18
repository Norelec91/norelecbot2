#include "game.hpp"

#include "logging.hpp"
#include "mishaps.hpp"
#include "position.hpp"
#include "text.hpp"
#include "zodiac.hpp"

#include <algorithm>
#include <iterator>
#include <cmath>
#include <limits>
#include <ranges>
#include <utility>

namespace norelecbot {
namespace {

constexpr std::size_t quotes_page_size = 30;

/* Higher score first, then username in byte order. */
constexpr auto ranks_before = [](const auto &first, const auto &second) {
    return first.second != second.second ? first.second > second.second : first.first < second.first;
};

std::int64_t counter(const Counters &counters, const std::string &username) {
    const auto found = counters.find(username);
    return found != counters.end() ? found->second : 0;
}

const Counters::value_type *find_ignore_case(const Counters &counters, std::string_view username) {
    const auto found = std::ranges::find_if(counters, [username](const Counters::value_type &entry) {
        return text::equals_ignore_case(entry.first, username);
    });
    return found != counters.end() ? &*found : nullptr;
}

/* Four attempts at most: the first has one chance in four, the fourth is certain. */
constexpr std::size_t balloon_attempts = 4;

Counters::iterator find_entry(Counters &counters, const std::string &username) {
    return std::ranges::find_if(counters, [&username](const Counters::value_type &entry) {
        return entry.first == username;
    });
}

}

namespace {

/* A balloon that holds costs the attacker, even when it puts him in the red. */
std::int64_t charge_attacker(ConquisterState &state, const std::string &username, int attack_cost) {
    if (attack_cost <= 0) {
        return 0;
    }
    state.scores[username] = counter(state.scores, username) - attack_cost;
    return attack_cost;
}

/* What a hold was worth: the seconds it lasted, times the boost he had bought, times the house of the
   day. Both the boost and the hold are spent by this. */
struct Settlement {
    std::int64_t earned = 0;
    std::int64_t boost_multiplier = 0;
    int zodiac_percent = 100;
};

Settlement settle_hold(
    ConquisterState &state,
    const std::string &holder,
    std::int64_t since,
    std::int64_t now,
    zodiac::Overrides signs
) {
    Settlement settled;
    settled.earned = now > since ? now - since : 0;
    if (const auto boost = find_entry(state.boosts, holder); boost != state.boosts.end()) {
        settled.boost_multiplier = boost->second;
        if (settled.earned > std::numeric_limits<std::int64_t>::max() / settled.boost_multiplier) {
            log_error("Could not multiply the Conquister score");
            throw StorageError("Conquister score overflow");
        }
        settled.earned *= settled.boost_multiplier;
        state.boosts.erase(holder);
    }
    settled.zodiac_percent = zodiac::percent_for(holder, now, signs);
    settled.earned = settled.earned / 100 * settled.zodiac_percent +
                     settled.earned % 100 * settled.zodiac_percent / 100;
    std::int64_t &score = state.scores[holder];
    if (score > 0 && settled.earned > std::numeric_limits<std::int64_t>::max() - score) {
        log_error("Could not update Conquister score");
        throw StorageError("Conquister score overflow");
    }
    score += settled.earned;
    return settled;
}

/* Everybody starts well liked; robbing people spends it, and a day of quiet gives a point back. */
constexpr std::int64_t simpatia_full = 20;
constexpr std::int64_t simpatia_day = 86400;

std::int64_t simpatia_now(ConquisterState &state, const std::string &username, std::int64_t now) {
    const auto found = find_entry(state.simpatia, username);
    if (found == state.simpatia.end()) {
        return simpatia_full;
    }
    const std::int64_t seen = counter(state.simpatia_seen, username);
    const std::int64_t days = seen > 0 && now > seen ? (now - seen) / simpatia_day : 0;
    const std::int64_t value = std::min(simpatia_full, found->second + days);
    if (days > 0) {
        found->second = value;
        state.simpatia_seen[username] = seen + (days * simpatia_day);
    }
    return value;
}

/* Returns what it is worth afterwards. */
std::int64_t simpatia_change(
    ConquisterState &state,
    const std::string &username,
    std::int64_t by,
    std::int64_t now
) {
    const std::int64_t value = std::min(simpatia_full, simpatia_now(state, username, now) + by);
    state.simpatia[username] = value;
    state.simpatia_seen[username] = now;
    return value;
}

/* Whoever is on the road has left his base, and everything in it, unguarded. */
bool is_away(const ConquisterState &state, const std::string &username) {
    return std::ranges::any_of(state.raids, [&username](const Raid &raid) {
        return raid.raider == username;
    });
}

Raid *raid_of(ConquisterState &state, const std::string &username) {
    const auto found = std::ranges::find_if(state.raids, [&username](const Raid &raid) {
        return raid.raider == username;
    });
    return found != state.raids.end() ? &*found : nullptr;
}

/* The id is drawn the first time the bot sees a player and never changes: it is where he lives. */
std::int64_t player_id(StorageSession &session, ConquisterState &state, const std::string &username) {
    if (const auto found = find_entry(state.ids, username); found != state.ids.end()) {
        return found->second;
    }
    std::int64_t drawn = 0;
    do {
        drawn = static_cast<std::int64_t>(session.random_index(static_cast<std::size_t>(position::ids)));
    } while (std::ranges::any_of(state.ids, [drawn](const Counters::value_type &entry) {
        return entry.second == drawn;
    }));
    state.ids[username] = drawn;
    return drawn;
}

/* Kept so that a message about this player can reach them where they play. */
void remember_telegram(ConquisterState &state, const std::string &username, std::int64_t user_id) {
    if (user_id != 0 && counter(state.telegram_ids, username) != user_id) {
        state.telegram_ids[username] = user_id;
    }
}

/* The name as it is written on file, whatever spelling the message used. */
std::optional<std::string> known_player(const ConquisterState &state, std::string_view name) {
    for (const Counters *counters : {&state.scores, &state.quotes_added, &state.ids}) {
        if (const Counters::value_type *found = find_ignore_case(*counters, name); found != nullptr) {
            return found->first;
        }
    }
    if (state.current && text::equals_ignore_case(state.current->username, name)) {
        return state.current->username;
    }
    return std::nullopt;
}

}

ClaimResult conquister_claim(
    Storage &storage,
    std::int64_t user_id,
    const std::string &username,
    std::int64_t now,
    const ClaimRules &rules
) {
    const ClaimResult result = storage.transaction([&](StorageSession &session) {
        ConquisterState &state = session.state();
        remember_telegram(state, username, user_id);
        ClaimResult outcome;
        /* A place is held by standing in it, not from the road. */
        if (const Raid *travelling = raid_of(state, username); travelling != nullptr) {
            outcome.status = ClaimStatus::travelling;
            outcome.travel_seconds = std::max<std::int64_t>(travelling->back - now, 0);
            return outcome;
        }
        if (const auto penalty = find_entry(state.cooldowns, username); penalty != state.cooldowns.end()) {
            if (penalty->second > now) {
                outcome.status = ClaimStatus::cooldown;
                outcome.penalty_seconds = penalty->second - now;
                return outcome;
            }
            state.cooldowns.erase(username);
        }
        if (state.current && state.current->username == username) {
            outcome.status = ClaimStatus::already_held;
            return outcome;
        }
        if (state.current && !state.current->username.empty()) {
            const std::string holder = state.current->username;
            outcome.previous_user_id = state.current->user_id;
            if (const auto shield = find_entry(state.shields, holder); shield != state.shields.end()) {
                if (shield->second > now && rules.ignores_shield) {
                    state.shields.erase(holder);
                    outcome.balloon_popped = true;
                } else if (shield->second > now) {
                    if (rules.cooldown_seconds > 0) {
                        state.cooldowns[username] = now + rules.cooldown_seconds;
                        outcome.penalty_seconds = rules.cooldown_seconds;
                    }
                    outcome.attack_cost = charge_attacker(state, username, rules.attack_cost);
                    outcome.status = ClaimStatus::defended;
                    outcome.previous_username = holder;
                    outcome.shield_seconds = shield->second - now;
                    return outcome;
                }
                state.shields.erase(holder);
            }
            if (const auto balloon = find_entry(state.balloons, holder); balloon != state.balloons.end()) {
                const std::int64_t attempt = balloon->second + 1;
                if (static_cast<std::int64_t>(session.random_index(balloon_attempts)) >= attempt) {
                    balloon->second = attempt;
                    if (rules.cooldown_seconds > 0) {
                        state.cooldowns[username] = now + rules.cooldown_seconds;
                        outcome.penalty_seconds = rules.cooldown_seconds;
                    }
                    outcome.attack_cost = charge_attacker(state, username, rules.attack_cost);
                    outcome.status = ClaimStatus::defended;
                    outcome.previous_username = holder;
                    outcome.next_chance =
                        static_cast<int>((attempt + 1) * 100 / static_cast<std::int64_t>(balloon_attempts));
                    return outcome;
                }
                state.balloons.erase(holder);
                outcome.balloon_popped = true;
            }
            outcome.previous_username = holder;
            const Settlement settled = settle_hold(state, holder, state.current->since, now, rules.signs);
            outcome.earned = settled.earned;
            outcome.boost_multiplier = settled.boost_multiplier;
            outcome.zodiac_percent = settled.zodiac_percent;
        }
        state.current = Holder{user_id, username, now};
        return outcome;
    });

    switch (result.status) {
    case ClaimStatus::taken:
        log_info(
            "claim taken user={} previous={} earned={} balloon_popped={} boost={}",
            username,
            result.previous_username,
            result.earned,
            result.balloon_popped ? 1 : 0,
            result.boost_multiplier
        );
        break;
    case ClaimStatus::defended:
        log_info(
            "claim defended user={} holder={} next_chance={} penalty={} shield={} cost={}",
            username,
            result.previous_username,
            result.next_chance,
            result.penalty_seconds,
            result.shield_seconds,
            result.attack_cost
        );
        break;
    case ClaimStatus::cooldown:
        log_info("claim blocked user={} wait={}", username, result.penalty_seconds);
        break;
    case ClaimStatus::travelling:
        log_info("claim refused user={} travelling={}", username, result.travel_seconds);
        break;
    case ClaimStatus::already_held:
        break;
    }
    return result;
}

DisputeResult dispute_open(Storage &storage, const std::string &username, std::int64_t now) {
    const DisputeResult result = storage.transaction([&](StorageSession &session) {
        ConquisterState &state = session.state();
        DisputeResult opened;
        /* Whoever is holding palle taken from him is the one he has a case against. */
        const auto took = std::ranges::find_if(state.loot_from, [&username](const auto &entry) {
            return text::equals_ignore_case(entry.second, username);
        });
        if (took == state.loot_from.end()) {
            opened.status = DisputeStatus::nothing_to_report;
            return opened;
        }
        opened.seller = took->first;
        opened.palle = counter(state.loot_amount, opened.seller);
        if (state.dispute_buyer.find(opened.seller) != state.dispute_buyer.end()) {
            opened.status = DisputeStatus::already_open;
            return opened;
        }
        if (counter(state.loot_when, opened.seller) + dispute_window_seconds < now) {
            opened.status = DisputeStatus::too_late;
            return opened;
        }
        state.dispute_buyer[opened.seller] = username;
        state.dispute_amount[opened.seller] = opened.palle;
        return opened;
    });

    if (result.status == DisputeStatus::done) {
        log_info("dispute opened by={} against={} palle={}", username, result.seller, result.palle);
    }
    return result;
}

ReturnResult loot_return(
    Storage &storage,
    const std::string &username,
    std::int64_t now,
    bool postage_on_seller
) {
    const ReturnResult result = storage.transaction([&](StorageSession &session) {
        ConquisterState &state = session.state();
        ReturnResult giving;
        const auto from = state.loot_from.find(username);
        const std::int64_t palle = counter(state.loot_amount, username);
        if (from == state.loot_from.end() || palle <= 0) {
            giving.status = ReturnStatus::nothing_to_return;
            return giving;
        }
        giving.victim = from->second;
        giving.palle = palle;
        giving.disputed = state.dispute_buyer.find(username) != state.dispute_buyer.end();
        if (counter(state.loot_when, username) + return_window_seconds < now) {
            giving.status = ReturnStatus::too_late;
            return giving;
        }
        /* One case in three the support refunds the buyer and lets the seller keep the palle. */
        giving.overturned =
            giving.disputed && session.random_index(support_overturns_one_in) == 0;
        if (giving.overturned) {
            state.scores[giving.victim] = counter(state.scores, giving.victim) + palle;
        } else {
            giving.postage = palle / return_postage_share;
            const std::int64_t from_seller = palle + (postage_on_seller ? giving.postage : 0);
            const std::int64_t to_buyer = palle - (postage_on_seller ? 0 : giving.postage);
            state.scores[username] = counter(state.scores, username) - from_seller;
            state.scores[giving.victim] = counter(state.scores, giving.victim) + to_buyer;
        }
        giving.score = counter(state.scores, username);
        state.loot_from.erase(username);
        state.loot_amount.erase(username);
        state.loot_when.erase(username);
        state.dispute_buyer.erase(username);
        state.dispute_amount.erase(username);
        return giving;
    });

    if (result.status == ReturnStatus::done) {
        log_info(
            "loot returned user={} to={} palle={} postage={}",
            username,
            result.victim,
            result.palle,
            result.postage
        );
    }
    return result;
}

std::optional<ReprogramResult> reprogram(Storage &storage, std::int64_t now) {
    const std::optional<ReprogramResult> result =
        storage.transaction([now](StorageSession &session) -> std::optional<ReprogramResult> {
            ConquisterState &state = session.state();
            std::vector<std::string> free_men;
            for (const Counters::value_type &entry : state.scores) {
                if (find_entry(state.reprogrammed, entry.first) == state.reprogrammed.end()) {
                    free_men.push_back(entry.first);
                }
            }
            if (free_men.empty()) {
                return std::nullopt;
            }
            ReprogramResult taken;
            taken.player = free_men[session.random_index(free_men.size())];
            state.reprogrammed[taken.player] = now;
            return taken;
        });

    if (result) {
        log_info("reprogrammed user={}", result->player);
    }
    return result;
}

bool is_reprogrammed(Storage &storage, const std::string &username) {
    return storage.transaction([&username](StorageSession &session) {
        ConquisterState &state = session.state();
        return find_entry(state.reprogrammed, username) != state.reprogrammed.end();
    });
}

FreeingResult set_free(Storage &storage, std::string_view username) {
    const FreeingResult result = storage.transaction([username](StorageSession &session) {
        ConquisterState &state = session.state();
        FreeingResult freeing;
        const Counters::value_type *held = find_ignore_case(state.reprogrammed, username);
        if (held == nullptr) {
            return freeing;
        }
        freeing.known = true;
        /* The paperwork does not always go through. */
        freeing.worked = session.random_index(freeing_fails_one_in) != 0;
        if (freeing.worked) {
            state.reprogrammed.erase(held->first);
        }
        return freeing;
    });

    if (result.known) {
        log_info("freeing user={} worked={}", username, result.worked ? 1 : 0);
    }
    return result;
}

std::optional<TaxResult> flegyas_strike(Storage &storage, int share) {
    const std::optional<TaxResult> result =
        storage.transaction([share](StorageSession &session) -> std::optional<TaxResult> {
            ConquisterState &state = session.state();
            if (share <= 0 || state.scores.empty()) {
                return std::nullopt;
            }
            const std::size_t who = session.random_index(state.scores.size());
            const auto player = std::next(state.scores.begin(), static_cast<std::ptrdiff_t>(who));
            if (player->second <= 0) {
                return std::nullopt;
            }
            TaxResult taken;
            taken.player = player->first;
            taken.palle = player->second / share;
            player->second -= taken.palle;
            taken.left = player->second;
            return taken;
        });

    if (result) {
        log_info("flegyas user={} palle={} left={}", result->player, result->palle, result->left);
    }
    return result;
}

std::optional<TaxResult> tax_the_leader(Storage &storage, int percent, std::int64_t now) {
    const std::optional<TaxResult> result =
        storage.transaction([percent, now](StorageSession &session) -> std::optional<TaxResult> {
            ConquisterState &state = session.state();
            if (percent <= 0 || state.scores.empty()) {
                return std::nullopt;
            }
            const auto leader = std::ranges::max_element(state.scores, [](const auto &first, const auto &second) {
                return ranks_before(second, first);
            });
            if (leader == state.scores.end() || leader->second <= 0) {
                return std::nullopt;
            }
            const std::int64_t last = counter(state.taxed, leader->first);
            if (last != 0 && now - last < 86400) {
                return std::nullopt;
            }
            TaxResult taken;
            taken.player = leader->first;
            taken.palle = leader->second * percent / 100;
            leader->second -= taken.palle;
            taken.left = leader->second;
            state.taxed[leader->first] = now;
            return taken;
        });

    if (result) {
        log_info("taxed user={} palle={} left={}", result->player, result->palle, result->left);
    }
    return result;
}

namespace {

constexpr std::array rule_names{
    "quote_cost",
    "balloon_cost",
    "boost_cost",
    "boost_multiplier",
    "raid_share",
    "travel_divisor",
    "attack_cost",
    "cooldown_seconds",
};

std::array<std::int64_t *, rule_names.size()> rule_fields(Rules &rules) {
    return {
        &rules.quote_cost,
        &rules.balloon_cost,
        &rules.boost_cost,
        &rules.boost_multiplier,
        &rules.raid_share,
        &rules.travel_divisor,
        &rules.attack_cost,
        &rules.cooldown_seconds,
    };
}

}

Rules rules_now(Storage &storage, const Rules &fallback) {
    return storage.transaction([&fallback](StorageSession &session) {
        const ConquisterState &state = session.state();
        Rules rules = fallback;
        const auto fields = rule_fields(rules);
        for (std::size_t which = 0; which < rule_names.size(); ++which) {
            const auto found = std::ranges::find_if(state.rules, [which](const Counters::value_type &entry) {
                return entry.first == rule_names.at(which);
            });
            if (found != state.rules.end()) {
                *fields.at(which) = found->second;
            }
        }
        return rules;
    });
}

Rules scramble_rules(Storage &storage, const Rules &least, const Rules &most) {
    const Rules drawn = storage.transaction([&least, &most](StorageSession &session) {
        ConquisterState &state = session.state();
        Rules rules;
        Rules floors = least;
        Rules ceilings = most;
        const auto fields = rule_fields(rules);
        const auto lowest = rule_fields(floors);
        const auto highest = rule_fields(ceilings);
        for (std::size_t which = 0; which < rule_names.size(); ++which) {
            const std::int64_t low = *lowest.at(which);
            const std::int64_t high = std::max(*highest.at(which), low);
            const auto span = static_cast<std::size_t>(high - low + 1);
            *fields.at(which) = low + static_cast<std::int64_t>(session.random_index(span));
            state.rules[rule_names.at(which)] = *fields.at(which);
        }
        return rules;
    });

    log_info(
        "rules shuffled quote={} balloon={} boost={}x{} share={} travel={} attack={} cooldown={}",
        drawn.quote_cost,
        drawn.balloon_cost,
        drawn.boost_cost,
        drawn.boost_multiplier,
        drawn.raid_share,
        drawn.travel_divisor,
        drawn.attack_cost,
        drawn.cooldown_seconds
    );
    return drawn;
}

std::optional<FlipperResult> flipper_hit(
    Storage &storage,
    const std::string &username,
    int odds,
    std::int64_t now,
    std::int64_t boost
) {
    const std::optional<FlipperResult> result =
        storage.transaction([&](StorageSession &session) -> std::optional<FlipperResult> {
            if (odds <= 0 || session.random_index(static_cast<std::size_t>(odds)) != 0) {
                return std::nullopt;
            }
            ConquisterState &state = session.state();
            FlipperResult hit;
            hit.which = session.random_index(flippers.size());
            const Mishap &what = flippers.at(hit.which);
            const std::int64_t before = counter(state.scores, username);
            hit.palle = what.palle;
            hit.score = before + what.palle;
            switch (what.boon) {
            case Boon::doubled:
                hit.score = before * 2;
                hit.palle = hit.score - before;
                break;
            case Boon::halved:
                hit.score = before / 2;
                hit.palle = hit.score - before;
                break;
            case Boon::balloon:
                if (find_entry(state.balloons, username) == state.balloons.end()) {
                    state.balloons[username] = 0;
                }
                break;
            case Boon::boost:
                state.boosts[username] = boost;
                break;
            case Boon::teleport:
                state.ids.erase(username);
                static_cast<void>(player_id(session, state, username));
                break;
            case Boon::liked:
                static_cast<void>(simpatia_change(state, username, 1, now));
                break;
            case Boon::disliked:
                static_cast<void>(simpatia_change(state, username, -1, now));
                break;
            case Boon::forgiven:
                state.cooldowns.erase(username);
                break;
            case Boon::none:
                break;
            }
            state.scores[username] = hit.score;
            return hit;
        });

    if (result) {
        log_info("flipper user={} which={} palle={}", username, result->which, result->palle);
    }
    return result;
}

std::vector<FlipperResult> cascade(
    Storage &storage,
    const std::string &username,
    int how_many,
    std::int64_t now,
    std::int64_t boost
) {
    std::vector<FlipperResult> chain;
    for (int strike = 0; strike < how_many; ++strike) {
        if (const std::optional<FlipperResult> hit = flipper_hit(storage, username, 1, now, boost)) {
            chain.push_back(*hit);
        }
    }
    log_info("cascade user={} events={}", username, chain.size());
    return chain;
}

LuckyResult lucky_word_said(Storage &storage, const std::string &username, int swing) {
    const LuckyResult result = storage.transaction([&](StorageSession &session) {
        ConquisterState &state = session.state();
        const auto range = static_cast<std::size_t>(std::max(swing, 1) * 2) + 1;
        const std::int64_t change =
            static_cast<std::int64_t>(session.random_index(range)) - std::max(swing, 1);
        const std::int64_t score = counter(state.scores, username) + change;
        state.scores[username] = score;
        return LuckyResult{change, score};
    });

    log_info("lucky word user={} palle={} left={}", username, result.palle, result.score);
    return result;
}

std::optional<MagicResult> magic_word_said(Storage &storage, const std::string &username, int most) {
    const std::optional<MagicResult> result =
        storage.transaction([&](StorageSession &session) -> std::optional<MagicResult> {
            ConquisterState &state = session.state();
            const std::int64_t score = counter(state.scores, username);
            if (score == 0) {
                return std::nullopt;
            }
            MagicResult magic;
            magic.multiplier = 2 + static_cast<std::int64_t>(session.random_index(
                static_cast<std::size_t>(std::max(most, 2) - 1)
            ));
            if (std::abs(score) > std::numeric_limits<std::int64_t>::max() / magic.multiplier) {
                return std::nullopt;
            }
            magic.score = score * magic.multiplier;
            state.scores[username] = magic.score;
            return magic;
        });

    if (result) {
        log_info("magic word user={} multiplier={} left={}", username, result->multiplier, result->score);
    }
    return result;
}

std::int64_t telegram_id_of(Storage &storage, const std::string &username) {
    return storage.transaction([&username](StorageSession &session) {
        const Counters::value_type *found = find_ignore_case(session.state().telegram_ids, username);
        return found != nullptr ? found->second : 0;
    });
}

Leaderboard conquister_leaderboard(Storage &storage, std::size_t limit) {
    return storage.transaction([limit](StorageSession &session) {
        const ConquisterState &state = session.state();
        std::vector<std::pair<std::string, std::int64_t>> ranked(state.scores.begin(), state.scores.end());
        std::ranges::sort(ranked, ranks_before);
        if (limit != 0 && ranked.size() > limit) {
            ranked.resize(limit);
        }
        Leaderboard leaderboard;
        std::ranges::transform(ranked, std::back_inserter(leaderboard.entries), [&state](const auto &entry) {
            return LeaderboardEntry{entry.first, entry.second, counter(state.quotes_added, entry.first)};
        });
        if (state.current && !state.current->username.empty()) {
            leaderboard.current = state.current;
        }
        return leaderboard;
    });
}

std::vector<LeaderboardEntry> conquister_negatives(Storage &storage, std::size_t limit) {
    return storage.transaction([limit](StorageSession &session) {
        const ConquisterState &state = session.state();
        std::vector<std::pair<std::string, std::int64_t>> ranked;
        for (const Counters::value_type &entry : state.scores) {
            if (entry.second < 0) {
                ranked.emplace_back(entry.first, entry.second);
            }
        }
        /* Deepest in the red first, then username in byte order. */
        std::ranges::sort(ranked, [](const auto &first, const auto &second) {
            return first.second != second.second ? first.second < second.second : first.first < second.first;
        });
        if (limit != 0 && ranked.size() > limit) {
            ranked.resize(limit);
        }
        std::vector<LeaderboardEntry> entries;
        std::ranges::transform(ranked, std::back_inserter(entries), [&state](const auto &entry) {
            return LeaderboardEntry{entry.first, entry.second, counter(state.quotes_added, entry.first)};
        });
        return entries;
    });
}

std::optional<ConquisterUser> conquister_user(Storage &storage, std::string_view username) {
    return storage.transaction([username](StorageSession &session) -> std::optional<ConquisterUser> {
        const ConquisterState &state = session.state();
        ConquisterUser user;
        if (state.current && !state.current->username.empty() &&
            text::equals_ignore_case(state.current->username, username)) {
            user.username = state.current->username;
            user.in_conquister = true;
            user.since = state.current->since;
        }
        const Counters::value_type *score = find_ignore_case(state.scores, username);
        const Counters::value_type *added = find_ignore_case(state.quotes_added, username);
        if (!user.in_conquister) {
            if (score == nullptr && added == nullptr) {
                return std::nullopt;
            }
            user.username = score != nullptr ? score->first : added->first;
        }
        if (score != nullptr) {
            const auto ahead = std::ranges::count_if(state.scores, [score](const Counters::value_type &other) {
                return ranks_before(other, *score);
            });
            user.score = score->second;
            user.rank = static_cast<std::size_t>(ahead) + 1;
        }
        if (added != nullptr) {
            user.quotes_added = added->second;
        }
        return user;
    });
}

std::optional<PlayerCard> player_card(
    Storage &storage,
    const std::string &viewer,
    std::string_view username,
    std::int64_t now,
    int travel_divisor
) {
    return storage.transaction([&](StorageSession &session) -> std::optional<PlayerCard> {
        ConquisterState &state = session.state();
        const std::optional<std::string> known = known_player(state, username);
        if (!known) {
            return std::nullopt;
        }
        PlayerCard card;
        card.username = *known;
        if (const Counters::value_type *score = find_ignore_case(state.scores, *known); score != nullptr) {
            card.score = score->second;
            const auto ahead = std::ranges::count_if(state.scores, [score](const Counters::value_type &other) {
                return ranks_before(other, *score);
            });
            card.rank = static_cast<std::size_t>(ahead) + 1;
        }
        card.quotes_added = counter(state.quotes_added, *known);
        card.simpatia = simpatia_now(state, *known, now);
        if (state.current && state.current->username == *known) {
            card.in_conquister = true;
            card.held_seconds = now > state.current->since ? now - state.current->since : 0;
        }
        if (const auto balloon = find_entry(state.balloons, *known); balloon != state.balloons.end()) {
            card.balloon_attempts = static_cast<int>(balloon->second);
        }
        if (const auto shield = find_entry(state.shields, *known);
            shield != state.shields.end() && shield->second > now) {
            card.shield_seconds = shield->second - now;
        }
        card.boost_multiplier = counter(state.boosts, *known);
        if (const auto penalty = find_entry(state.cooldowns, *known);
            penalty != state.cooldowns.end() && penalty->second > now) {
            card.cooldown_seconds = penalty->second - now;
        }
        if (const Raid *raid = raid_of(state, *known); raid != nullptr) {
            card.travelling = true;
            card.carrying = raid->arrived;
            card.travel_target = raid->target;
            card.travel_seconds =
                std::max<std::int64_t>((raid->arrived ? raid->back : raid->arrive) - now, 0);
        }
        if (!text::equals_ignore_case(viewer, *known)) {
            const position::Point mine = position::coordinates_of(player_id(session, state, viewer));
            const position::Point theirs = position::coordinates_of(player_id(session, state, *known));
            card.distance_seconds =
                position::travel_seconds(position::distance(mine, theirs), travel_divisor);
        }
        return card;
    });
}

BalloonResult balloon_buy(
    Storage &storage,
    const std::string &username,
    int cost,
    std::int64_t now,
    std::int64_t shield_seconds
) {
    const BalloonResult result = storage.transaction([&](StorageSession &session) {
        ConquisterState &state = session.state();
        const std::int64_t score = counter(state.scores, username);
        if (const auto shield = find_entry(state.shields, username); shield != state.shields.end()) {
            if (shield->second > now) {
                return BalloonResult{BalloonStatus::already_owned, score, shield->second - now};
            }
            state.shields.erase(username);
        }
        if (find_entry(state.balloons, username) != state.balloons.end()) {
            return BalloonResult{BalloonStatus::already_owned, score};
        }
        if (find_entry(state.boosts, username) != state.boosts.end()) {
            return BalloonResult{BalloonStatus::has_boost, score};
        }
        if (score < cost) {
            return BalloonResult{BalloonStatus::insufficient_score, score};
        }
        state.scores[username] = score - cost;
        if (shield_seconds > 0) {
            state.shields[username] = now + shield_seconds;
        } else {
            state.balloons[username] = 0;
        }
        return BalloonResult{BalloonStatus::bought, score - cost, shield_seconds};
    });

    if (result.status == BalloonStatus::bought) {
        log_info(
            "balloon bought user={} cost={} left={} shield={}",
            username,
            cost,
            result.available_score,
            result.shield_seconds
        );
    }
    return result;
}

std::optional<MishapResult> mishap_strike(Storage &storage, std::int64_t now, std::int64_t boost) {
    const std::optional<MishapResult> result =
        storage.transaction([&](StorageSession &session) -> std::optional<MishapResult> {
            ConquisterState &state = session.state();
            if (state.scores.empty()) {
                return std::nullopt;
            }
            const std::size_t who = session.random_index(state.scores.size());
            const auto player = std::next(state.scores.begin(), static_cast<std::ptrdiff_t>(who));
            const std::string name = player->first;
            MishapResult mishap;
            mishap.player = name;
            mishap.which = session.random_index(mishaps.size());
            const Mishap &what = mishaps.at(mishap.which);
            mishap.palle = what.palle;
            player->second += what.palle;
            switch (what.boon) {
            case Boon::balloon:
                if (find_entry(state.balloons, name) == state.balloons.end()) {
                    state.balloons[name] = 0;
                }
                break;
            case Boon::boost:
                state.boosts[name] = boost;
                break;
            case Boon::teleport:
                state.ids.erase(name);
                static_cast<void>(player_id(session, state, name));
                break;
            case Boon::liked:
                static_cast<void>(simpatia_change(state, name, 1, now));
                break;
            case Boon::disliked:
                static_cast<void>(simpatia_change(state, name, -1, now));
                break;
            case Boon::forgiven:
                state.cooldowns.erase(name);
                break;
            case Boon::doubled:
            case Boon::halved:
            case Boon::none:
                break;
            }
            return mishap;
        });

    if (result) {
        log_info("mishap user={} which={} palle={}", result->player, result->which, result->palle);
    }
    return result;
}

RaidResult raid_start(
    Storage &storage,
    std::int64_t user_id,
    const std::string &username,
    std::string_view target,
    std::int64_t now,
    const RaidRules &rules
) {
    const RaidResult result = storage.transaction([&](StorageSession &session) {
        ConquisterState &state = session.state();
        remember_telegram(state, username, user_id);
        RaidResult outcome;
        const bool homewards = text::equals_ignore_case(username, target);
        const bool holds_place = state.current && text::equals_ignore_case(state.current->username, username);
        Raid *travelling = raid_of(state, username);
        /* Naming yourself is the way home. */
        if (homewards) {
            if (travelling != nullptr) {
                /* He turns his back on the raid and rides the rest of the way home. */
                travelling->arrived = true;
                outcome.status = RaidStatus::coming_home;
                outcome.seconds = std::max<std::int64_t>(travelling->back - now, 0);
                return outcome;
            }
            if (holds_place) {
                const std::string holder = state.current->username;
                const Settlement settled =
                    settle_hold(state, holder, state.current->since, now, rules.signs);
                state.current.reset();
                outcome.status = RaidStatus::left_place;
                outcome.earned = settled.earned;
                outcome.boost_multiplier = settled.boost_multiplier;
                outcome.zodiac_percent = settled.zodiac_percent;
                return outcome;
            }
            outcome.status = RaidStatus::home_already;
            return outcome;
        }
        /* Whoever is already on the road only gets told so. */
        if (travelling != nullptr) {
            outcome.status = RaidStatus::already_travelling;
            outcome.seconds = std::max<std::int64_t>(travelling->back - now, 0);
            return outcome;
        }
        /* Whoever holds the place stays in it: leaving would be leaving it behind. */
        if (holds_place) {
            outcome.status = RaidStatus::holding_place;
            return outcome;
        }
        const std::optional<std::string> known = known_player(state, target);
        if (!known) {
            outcome.status = RaidStatus::unknown_target;
            return outcome;
        }
        outcome.target = *known;
        const position::Point home = position::coordinates_of(player_id(session, state, username));
        const position::Point theirs = position::coordinates_of(player_id(session, state, *known));
        outcome.seconds = position::travel_seconds(position::distance(home, theirs), rules.travel_divisor);
        state.raids.push_back(Raid{
            .raider = username,
            .target = *known,
            .arrive = now + outcome.seconds,
            .back = now + (2 * outcome.seconds),
            .arrived = false,
            .loot = 0,
        });
        return outcome;
    });

    if (result.status == RaidStatus::started) {
        log_info("raid started user={} target={} travel={}", username, result.target, result.seconds);
    }
    if (result.status == RaidStatus::left_place) {
        log_info("left the place user={} earned={}", username, result.earned);
    }
    if (result.status == RaidStatus::coming_home) {
        log_info("raid called off user={} home_in={}", username, result.seconds);
    }
    return result;
}

std::vector<RaidEvent> raid_due(Storage &storage, std::int64_t now, const RaidRules &rules) {
    const std::vector<RaidEvent> events = storage.transaction([&](StorageSession &session) {
        ConquisterState &state = session.state();
        std::vector<RaidEvent> settled;
        for (Raid &raid : state.raids) {
            if (!raid.arrived && now >= raid.arrive) {
                raid.arrived = true;
                RaidEvent event{.kind = RaidEvent::Kind::stolen, .raider = raid.raider, .target = raid.target};
                event.seconds = std::max<std::int64_t>(raid.back - now, 0);
                event.target_on_telegram = counter(state.telegram_ids, raid.target) != 0;
                event.raider_on_telegram = counter(state.telegram_ids, raid.raider) != 0;
                /* He is answered like everyone else and gets nowhere, as the owner asked. */
                const bool shadowed = std::ranges::any_of(rules.shadowed, [&raid](const std::string &name) {
                    return text::equals_ignore_case(name, raid.raider);
                });
                const bool guarded = shadowed || !is_away(state, raid.target);
                event.undefended = !guarded;
                const auto shield = find_entry(state.shields, raid.target);
                const auto balloon = find_entry(state.balloons, raid.target);
                if (shadowed) {
                    event.kind = RaidEvent::Kind::defended;
                    event.undefended = false;
                    event.cost = charge_attacker(state, raid.raider, rules.attack_cost);
                } else if (guarded && shield != state.shields.end() && shield->second > now) {
                    event.kind = RaidEvent::Kind::defended;
                    event.cost = charge_attacker(state, raid.raider, rules.attack_cost);
                } else if (guarded && balloon != state.balloons.end()) {
                    const std::int64_t attempt = balloon->second + 1;
                    if (static_cast<std::int64_t>(session.random_index(balloon_attempts)) >= attempt) {
                        balloon->second = attempt;
                        event.kind = RaidEvent::Kind::defended;
                        event.cost = charge_attacker(state, raid.raider, rules.attack_cost);
                    } else {
                        state.balloons.erase(raid.target);
                        event.balloon_popped = true;
                    }
                }
                if (event.kind == RaidEvent::Kind::stolen) {
                    const std::int64_t theirs = counter(state.scores, raid.target);
                    event.raider_percent = zodiac::percent_for(raid.raider, now, rules.signs);
                    event.target_percent = zodiac::percent_for(raid.target, now, rules.signs);
                    /* Nothing to carry away from somebody who owns less than nothing. */
                    if (theirs > 0 && rules.loot_share > 0) {
                        const std::int64_t share = theirs / rules.loot_share;
                        event.loot = std::min(theirs, share * event.raider_percent / event.target_percent);
                    }
                    if (event.loot > 0) {
                        state.scores[raid.target] = theirs - event.loot;
                        /* Robbing people is not the way to be liked; being robbed earns some. */
                        event.simpatia = simpatia_change(state, raid.raider, -1, now);
                        event.denounced = event.simpatia < simpatia_threshold &&
                                          event.simpatia + 1 >= simpatia_threshold;
                        static_cast<void>(simpatia_change(state, raid.target, 1, now));
                    }
                    raid.loot = event.loot;
                }
                settled.push_back(std::move(event));
            }
            if (raid.arrived && now >= raid.back) {
                const std::int64_t carried = counter(state.scores, raid.raider);
                if (raid.loot > 0) {
                    state.scores[raid.raider] = carried + raid.loot;
                }
                if (raid.loot > 0) {
                    state.loot_from[raid.raider] = raid.target;
                    state.loot_amount[raid.raider] = raid.loot;
                    state.loot_when[raid.raider] = now;
                }
                settled.push_back(RaidEvent{
                    .kind = RaidEvent::Kind::returned,
                    .raider = raid.raider,
                    .target = raid.target,
                    .loot = raid.loot,
                    .raider_on_telegram = counter(state.telegram_ids, raid.raider) != 0,
                });
            }
        }
        std::erase_if(state.raids, [now](const Raid &raid) { return raid.arrived && now >= raid.back; });
        return settled;
    });

    for (const RaidEvent &event : events) {
        switch (event.kind) {
        case RaidEvent::Kind::stolen:
            log_info(
                "raid stolen user={} target={} loot={} undefended={} balloon_popped={} percent={}/{}",
                event.raider,
                event.target,
                event.loot,
                event.undefended ? 1 : 0,
                event.balloon_popped ? 1 : 0,
                event.raider_percent,
                event.target_percent
            );
            break;
        case RaidEvent::Kind::defended:
            log_info("raid defended user={} target={} cost={}", event.raider, event.target, event.cost);
            break;
        case RaidEvent::Kind::returned:
            log_info("raid returned user={} target={} loot={}", event.raider, event.target, event.loot);
            break;
        }
    }
    return events;
}

BoostResult boost_buy(
    Storage &storage,
    const std::string &username,
    int cost,
    std::int64_t multiplier,
    std::int64_t now
) {
    const BoostResult result = storage.transaction([&](StorageSession &session) {
        ConquisterState &state = session.state();
        const std::int64_t score = counter(state.scores, username);
        if (const auto boost = find_entry(state.boosts, username); boost != state.boosts.end()) {
            return BoostResult{BoostStatus::already_owned, score, boost->second};
        }
        if (find_entry(state.balloons, username) != state.balloons.end()) {
            return BoostResult{BoostStatus::has_balloon, score};
        }
        if (const auto shield = find_entry(state.shields, username); shield != state.shields.end()) {
            if (shield->second > now) {
                return BoostResult{BoostStatus::has_balloon, score};
            }
            state.shields.erase(username);
        }
        if (score < cost) {
            return BoostResult{BoostStatus::insufficient_score, score};
        }
        state.scores[username] = score - cost;
        state.boosts[username] = multiplier;
        return BoostResult{BoostStatus::bought, score - cost, multiplier};
    });

    if (result.status == BoostStatus::bought) {
        log_info(
            "boost bought user={} cost={} left={} multiplier={}",
            username,
            cost,
            result.available_score,
            result.multiplier
        );
    }
    return result;
}

QuoteAddResult quote_pretend(Storage &storage, const std::string &username, int cost) {
    const QuoteAddResult result = storage.transaction([&](StorageSession &session) {
        ConquisterState &state = session.state();
        const std::int64_t score = counter(state.scores, username);
        if (score < cost) {
            return QuoteAddResult{QuoteAddStatus::insufficient_score, score};
        }
        state.scores[username] = score - cost;
        return QuoteAddResult{QuoteAddStatus::added, score - cost};
    });

    if (result.status == QuoteAddStatus::added) {
        log_info("quote dropped user={} cost={} left={}", username, cost, result.available_score);
    }
    return result;
}

QuoteAddResult quote_add(
    Storage &storage,
    const std::string &username,
    const std::string &quote,
    int cost,
    std::int64_t now
) {
    const QuoteAddResult result = storage.transaction([&](StorageSession &session) {
        ConquisterState &state = session.state();
        Quotes &quotes = session.quotes();
        const std::int64_t score = counter(state.scores, username);
        if (score < cost) {
            return QuoteAddResult{QuoteAddStatus::insufficient_score, score};
        }
        if (std::ranges::find(quotes, quote) != quotes.end()) {
            return QuoteAddResult{QuoteAddStatus::duplicate, score};
        }
        const std::int64_t added = counter(state.quotes_added, username);
        if (added == std::numeric_limits<std::int64_t>::max()) {
            throw StorageError("Quote counter overflow");
        }
        quotes.push_back(quote);
        state.quote_authors[quote] = username;
        static_cast<void>(simpatia_change(state, username, 1, now));
        state.scores[username] = score - cost;
        state.quotes_added[username] = added + 1;
        return QuoteAddResult{QuoteAddStatus::added, score - cost};
    });

    if (result.status == QuoteAddStatus::added) {
        log_info("quote added user={} cost={} left={}", username, cost, result.available_score);
    }
    return result;
}

QuotePage quote_page_load(Storage &storage, int requested_page) {
    return storage.transaction([requested_page](StorageSession &session) {
        const Quotes &quotes = session.quotes();
        QuotePage page;
        page.total = quotes.size();
        if (page.total == 0) {
            return page;
        }
        page.pages = ((page.total - 1) / quotes_page_size) + 1;
        page.page = std::min(requested_page > 0 ? static_cast<std::size_t>(requested_page) : 1U, page.pages);
        const std::size_t offset = (page.page - 1) * quotes_page_size;
        page.first_number = offset + 1;
        page.items = quotes | std::views::drop(offset) | std::views::take(quotes_page_size) |
                     std::ranges::to<std::vector<std::string>>();
        const Authors &authors = session.state().quote_authors;
        std::ranges::transform(page.items, std::back_inserter(page.authors), [&authors](const std::string &quote) {
            const auto found = authors.find(quote);
            return found != authors.end() ? found->second : std::string{};
        });
        return page;
    });
}

std::optional<std::string> quote_random(Storage &storage) {
    return storage.transaction([](StorageSession &session) -> std::optional<std::string> {
        const Quotes &quotes = session.quotes();
        if (quotes.empty()) {
            return std::nullopt;
        }
        return quotes[session.random_index(quotes.size())];
    });
}

std::optional<std::string> quote_delete(Storage &storage, std::string_view selector) {
    return storage.transaction([selector](StorageSession &session) -> std::optional<std::string> {
        Quotes &quotes = session.quotes();
        const std::optional<std::int64_t> position = text::parse_int64(selector);
        const auto selected = position && *position > 0 && static_cast<std::uint64_t>(*position) <= quotes.size()
            ? quotes.begin() + static_cast<std::ptrdiff_t>(*position - 1)
            : std::ranges::find(quotes, std::string{selector});
        if (selected == quotes.end()) {
            return std::nullopt;
        }
        std::string removed = std::move(*selected);
        quotes.erase(selected);
        ConquisterState &state = session.state();
        /* A quote that is deleted no longer counts for whoever added it. */
        if (const auto author = state.quote_authors.find(removed); author != state.quote_authors.end()) {
            if (const std::int64_t added = counter(state.quotes_added, author->second); added > 0) {
                state.quotes_added[author->second] = added - 1;
            }
            state.quote_authors.erase(removed);
        }
        return removed;
    });
}

}
