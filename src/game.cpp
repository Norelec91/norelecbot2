#include "game.hpp"

#include "logging.hpp"
#include "position.hpp"
#include "text.hpp"
#include "zodiac.hpp"

#include <algorithm>
#include <array>
#include <cctype>
#include <cmath>
#include <iterator>
#include <limits>
#include <ranges>
#include <utility>

namespace norelecbot {
namespace {

constexpr std::size_t quotes_page_size = 30;
std::string display_name(const ConquisterState &state, const std::string &key);

/* Higher score first, then username in byte order. */
constexpr auto ranks_before = [](const auto &first, const auto &second) {
    return first.second != second.second ? first.second > second.second : first.first < second.first;
};

std::int64_t counter(const Counters &counters, const std::string &username) {
    const auto found = counters.find(username);
    return found != counters.end() ? found->second : 0;
}

std::string lower_name(std::string_view name) {
    std::string lowered{name};
    std::ranges::transform(lowered, lowered.begin(), [](unsigned char character) {
        return static_cast<char>(std::tolower(character));
    });
    return lowered;
}

const Counters::value_type *find_ignore_case(const Counters &counters, std::string_view username) {
    const auto found = std::ranges::find_if(counters, [username](const Counters::value_type &entry) {
        return text::equals_ignore_case(entry.first, username);
    });
    return found != counters.end() ? &*found : nullptr;
}

/* Four attempts at most: the first has one chance in four, the fourth is certain. */
constexpr std::size_t balloon_attempts = 4;
/* A successful raid halves the next loot, up to three times; one level recovers every two hours. */
constexpr std::int64_t max_raid_resistance = 3;
constexpr std::int64_t raid_resistance_recovery_seconds = 2 * 60 * 60;

Counters::iterator find_entry(Counters &counters, const std::string &username) {
    return std::ranges::find_if(counters, [&username](const Counters::value_type &entry) {
        return entry.first == username;
    });
}

/* What somebody hung beside his name, or nothing. */
std::string furniture_of(const ConquisterState &state, const std::string &username) {
    const auto mine = std::ranges::find_if(state.furniture, [&username](const Authors::value_type &entry) {
        return entry.first == username;
    });
    return mine == state.furniture.end() ? std::string{} : mine->second;
}

Authors::iterator find_entry(Authors &authors, const std::string &username) {
    return std::ranges::find_if(authors, [&username](const Authors::value_type &entry) {
        return entry.first == username;
    });
}

}

namespace {

/* A balloon that holds costs the attacker, who cannot go below nothing. */
std::int64_t charge_attacker(ConquisterState &state, const std::string &username, int attack_cost) {
    const std::int64_t available = counter(state.scores, username);
    const std::int64_t charged = std::min<std::int64_t>(attack_cost, available);
    if (charged > 0) {
        state.scores[username] = available - charged;
    }
    return charged;
}

/* Apply x/(x+1000) to the potential loot x, without overflowing x*x for large scores. */
std::int64_t shielded_loot(std::int64_t loot) {
    constexpr std::int64_t k = 1000;
    if (loot <= 0) {
        return loot;
    }
    /* For x > k*(k-1), floor(x*x/(x+k)) is exactly x-k. */
    return loot > k * (k - 1) ? loot - k : loot * loot / (loot + k);
}

std::int64_t planet_resisted_loot(ConquisterState &state, const std::string &target,
                                 std::int64_t loot, std::int64_t now) {
    if (loot <= 0) {
        return loot;
    }
    std::int64_t level = std::clamp(counter(state.raid_resistance_levels, target),
                                    std::int64_t{0}, max_raid_resistance);
    std::int64_t since = counter(state.raid_resistance_since, target);
    if (since > now) {
        since = now;
    }
    if (level > 0 && now > since) {
        const std::uint64_t elapsed = static_cast<std::uint64_t>(now) -
                                      static_cast<std::uint64_t>(since);
        const std::int64_t recovered = static_cast<std::int64_t>(std::min<std::uint64_t>(
            static_cast<std::uint64_t>(level), elapsed / raid_resistance_recovery_seconds));
        level -= recovered;
        since = level > 0 ? since + recovered * raid_resistance_recovery_seconds : now;
    }
    const std::int64_t reduced = loot / (std::int64_t{1} << level);
    if (reduced > 0) {
        state.raid_resistance_levels[target] = std::min(level + 1, max_raid_resistance);
        state.raid_resistance_since[target] = level == 0 ? now : since;
    }
    return reduced;
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
    settled.zodiac_percent = zodiac::percent_for(display_name(state, holder), now, signs);
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

/* A raid takes the player away from their own planet until the return trip ends. */
bool is_away(const ConquisterState &state, const std::string &username) {
    return std::ranges::any_of(state.raids, [&username](const Raid &raid) {
        return raid.raider == username;
    });
}

/* Occupying @TheConquister37 is not the same as being on one's own planet. */
bool on_own_planet(const ConquisterState &state, const std::string &username) {
    return !is_away(state, username) &&
        (!state.current || !text::equals_ignore_case(state.current->username, username));
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

void remember_irc(ConquisterState &state, const std::string &username) {
    if (find_ignore_case(state.irc_names, username) == nullptr) {
        state.irc_names[username] = 1;
    }
}

void remember_platform(ConquisterState &state, const std::string &username, std::int64_t user_id) {
    if (user_id == 0) {
        remember_irc(state, username);
    } else {
        remember_telegram(state, username, user_id);
    }
}

std::string platform_key(std::int64_t user_id, std::string_view username, std::string_view account_name) {
    return user_id != 0 ? "tg:" + std::to_string(user_id)
                        : "irc:" + lower_name(account_name.empty() ? username : account_name);
}

std::string display_name(const ConquisterState &state, const std::string &key) {
    const auto found = state.display_names.find(key);
    return found != state.display_names.end() ? found->second : key;
}

std::optional<std::string> known_player(const ConquisterState &state, std::string_view name);

bool ambiguous_legacy(const ConquisterState &state, std::string_view key) {
    if (state.display_names.find(std::string{key}) != state.display_names.end()) {
        return false;
    }
    const bool current = state.current && text::equals_ignore_case(state.current->username, key);
    const bool telegram = find_ignore_case(state.telegram_ids, key) != nullptr ||
        (current && state.current->user_id != 0);
    const bool irc = find_ignore_case(state.irc_names, key) != nullptr ||
        (current && state.current->user_id == 0);
    return telegram && irc;
}

/* Never give a historical name to a different Telegram ID or to an IRC namesake. */
std::optional<std::string> legacy_owner(const ConquisterState &state, std::int64_t user_id,
                                        std::string_view username, std::string_view account_name) {
    std::optional<std::string> candidate;
    if (user_id != 0) {
        for (const auto &[name, id] : state.telegram_ids) {
            if (id == user_id) {
                if (candidate && *candidate != name) {
                    return std::nullopt;
                }
                candidate = name;
            }
        }
        if (!candidate && state.current && state.current->user_id == user_id) {
            candidate = state.current->username;
        }
    } else {
        const auto known = known_player(state, username);
        if (known && find_ignore_case(state.telegram_ids, *known) == nullptr &&
            (find_ignore_case(state.irc_names, *known) != nullptr ||
             (state.current && state.current->user_id == 0 && state.current->username == *known))) {
            candidate = *known;
        }
    }
    if (!candidate) {
        return std::nullopt;
    }
    if (ambiguous_legacy(state, *candidate)) {
        return std::nullopt;
    }
    for (const auto &[account, key] : state.accounts) {
        if (key == *candidate && account != platform_key(user_id, username, account_name)) {
            return std::nullopt;
        }
    }
    return candidate;
}

/* The name as it is written on file, whatever spelling the message used. */
std::optional<std::string> known_player(const ConquisterState &state, std::string_view name) {
    for (const Counters *counters : {&state.scores, &state.quotes_added, &state.ids,
                                     &state.balloons, &state.cooldowns, &state.shields,
                                     &state.boosts, &state.raid_shields,
                                     &state.raid_resistance_levels}) {
        if (const Counters::value_type *found = find_ignore_case(*counters, name); found != nullptr) {
            return found->first;
        }
    }
    for (const InvestmentDeposit &deposit : state.investments) {
        if (text::equals_ignore_case(deposit.player, name)) {
            return deposit.player;
        }
    }
    if (state.current && text::equals_ignore_case(state.current->username, name)) {
        return state.current->username;
    }
    return std::nullopt;
}

std::optional<std::string> player_by_name(const ConquisterState &state, std::string_view name,
                                          RaidTargetKind platform) {
    if (platform != RaidTargetKind::any) {
        const Authors &names = platform == RaidTargetKind::telegram ? state.telegram_names : state.irc_nicks;
        const auto found = names.find(lower_name(name));
        if (found != names.end()) {
            return found->second;
        }
        /* Read-only compatibility for players in pre-identity saves who have not spoken yet. */
        auto legacy = known_player(state, name);
        if (!legacy || state.display_names.find(*legacy) != state.display_names.end() ||
            ambiguous_legacy(state, *legacy)) {
            return std::nullopt;
        }
        if (platform == RaidTargetKind::telegram && find_ignore_case(state.telegram_ids, *legacy)) {
            return legacy;
        }
        if (platform == RaidTargetKind::irc && find_ignore_case(state.telegram_ids, *legacy) == nullptr &&
            (find_ignore_case(state.irc_names, *legacy) ||
             (state.current && state.current->user_id == 0 && state.current->username == *legacy))) {
            return legacy;
        }
        return std::nullopt;
    }
    if (const auto tg = state.telegram_names.find(lower_name(name)); tg != state.telegram_names.end()) {
        return tg->second;
    }
    if (const auto irc = state.irc_nicks.find(lower_name(name)); irc != state.irc_nicks.end()) {
        return irc->second;
    }
    return known_player(state, name);
}

}

std::string player_seen(Storage &storage, std::int64_t user_id, const std::string &username,
                        std::string_view account_name) {
    return storage.transaction([&](StorageSession &session) {
        ConquisterState &state = session.state();
        const std::string account = platform_key(user_id, username, account_name);
        auto binding = state.accounts.find(account);
        if (binding == state.accounts.end()) {
            const std::string key = legacy_owner(state, user_id, username, account_name).value_or(account);
            state.accounts[account] = key;
            binding = state.accounts.find(account);
        }
        const std::string key = binding->second;
        Authors &names = user_id != 0 ? state.telegram_names : state.irc_nicks;
        for (auto entry = names.begin(); entry != names.end();) {
            if (entry->second == key) {
                entry = names.erase(entry);
            } else {
                ++entry;
            }
        }
        names[lower_name(username)] = key;
        if (user_id != 0 || counter(state.telegram_ids, key) == 0) {
            state.display_names[key] = username;
        }
        remember_platform(state, key, user_id);
        return key;
    });
}

LinkStatus player_link(Storage &storage, std::int64_t user_id, const std::string &username,
                       std::string_view other_name, std::string_view account_name) {
    return storage.transaction([&](StorageSession &session) {
        ConquisterState &state = session.state();
        const std::string mine = platform_key(user_id, username, account_name);
        const Authors &names = user_id != 0 ? state.irc_nicks : state.telegram_names;
        const auto other = names.find(lower_name(other_name));
        if (other == names.end()) {
            return LinkStatus::unknown_account;
        }
        std::string theirs;
        for (const auto &[account, key] : state.accounts) {
            if (key == other->second && account.starts_with(user_id != 0 ? "irc:" : "tg:")) {
                theirs = account;
                break;
            }
        }
        if (theirs.empty()) {
            return LinkStatus::unknown_account;
        }
        if (state.accounts.at(mine) == state.accounts.at(theirs)) {
            return LinkStatus::self;
        }
        const auto reciprocal = state.link_requests.find(theirs);
        if (reciprocal == state.link_requests.end() || reciprocal->second != mine) {
            state.link_requests[mine] = theirs;
            return LinkStatus::pending;
        }
        const std::string mine_key = state.accounts.at(mine);
        const std::string other_key = state.accounts.at(theirs);
        for (const auto &[account, key] : state.accounts) {
            if ((key == mine_key && account != mine && account.starts_with(user_id != 0 ? "irc:" : "tg:")) ||
                (key == other_key && account != theirs && account.starts_with(user_id != 0 ? "tg:" : "irc:"))) {
                return LinkStatus::already_linked;
            }
        }
        const auto has_assets = [&state](const std::string &key) {
            const std::array items{&state.scores, &state.quotes_added, &state.balloons,
                                   &state.cooldowns, &state.shields, &state.boosts,
                                   &state.raid_shields, &state.raid_resistance_levels,
                                   &state.raid_resistance_since, &state.ids, &state.debugging};
            return std::ranges::any_of(items, [&key](const Counters *entries) {
                return entries->find(key) != entries->end();
            }) || (state.current && state.current->username == key) ||
                state.furniture.find(key) != state.furniture.end() ||
                std::ranges::any_of(state.raids, [&key](const Raid &raid) {
                    return raid.raider == key || raid.target == key;
                }) ||
                std::ranges::any_of(state.quote_authors, [&key](const Authors::value_type &entry) {
                    return entry.second == key;
                }) ||
                std::ranges::any_of(state.investments, [&key](const InvestmentDeposit &deposit) {
                    return deposit.player == key;
                });
        };
        if (has_assets(mine_key) && has_assets(other_key)) {
            return LinkStatus::conflict;
        }
        const std::string primary = has_assets(other_key) ? other_key : mine_key;
        const std::string secondary = primary == mine_key ? other_key : mine_key;
        const std::string telegram_display = counter(state.telegram_ids, primary) != 0
            ? display_name(state, primary)
            : counter(state.telegram_ids, secondary) != 0 ? display_name(state, secondary) : std::string{};
        for (auto &[account, key] : state.accounts) {
            if (key == secondary) {
                key = primary;
            }
        }
        for (auto &[name, key] : state.telegram_names) {
            if (key == secondary) {
                key = primary;
            }
        }
        for (auto &[name, key] : state.irc_nicks) {
            if (key == secondary) {
                key = primary;
            }
        }
        if (const auto id = state.telegram_ids.find(secondary); id != state.telegram_ids.end()) {
            state.telegram_ids[primary] = id->second;
            state.telegram_ids.erase(secondary);
        }
        if (state.irc_names.erase(secondary) > 0) {
            state.irc_names[primary] = 1;
        }
        for (InvestmentDeposit &deposit : state.investments) {
            if (deposit.player == secondary) {
                deposit.player = primary;
            }
        }
        state.display_names.erase(secondary);
        if (!telegram_display.empty()) {
            state.display_names[primary] = telegram_display;
        }
        state.link_requests.erase(mine);
        state.link_requests.erase(theirs);
        return LinkStatus::linked;
    });
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
        remember_platform(state, username, user_id);
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
            outcome.previous_user_id = !ambiguous_legacy(state, holder) &&
                counter(state.telegram_ids, holder) != 0
                ? counter(state.telegram_ids, holder) : state.current->user_id;
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
                    outcome.previous_username = display_name(state, holder);
                    outcome.previous_key = holder;
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
                    outcome.previous_username = display_name(state, holder);
                    outcome.previous_key = holder;
                    outcome.next_chance =
                        static_cast<int>((attempt + 1) * 100 / static_cast<std::int64_t>(balloon_attempts));
                    return outcome;
                }
                state.balloons.erase(holder);
                outcome.balloon_popped = true;
            }
            outcome.previous_username = display_name(state, holder);
            outcome.previous_key = holder;
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
            return LeaderboardEntry{display_name(state, entry.first), entry.first, entry.second,
                                    counter(state.quotes_added, entry.first)};
        });
        if (state.current && !state.current->username.empty()) {
            leaderboard.current = state.current;
            leaderboard.current_key = state.current->username;
            leaderboard.current->username = display_name(state, state.current->username);
        }
        return leaderboard;
    });
}

std::optional<ConquisterUser> conquister_user(Storage &storage, std::string_view username,
                                             RaidTargetKind platform) {
    return storage.transaction([username, platform](StorageSession &session) -> std::optional<ConquisterUser> {
        const ConquisterState &state = session.state();
        const auto known = player_by_name(state, username, platform);
        if (!known) {
            return std::nullopt;
        }
        const std::string &key = *known;
        ConquisterUser user;
        if (state.current && !state.current->username.empty() &&
            state.current->username == key) {
            user.username = display_name(state, key);
            user.in_conquister = true;
            user.since = state.current->since;
        }
        const Counters::value_type *score = find_ignore_case(state.scores, key);
        const Counters::value_type *added = find_ignore_case(state.quotes_added, key);
        if (!user.in_conquister) {
            if (score == nullptr && added == nullptr) {
                return std::nullopt;
            }
            user.username = display_name(state, key);
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

Wealth wealth_now(Storage &storage) {
    return storage.transaction([](StorageSession &session) {
        const ConquisterState &state = session.state();
        Wealth wealth;
        std::vector<std::int64_t> scores;
        scores.reserve(state.scores.size());
        for (const Counters::value_type &entry : state.scores) {
            wealth.total += entry.second;
            scores.push_back(entry.second);
        }
        wealth.players = scores.size();
        if (scores.empty()) {
            return wealth;
        }
        const auto middle = scores.begin() + static_cast<std::ptrdiff_t>(scores.size() / 2);
        std::ranges::nth_element(scores, middle);
        wealth.middle = *middle;
        return wealth;
    });
}

void debug_set(Storage &storage, const std::string &username, bool wanted) {
    storage.transaction([&username, wanted](StorageSession &session) {
        ConquisterState &state = session.state();
        if (wanted) {
            state.debugging[username] = 1;
        } else {
            state.debugging.erase(username);
        }
        return 0;
    });
    log_info("debug user={} {}", username, wanted ? "on" : "off");
}

bool debug_on(Storage &storage, const std::string &username) {
    return storage.transaction([&username](StorageSession &session) {
        return counter(session.state().debugging, username) != 0;
    });
}

FurnitureResult furniture_buy(
    Storage &storage,
    const std::string &username,
    const std::string &emoji,
    int cost,
    std::size_t limit
) {
    const FurnitureResult result = storage.transaction([&](StorageSession &session) {
        ConquisterState &state = session.state();
        const std::int64_t score = counter(state.scores, username);
        const auto mine = find_entry(state.furniture, username);
        const std::string kept = mine == state.furniture.end() ? std::string{} : mine->second;
        const std::size_t had = text::emoji_count(kept).value_or(0);
        const std::size_t asked = text::emoji_count(emoji).value_or(0);
        if (had + asked > limit) {
            return FurnitureResult{FurnitureStatus::too_many, score, kept, had};
        }
        if (score < cost) {
            return FurnitureResult{FurnitureStatus::insufficient_score, score, kept, had};
        }
        state.scores[username] = score - cost;
        const std::string shown = kept + emoji;
        if (mine != state.furniture.end()) {
            mine->second = shown;
        } else {
            state.furniture[username] = shown;
        }
        return FurnitureResult{FurnitureStatus::bought, score - cost, shown, had + asked};
    });

    log_info(
        "furniture user={} status={} howmany={}",
        username,
        static_cast<int>(result.status),
        result.howmany
    );
    return result;
}

Authors furniture_all(Storage &storage) {
    return storage.transaction([](StorageSession &session) { return session.state().furniture; });
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
        state.raid_shields.erase(username);
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

RaidShieldResult raid_shield_buy(Storage &storage, const std::string &username, int cost, std::int64_t now) {
    const RaidShieldResult result = storage.transaction([&](StorageSession &session) {
        ConquisterState &state = session.state();
        const std::int64_t score = counter(state.scores, username);
        if (find_entry(state.raid_shields, username) != state.raid_shields.end()) {
            return RaidShieldResult{RaidShieldStatus::already_owned, score};
        }
        if (find_entry(state.balloons, username) != state.balloons.end()) {
            return RaidShieldResult{RaidShieldStatus::has_balloon, score};
        }
        if (const auto balloon = find_entry(state.shields, username); balloon != state.shields.end()) {
            if (balloon->second > now) {
                return RaidShieldResult{RaidShieldStatus::has_balloon, score};
            }
            state.shields.erase(username);
        }
        if (find_entry(state.boosts, username) != state.boosts.end()) {
            return RaidShieldResult{RaidShieldStatus::has_boost, score};
        }
        if (score < cost) {
            return RaidShieldResult{RaidShieldStatus::insufficient_score, score};
        }
        state.scores[username] = score - cost;
        state.raid_shields[username] = 1;
        return RaidShieldResult{RaidShieldStatus::bought, score - cost};
    });
    if (result.status == RaidShieldStatus::bought) {
        log_info("raid shield bought user={} cost={} left={}", username, cost, result.available_score);
    }
    return result;
}

InvestmentResult investment_deposit(Storage &storage, const std::string &player,
                                    std::string_view target, RaidTargetKind platform,
                                    std::int64_t amount, std::int64_t now) {
    return storage.transaction([&](StorageSession &session) {
        ConquisterState &state = session.state();
        InvestmentResult result;
        if (player_by_name(state, target, platform) != player) {
            result.status = InvestmentStatus::not_self;
        } else if (!on_own_planet(state, player)) {
            result.status = InvestmentStatus::not_home;
        } else if (amount <= 0) {
            result.status = InvestmentStatus::invalid_amount;
        } else {
            const std::int64_t score = counter(state.scores, player);
            if (amount > score) {
                result.status = InvestmentStatus::insufficient_score;
                result.score = score;
            } else {
                state.scores[player] = score - amount;
                state.investments.push_back({player, amount, now});
                result.status = InvestmentStatus::deposited;
                result.amount = amount;
                result.score = score - amount;
            }
        }
        return result;
    });
}

InvestmentResult investment_withdraw(Storage &storage, const std::string &player,
                                     std::string_view target, RaidTargetKind platform,
                                     std::int64_t now) {
    return storage.transaction([&](StorageSession &session) {
        ConquisterState &state = session.state();
        InvestmentResult result;
        if (player_by_name(state, target, platform) != player) {
            result.status = InvestmentStatus::not_self;
            return result;
        }
        if (!on_own_planet(state, player)) {
            result.status = InvestmentStatus::not_home;
            return result;
        }
        long double total = 0;
        std::int64_t principal = 0;
        bool found = false;
        for (const InvestmentDeposit &deposit : state.investments) {
            if (deposit.player != player) {
                continue;
            }
            found = true;
            const long double elapsed = now > deposit.since
                ? static_cast<long double>(now) - static_cast<long double>(deposit.since) : 0;
            total += static_cast<long double>(deposit.amount) * std::pow(1.03L, elapsed / 86400.0L);
            if (total >= static_cast<long double>(std::numeric_limits<std::int64_t>::max()) ||
                !std::isfinite(total)) {
                result.status = InvestmentStatus::balance_limit;
                return result;
            }
            principal += deposit.amount;
        }
        if (!found) {
            result.status = InvestmentStatus::no_investment;
            return result;
        }
        const std::int64_t score = counter(state.scores, player);
        if (total > static_cast<long double>(std::numeric_limits<std::int64_t>::max() - score)) {
            result.status = InvestmentStatus::balance_limit;
            return result;
        }
        const std::int64_t payout = static_cast<std::int64_t>(std::floor(std::nextafter(
            total, std::numeric_limits<long double>::infinity())));
        state.scores[player] = score + payout;
        std::erase_if(state.investments, [&player](const InvestmentDeposit &deposit) {
            return deposit.player == player;
        });
        result.status = InvestmentStatus::withdrawn;
        result.amount = payout;
        result.interest = payout - principal;
        result.score = score + payout;
        return result;
    });
}

RaidResult raid_start(
    Storage &storage,
    std::int64_t user_id,
    const std::string &username,
    std::string_view target,
    std::int64_t now,
    const RaidRules &rules,
    RaidTargetKind target_kind
) {
    const RaidResult result = storage.transaction([&](StorageSession &session) {
        ConquisterState &state = session.state();
        remember_platform(state, username, user_id);
        RaidResult outcome;
        const bool self_on_requested_platform = target_kind == RaidTargetKind::any ||
            (target_kind == RaidTargetKind::telegram ? user_id != 0 : user_id == 0);
        const bool homewards = self_on_requested_platform &&
            text::equals_ignore_case(display_name(state, username), target);
        const std::optional<std::string> known = homewards ? std::optional<std::string>{username}
            : player_by_name(state, target, target_kind);
        if (!known) {
            outcome.status = RaidStatus::unknown_target;
            return outcome;
        }
        const bool holds_place = state.current && text::equals_ignore_case(state.current->username, username);
        Raid *travelling = raid_of(state, username);
        /* Naming yourself is the way home. */
        if (homewards) {
            if (travelling != nullptr) {
                /* He turns his back on the raid and rides home the way he came: what is left is
                   the road already walked, not the one he had planned. Both legs are the same
                   length, so the departure was as far before the arrival as the arrival is
                   before the return. */
                travelling->arrived = true;
                const std::int64_t leg = travelling->back - travelling->arrive;
                const std::int64_t left = now < travelling->arrive
                    ? std::max<std::int64_t>(now - (travelling->arrive - leg), 0)
                    : std::max<std::int64_t>(travelling->back - now, 0);
                travelling->back = now + left;
                outcome.status = RaidStatus::coming_home;
                outcome.seconds = left;
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
        outcome.target = display_name(state, *known);
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
                RaidEvent event;
                event.kind = RaidEvent::Kind::stolen;
                event.raider = display_name(state, raid.raider);
                event.target = display_name(state, raid.target);
                event.seconds = std::max<std::int64_t>(raid.back - now, 0);
                event.target_on_telegram = counter(state.telegram_ids, raid.target) != 0;
                event.raider_emoji = furniture_of(state, raid.raider);
                event.target_emoji = furniture_of(state, raid.target);
                const bool on_home_planet = on_own_planet(state, raid.target);
                event.undefended = !on_home_planet;
                const auto shield = find_entry(state.shields, raid.target);
                const auto balloon = find_entry(state.balloons, raid.target);
                if (on_home_planet && shield != state.shields.end() && shield->second > now) {
                    event.kind = RaidEvent::Kind::defended;
                    event.cost = charge_attacker(state, raid.raider, rules.attack_cost);
                } else if (on_home_planet && balloon != state.balloons.end()) {
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
                    /* A palla for every unit of road walked to get there: neighbours take
                       little, whoever comes from far away pays for the journey. */
                    const position::Point from = position::coordinates_of(player_id(session, state, raid.raider));
                    const position::Point to = position::coordinates_of(player_id(session, state, raid.target));
                    event.distance = position::distance(from, to);
                    event.raider_percent = zodiac::percent_for(event.raider, now, rules.signs);
                    event.target_percent = zodiac::percent_for(event.target, now, rules.signs);
                    const std::int64_t walked =
                        rules.loot_divisor > 0 ? event.distance / rules.loot_divisor : event.distance;
                    const std::int64_t carried = walked * event.raider_percent / event.target_percent;
                    /* The road says what could be taken, the ceiling what may be: no single
                       raid leaves anybody at nothing. */
                    const std::int64_t most =
                        rules.loot_share > 0 ? theirs / rules.loot_share : theirs;
                    event.loot = std::min({theirs, carried, most});
                    if (event.loot > 0) {
                        if (on_home_planet) {
                            if (find_entry(state.raid_shields, raid.target) != state.raid_shields.end()) {
                                const std::int64_t potential = event.loot;
                                event.loot = shielded_loot(potential);
                                event.shield_absorbed = potential - event.loot;
                            }
                        }
                        const std::int64_t exposed = event.loot;
                        event.loot = planet_resisted_loot(state, raid.target, exposed, now);
                        event.resistance_absorbed = exposed - event.loot;
                        state.scores[raid.target] = theirs - event.loot;
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
                settled.push_back(RaidEvent{
                    .kind = RaidEvent::Kind::returned,
                    .raider = display_name(state, raid.raider),
                    .target = display_name(state, raid.target),
                    .loot = raid.loot,
                    .raider_emoji = furniture_of(state, raid.raider),
                    .target_emoji = furniture_of(state, raid.target),
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
                "raid stolen user={} target={} loot={} resistance={} undefended={} balloon_popped={} percent={}/{}",
                event.raider,
                event.target,
                event.loot,
                event.resistance_absorbed,
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
        /* A boost bought during a hold would multiply even the time before its purchase. */
        if (state.current && text::equals_ignore_case(state.current->username, username)) {
            return BoostResult{BoostStatus::holding_place, score};
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
        state.raid_shields.erase(username);
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

QuoteAddResult quote_add(
    Storage &storage,
    const std::string &username,
    const std::string &quote,
    int cost
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
        const ConquisterState &state = session.state();
        const Authors &authors = state.quote_authors;
        std::ranges::transform(page.items, std::back_inserter(page.authors), [&authors, &state](const std::string &quote) {
            const auto found = authors.find(quote);
            return found != authors.end() ? display_name(state, found->second) : std::string{};
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
