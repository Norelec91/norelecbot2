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

enum class BalloonRoll : std::uint8_t { none, held, popped };

/* Everybody always has a balloon. The file keeps how many attempts it has survived; one that pops is
   replaced by a fresh one at once, and a fresh one has no entry. */
BalloonRoll balloon_attempt(StorageSession &session, ConquisterState &state, const std::string &player) {
    const std::int64_t attempt = counter(state.balloons, player) + 1;
    if (static_cast<std::int64_t>(session.random_index(balloon_attempts)) < attempt) {
        state.balloons.erase(player);
        return BalloonRoll::popped;
    }
    state.balloons[player] = attempt;
    return BalloonRoll::held;
}

/* The chance, in percent, that the next attempt pops the player's balloon. */
int balloon_pop_chance(const ConquisterState &state, const std::string &player) {
    return static_cast<int>((counter(state.balloons, player) + 1) * 100 /
                            static_cast<std::int64_t>(balloon_attempts));
}

/* What a hold was worth: the seconds it lasted, times the ⚡ he came in with, times the house of the day. */
struct Settlement {
    std::int64_t earned = 0;
    std::int64_t lightning = 0;
    int zodiac_percent = 100;
};

Settlement settle_hold(ConquisterState &state, const Holder &hold, std::int64_t now, zodiac::Overrides signs) {
    const std::string &holder = hold.username;
    Settlement settled;
    settled.earned = now > hold.since ? now - hold.since : 0;
    if (hold.lightning_percent > 100) {
        settled.lightning = hold.lightning_percent;
        if (settled.earned > std::numeric_limits<std::int64_t>::max() / settled.lightning) {
            log_error("Could not multiply the Conquister score");
            throw StorageError("Conquister score overflow");
        }
        settled.earned = settled.earned * settled.lightning / 100;
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

/* Takes the holder out of @TheConquister37, paying what the hold earned. His balloon is his, not the
   place's: it comes home with him. */
Settlement leave_place(ConquisterState &state, std::int64_t now, zodiac::Overrides signs) {
    if (!state.current) {
        return {};
    }
    const Holder holder = *state.current;
    state.current.reset();
    return settle_hold(state, holder, now, signs);
}

/* From @TheConquister37 a line meant for home takes him there first; anywhere else nothing happens. */
Departure go_home(ConquisterState &state, const std::string &player, std::int64_t now, zodiac::Overrides signs) {
    if (!state.current || !text::equals_ignore_case(state.current->username, player)) {
        return {};
    }
    const Settlement settled = leave_place(state, now, signs);
    log_info("left the place user={} earned={}", player, settled.earned);
    return {.left = true, .earned = settled.earned, .lightning = settled.lightning,
            .zodiac_percent = settled.zodiac_percent};
}

/* A raid takes the player away from home until the return trip ends. */
bool is_away(const ConquisterState &state, const std::string &username) {
    return std::ranges::any_of(state.raids, [&username](const Raid &raid) {
        return raid.raider == username;
    });
}

/* Occupying @TheConquister37 is not the same as being home. */
bool at_home(const ConquisterState &state, const std::string &username) {
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
                                     &state.balloons, &state.cooldowns}) {
        if (const Counters::value_type *found = find_ignore_case(*counters, name); found != nullptr) {
            return found->first;
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

/* How many ⚡ hang on a name, drawn in colour or not. */
std::int64_t count_bolts(std::string_view stored) {
    return std::ranges::count_if(furniture_slots(stored), [](const std::string &slot) {
        return slot == "⚡" || slot == "⚡\xEF\xB8\x8F";
    });
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
                                   &state.cooldowns, &state.ids, &state.debugging};
            return std::ranges::any_of(items, [&key](const Counters *entries) {
                return entries->find(key) != entries->end();
            }) || (state.current && state.current->username == key) ||
                state.furniture.find(key) != state.furniture.end() ||
                std::ranges::any_of(state.raids, [&key](const Raid &raid) {
                    return raid.raider == key || raid.target == key;
                }) ||
                std::ranges::any_of(state.quote_authors, [&key](const Authors::value_type &entry) {
                    return entry.second == key;
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
        state.display_names.erase(secondary);
        if (!telegram_display.empty()) {
            state.display_names[primary] = telegram_display;
        }
        state.link_requests.erase(mine);
        state.link_requests.erase(theirs);
        return LinkStatus::linked;
    });
}

std::optional<std::int64_t> returning_in(Storage &storage, const std::string &player, std::int64_t now) {
    return storage.transaction([&](StorageSession &session) -> std::optional<std::int64_t> {
        const Raid *trip = raid_of(session.state(), player);
        if (trip == nullptr || !trip->arrived) {
            return std::nullopt;
        }
        return std::max<std::int64_t>(trip->back - now, 0);
    });
}

std::vector<Refund> take_bank_refunds(Storage &storage) {
    std::vector<Refund> refunds = storage.transaction([](StorageSession &session) {
        ConquisterState &state = session.state();
        std::vector<Refund> taken;
        for (const auto &[key, amount] : state.bank_refunds) {
            taken.push_back({.name = display_name(state, key),
                             .on_telegram = counter(state.telegram_ids, key) != 0,
                             .amount = amount});
        }
        state.bank_refunds.clear();
        return taken;
    });
    std::ranges::sort(refunds, std::ranges::greater{}, &Refund::amount);
    return refunds;
}

bool names_player(Storage &storage, const std::string &player, std::string_view name, RaidTargetKind platform) {
    return storage.transaction([&](StorageSession &session) {
        return player_by_name(session.state(), name, platform) == player;
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
            const BalloonRoll balloon = balloon_attempt(session, state, holder);
            if (balloon == BalloonRoll::held) {
                if (rules.cooldown_seconds > 0) {
                    state.cooldowns[username] = now + rules.cooldown_seconds;
                    outcome.penalty_seconds = rules.cooldown_seconds;
                }
                outcome.status = ClaimStatus::defended;
                outcome.previous_username = display_name(state, holder);
                outcome.previous_key = holder;
                outcome.next_chance = balloon_pop_chance(state, holder);
                return outcome;
            }
            outcome.balloon_popped = balloon == BalloonRoll::popped;
            /* Kicked out, he gets a fresh balloon. */
            state.balloons.erase(holder);
            outcome.previous_username = display_name(state, holder);
            outcome.previous_key = holder;
            const Settlement settled = settle_hold(state, *state.current, now, rules.signs);
            outcome.earned = settled.earned;
            outcome.lightning = settled.lightning;
            outcome.zodiac_percent = settled.zodiac_percent;
        }
        /* The ⚡ on his name as he comes in fix what this hold is worth, each one adding its share:
           nothing hung or burnt later can change it. He brings his own balloon, as worn as it is. */
        const std::int64_t bolts = count_bolts(furniture_of(state, username));
        outcome.entered_lightning = bolts > 0 && rules.lightning > 0 ? 100 + bolts * rules.lightning : 0;
        state.current = Holder{.user_id = user_id, .username = username, .since = now,
                               .lightning_percent = outcome.entered_lightning};
        return outcome;
    });

    switch (result.status) {
    case ClaimStatus::taken:
        log_info(
            "claim taken user={} previous={} earned={} balloon_popped={} lightning_percent={}",
            username,
            result.previous_username,
            result.earned,
            result.balloon_popped ? 1 : 0,
            result.lightning
        );
        break;
    case ClaimStatus::defended:
        log_info(
            "claim defended user={} holder={} next_chance={} penalty={}",
            username,
            result.previous_username,
            result.next_chance,
            result.penalty_seconds
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

namespace {

Profile profile_from(ConquisterState &state, const std::string &key, std::int64_t now) {
    Profile profile;
    profile.name = display_name(state, key);
    profile.furniture = furniture_of(state, key);
    profile.on_telegram = counter(state.telegram_ids, key) != 0;
    profile.players = state.scores.size();
    if (const Counters::value_type *score = find_ignore_case(state.scores, key); score != nullptr) {
        profile.score = score->second;
        profile.rank = static_cast<std::size_t>(std::ranges::count_if(state.scores,
            [score](const Counters::value_type &other) { return ranks_before(other, *score); })) + 1;
    }
    profile.quotes_added = counter(state.quotes_added, key);
    if (state.current && state.current->username == key) {
        profile.place = ProfilePlace::conquister;
        profile.since = state.current->since;
        profile.lightning_percent = state.current->lightning_percent;
    } else if (const Raid *trip = raid_of(state, key); trip != nullptr) {
        profile.place = ProfilePlace::road;
        profile.heading = display_name(state, trip->target);
        profile.returning = trip->arrived;
        profile.home_in = std::max<std::int64_t>(trip->back - now, 0);
    }
    return profile;
}

}

std::optional<Profile> player_profile(Storage &storage, std::string_view name, RaidTargetKind platform,
                                      std::int64_t now) {
    return storage.transaction([&](StorageSession &session) -> std::optional<Profile> {
        ConquisterState &state = session.state();
        const std::optional<std::string> key = player_by_name(state, name, platform);
        if (!key) {
            return std::nullopt;
        }
        return profile_from(state, *key, now);
    });
}

Profile player_profile_of(Storage &storage, const std::string &key, std::int64_t now) {
    return storage.transaction([&](StorageSession &session) {
        return profile_from(session.state(), key, now);
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

namespace {

constexpr std::string_view empty_slot = "[]";

/* The same emoji whether or not it asks to be drawn in colour: ❤ and ❤️ are one heart. */
std::string without_variation(std::string_view emoji) {
    std::string plain;
    for (std::size_t at = 0; at < emoji.size();) {
        const std::string_view rest = emoji.substr(at);
        if (rest.starts_with("\xEF\xB8\x8F") || rest.starts_with("\xEF\xB8\x8E")) {
            at += 3;
            continue;
        }
        plain += emoji[at];
        ++at;
    }
    return plain;
}

/* The emoji a player has on the road, empty when he carries none. */
std::string emoji_travelling(const ConquisterState &state, const std::string &player) {
    const auto trip = std::ranges::find_if(state.raids, [&player](const Raid &raid) {
        return raid.raider == player && !raid.gift_emoji.empty();
    });
    return trip == state.raids.end() ? std::string{} : trip->gift_emoji;
}

/* How many of a name's slots, up to the limit, have nothing hanging in them. */
std::size_t empty_slots(const std::vector<std::string> &slots, std::size_t limit) {
    const std::size_t used = std::min(slots.size(), limit);
    const auto holes = std::ranges::count_if(slots.begin(), slots.begin() + static_cast<std::ptrdiff_t>(used),
                                             [](const std::string &slot) { return slot.empty(); });
    return static_cast<std::size_t>(holes) + (limit - used);
}

/* The price grown by the inflation percent once for every copy, stopping at the largest number
   rather than wrapping. */
std::int64_t inflated(std::int64_t cost, std::size_t copies, std::int64_t inflation) {
    if (cost <= 0) {
        return 0;
    }
    constexpr auto most = std::numeric_limits<std::int64_t>::max();
    const std::int64_t factor = 100 + std::max<std::int64_t>(inflation, 0);
    std::int64_t price = cost;
    for (std::size_t copy = 0; copy < copies; ++copy) {
        if (price > most / factor) {
            return most;
        }
        price = price * factor / 100;
    }
    return price;
}

}

std::vector<std::string> furniture_slots(std::string_view stored) {
    std::vector<std::string> slots;
    while (!stored.empty()) {
        if (stored.starts_with(empty_slot)) {
            slots.emplace_back();
            stored.remove_prefix(empty_slot.size());
            continue;
        }
        const std::size_t end = std::min(stored.find(empty_slot), stored.size());
        const std::string_view piece = stored.substr(0, end);
        if (const auto emoji = text::emoji_split(piece)) {
            slots.insert(slots.end(), emoji->begin(), emoji->end());
        } else {
            /* Whatever an older version saved stays where it was, as one slot. */
            slots.emplace_back(piece);
        }
        stored.remove_prefix(end);
    }
    return slots;
}

std::string furniture_stored(std::vector<std::string> slots) {
    while (!slots.empty() && slots.back().empty()) {
        slots.pop_back();
    }
    std::string stored;
    for (const std::string &slot : slots) {
        stored += slot.empty() ? std::string{empty_slot} : slot;
    }
    return stored;
}

namespace {

bool same_emoji(std::string_view one, std::string_view other) {
    return without_variation(one) == without_variation(other);
}

bool is_poo(std::string_view emoji) {
    return same_emoji(emoji, "💩");
}

std::vector<std::string> slots_of(const ConquisterState &state, const std::string &player) {
    return furniture_slots(furniture_of(state, player));
}

/* Writes a name's slots back; a name left with nothing loses its entry. */
void hang(ConquisterState &state, const std::string &player, std::vector<std::string> slots) {
    std::string stored = furniture_stored(std::move(slots));
    const auto mine = find_entry(state.furniture, player);
    if (stored.empty()) {
        if (mine != state.furniture.end()) {
            const std::string key = mine->first;
            state.furniture.erase(key);
        }
    } else if (mine != state.furniture.end()) {
        mine->second = std::move(stored);
    } else {
        state.furniture[player] = std::move(stored);
    }
}

/* The first empty slot on a name, or nothing when every one of them is taken. */
std::optional<std::size_t> free_slot(const std::vector<std::string> &slots, std::size_t limit) {
    const auto empty = std::ranges::find_if(slots, [](const std::string &slot) { return slot.empty(); });
    const auto index = static_cast<std::size_t>(empty - slots.begin());
    return index < limit ? std::optional<std::size_t>{index} : std::nullopt;
}

/* Whether a name can take one more emoji and still keep a slot for its own one on the road. */
bool has_room(const ConquisterState &state, const std::string &player, std::size_t limit) {
    const std::size_t needed = emoji_travelling(state, player).empty() ? 1 : 2;
    return empty_slots(slots_of(state, player), limit) >= needed;
}

bool has_emoji(const ConquisterState &state, const std::string &player, std::string_view emoji) {
    return std::ranges::any_of(slots_of(state, player), [emoji](const std::string &slot) {
        return !slot.empty() && same_emoji(slot, emoji);
    });
}

/* Takes the first copy of an emoji off a name, leaving a hole where it hung. */
bool take_emoji(ConquisterState &state, const std::string &player, std::string_view emoji) {
    std::vector<std::string> slots = slots_of(state, player);
    const auto found = std::ranges::find_if(slots, [emoji](const std::string &slot) {
        return !slot.empty() && same_emoji(slot, emoji);
    });
    if (found == slots.end()) {
        return false;
    }
    found->clear();
    hang(state, player, std::move(slots));
    return true;
}

/* Hangs an emoji in the first empty slot of a name; false when the name is full. */
bool give_emoji(ConquisterState &state, const std::string &player, const std::string &emoji, std::size_t limit) {
    std::vector<std::string> slots = slots_of(state, player);
    const std::optional<std::size_t> slot = free_slot(slots, limit);
    if (!slot) {
        return false;
    }
    if (*slot >= slots.size()) {
        slots.resize(*slot + 1);
    }
    slots[*slot] = emoji;
    hang(state, player, std::move(slots));
    return true;
}

}

FurnitureMoveResult furniture_move(Storage &storage, const std::string &username,
                                   std::int64_t from, std::int64_t to, std::size_t limit,
                                   std::int64_t now, zodiac::Overrides signs) {
    const FurnitureMoveResult result = storage.transaction([&](StorageSession &session) {
        ConquisterState &state = session.state();
        FurnitureMoveResult outcome;
        std::vector<std::string> slots = slots_of(state, username);
        outcome.shown = furniture_stored(slots);
        const auto valid = [limit](std::int64_t slot) {
            return slot >= 1 && static_cast<std::uint64_t>(slot) <= limit;
        };
        if (!valid(from) || !valid(to)) {
            outcome.status = FurnitureMoveStatus::invalid_position;
            return outcome;
        }
        if (from == to) {
            outcome.status = FurnitureMoveStatus::same_position;
            return outcome;
        }
        outcome.departure = go_home(state, username, now, signs);
        if (!at_home(state, username)) {
            outcome.status = FurnitureMoveStatus::not_home;
            return outcome;
        }
        const auto source = static_cast<std::size_t>(from - 1);
        const auto target = static_cast<std::size_t>(to - 1);
        if (source >= slots.size() || slots[source].empty()) {
            outcome.status = FurnitureMoveStatus::empty_slot;
            return outcome;
        }
        slots.resize(std::max(slots.size(), target + 1));
        outcome.moved = slots[source];
        outcome.swapped = slots[target];
        std::swap(slots[source], slots[target]);
        outcome.status = outcome.swapped.empty() ? FurnitureMoveStatus::moved : FurnitureMoveStatus::swapped;
        hang(state, username, slots);
        outcome.shown = furniture_stored(std::move(slots));
        return outcome;
    });
    log_info("furniture move user={} from={} to={} status={}", username, from, to, static_cast<int>(result.status));
    return result;
}

FurnitureBurnResult furniture_burn(Storage &storage, const std::string &player, const std::string &emoji) {
    const FurnitureBurnResult result = storage.transaction([&](StorageSession &session) {
        ConquisterState &state = session.state();
        if (is_away(state, player)) {
            return FurnitureBurnResult{.status = FurnitureBurnStatus::travelling, .shown = {}};
        }
        if (!take_emoji(state, player, emoji)) {
            return FurnitureBurnResult{.status = FurnitureBurnStatus::not_owned, .shown = {}};
        }
        return FurnitureBurnResult{.status = FurnitureBurnStatus::burned, .shown = furniture_of(state, player)};
    });
    if (result.status == FurnitureBurnStatus::burned) {
        log_info("emoji burned user={} emoji={}", player, emoji);
    }
    return result;
}

FurnitureResult furniture_buy(
    Storage &storage,
    const std::string &username,
    const std::string &emoji,
    std::int64_t position,
    std::int64_t cost,
    std::size_t limit,
    std::int64_t now,
    zodiac::Overrides signs,
    std::int64_t inflation
) {
    const FurnitureResult result = storage.transaction([&](StorageSession &session) {
        ConquisterState &state = session.state();
        FurnitureResult outcome;
        outcome.departure = go_home(state, username, now, signs);
        outcome.available_score = counter(state.scores, username);
        if (!at_home(state, username)) {
            outcome.status = FurnitureStatus::not_home;
            return outcome;
        }
        const auto mine = find_entry(state.furniture, username);
        std::vector<std::string> slots =
            furniture_slots(mine == state.furniture.end() ? std::string_view{} : std::string_view{mine->second});
        outcome.shown = furniture_stored(slots);
        std::size_t index = 0;
        if (position == 0) {
            const auto empty = std::ranges::find_if(slots, [](const std::string &slot) { return slot.empty(); });
            index = static_cast<std::size_t>(empty - slots.begin());
            if (index >= limit) {
                outcome.status = FurnitureStatus::full;
                return outcome;
            }
        } else if (position < 0 || static_cast<std::uint64_t>(position) > limit) {
            outcome.status = FurnitureStatus::invalid_position;
            return outcome;
        } else {
            index = static_cast<std::size_t>(position - 1);
        }
        outcome.position = index + 1;
        const std::string wanted = without_variation(emoji);
        if (index < slots.size() && without_variation(slots[index]) == wanted) {
            outcome.status = FurnitureStatus::already_there;
            outcome.replaced = slots[index];
            return outcome;
        }
        for (const Authors::value_type &hung : state.furniture) {
            outcome.copies += static_cast<std::size_t>(std::ranges::count_if(
                furniture_slots(hung.second), [&wanted](const std::string &slot) {
                    return !slot.empty() && without_variation(slot) == wanted;
                }));
        }
        /* One on its way to somebody is still in the game: a delivery does not make it cheaper. */
        outcome.copies += static_cast<std::size_t>(std::ranges::count_if(state.raids, [&wanted](const Raid &raid) {
            return !raid.gift_emoji.empty() && without_variation(raid.gift_emoji) == wanted;
        }));
        outcome.charged = inflated(cost, outcome.copies, inflation);
        if (outcome.available_score < outcome.charged) {
            outcome.status = FurnitureStatus::insufficient_score;
            return outcome;
        }
        if (index >= slots.size()) {
            slots.resize(index + 1);
        }
        outcome.replaced = slots[index];
        slots[index] = emoji;
        outcome.shown = furniture_stored(slots);
        outcome.available_score -= outcome.charged;
        state.scores[username] = outcome.available_score;
        hang(state, username, std::move(slots));
        outcome.status = FurnitureStatus::bought;
        return outcome;
    });

    log_info(
        "furniture user={} status={} position={} copies={} charged={}",
        username,
        static_cast<int>(result.status),
        result.position,
        result.copies,
        result.charged
    );
    return result;
}

Authors furniture_all(Storage &storage) {
    return storage.transaction([](StorageSession &session) { return session.state().furniture; });
}

BurnResult palle_burn(Storage &storage, const std::string &player, std::int64_t amount) {
    const BurnResult result = storage.transaction([&](StorageSession &session) {
        ConquisterState &state = session.state();
        BurnResult outcome;
        const std::int64_t score = counter(state.scores, player);
        outcome.score = score;
        if (is_away(state, player)) {
            outcome.status = BurnStatus::travelling;
            return outcome;
        }
        if (amount <= 0) {
            outcome.status = BurnStatus::invalid_amount;
            return outcome;
        }
        if (amount > score) {
            outcome.status = BurnStatus::insufficient_score;
            return outcome;
        }
        state.scores[player] = score - amount;
        outcome.status = BurnStatus::burned;
        outcome.amount = amount;
        outcome.score = score - amount;
        return outcome;
    });
    if (result.status == BurnStatus::burned) {
        log_info("palle burned user={} amount={} left={}", player, result.amount, result.score);
    }
    return result;
}

RaidResult raid_start(
    Storage &storage,
    std::int64_t user_id,
    const std::string &username,
    std::string_view target,
    std::int64_t now,
    const RaidRules &rules,
    RaidTargetKind target_kind,
    std::int64_t gift,
    std::string_view gift_emoji
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
                const Settlement settled = leave_place(state, now, rules.signs);
                outcome.status = RaidStatus::left_place;
                outcome.earned = settled.earned;
                outcome.lightning = settled.lightning;
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
        /* Palle taken along leave home with him, so nothing can be robbed from them on the way. */
        if (gift != 0) {
            const std::int64_t score = counter(state.scores, username);
            if (gift < 0) {
                outcome.status = RaidStatus::invalid_amount;
                return outcome;
            }
            if (gift > score) {
                outcome.status = RaidStatus::insufficient_score;
                outcome.score = score;
                return outcome;
            }
            state.scores[username] = score - gift;
            outcome.score = score - gift;
        }
        /* An emoji taken along leaves his name as he sets off, if the target has somewhere to hang it. */
        if (!gift_emoji.empty()) {
            if (!has_emoji(state, username, gift_emoji)) {
                outcome.status = RaidStatus::no_such_emoji;
                return outcome;
            }
            /* A pile of poo is thrown, not hung: it needs no room on the target's name. */
            if (!is_poo(gift_emoji) && !has_room(state, *known, rules.furniture_limit)) {
                outcome.status = RaidStatus::no_room;
                return outcome;
            }
            static_cast<void>(take_emoji(state, username, gift_emoji));
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
            .gift = gift,
            .gift_emoji = std::string{gift_emoji},
        });
        return outcome;
    });

    if (result.status == RaidStatus::started) {
        log_info("raid started user={} target={} travel={} gift={} emoji={}", username, result.target,
                 result.seconds, gift, gift_emoji);
    }
    if (result.status == RaidStatus::left_place) {
        log_info("left the place user={} earned={}", username, result.earned);
    }
    if (result.status == RaidStatus::coming_home) {
        log_info("raid called off user={} home_in={}", username, result.seconds);
    }
    return result;
}

namespace {

/* A raid on a player at home meets his balloon first; while he is away it guards nothing. */
bool defended_at_home(StorageSession &session, ConquisterState &state, const std::string &target,
                      RaidEvent &event) {
    if (!at_home(state, target)) {
        return false;
    }
    switch (balloon_attempt(session, state, target)) {
    case BalloonRoll::held:
        event.balloon_held = true;
        event.next_chance = balloon_pop_chance(state, target);
        return true;
    case BalloonRoll::popped:
        event.balloon_popped = true;
        return false;
    case BalloonRoll::none:
        break;
    }
    return false;
}

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
                event.raider_on_telegram = counter(state.telegram_ids, raid.raider) != 0;
                event.raider_emoji = furniture_of(state, raid.raider);
                event.target_emoji = furniture_of(state, raid.target);
                /* Whoever came to give hands the palle or the emoji over and robs nothing. */
                if (raid.gift > 0 || !raid.gift_emoji.empty()) {
                    event.kind = RaidEvent::Kind::delivered;
                    event.gift = raid.gift;
                    if (raid.gift > 0) {
                        state.scores[raid.target] = counter(state.scores, raid.target) + raid.gift;
                        raid.gift = 0;
                    }
                    if (!raid.gift_emoji.empty()) {
                        event.gift_emoji = raid.gift_emoji;
                        /* Poo splatters on arrival and is gone; any other emoji is hung, and a name
                           that filled up meanwhile sends it back the way it came. */
                        if (is_poo(raid.gift_emoji)) {
                            raid.gift_emoji.clear();
                        } else if (has_room(state, raid.target, rules.furniture_limit) &&
                            give_emoji(state, raid.target, raid.gift_emoji, rules.furniture_limit)) {
                            raid.gift_emoji.clear();
                            event.target_emoji = furniture_of(state, raid.target);
                        } else {
                            event.no_room = true;
                        }
                    }
                } else if (defended_at_home(session, state, raid.target, event)) {
                    raid.loot = 0;
                } else {
                    event.undefended = !at_home(state, raid.target);
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
                    /* The road says what can be taken, and nobody loses more than he has. */
                    event.loot = std::min(theirs, carried);
                    if (event.loot > 0) {
                        state.scores[raid.target] = theirs - event.loot;
                    }
                    raid.loot = event.loot;
                }
                settled.push_back(std::move(event));
            }
            if (raid.arrived && now >= raid.back) {
                const std::int64_t carried = counter(state.scores, raid.raider);
                /* The loot, plus the palle nobody received because he turned back. */
                const std::int64_t brought = raid.loot + raid.gift;
                if (brought > 0) {
                    state.scores[raid.raider] = carried + brought;
                }
                /* An emoji nobody took goes back on his name: its slot was kept free while it travelled. */
                if (!raid.gift_emoji.empty()) {
                    static_cast<void>(give_emoji(state, raid.raider, raid.gift_emoji,
                                                 std::numeric_limits<std::size_t>::max()));
                }
                settled.push_back(RaidEvent{
                    .kind = RaidEvent::Kind::returned,
                    .raider = display_name(state, raid.raider),
                    .target = display_name(state, raid.target),
                    .loot = raid.loot,
                    .gift = raid.gift,
                    .gift_emoji = raid.gift_emoji,
                    .raider_emoji = furniture_of(state, raid.raider),
                    .target_emoji = furniture_of(state, raid.target),
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
                "raid {} user={} target={} loot={} undefended={} percent={}/{}",
                event.balloon_held ? "held off by the balloon" :
                    event.balloon_popped ? "stolen past a popped balloon" : "stolen",
                event.raider,
                event.target,
                event.loot,
                event.undefended ? 1 : 0,
                event.raider_percent,
                event.target_percent
            );
            break;
        case RaidEvent::Kind::delivered:
            log_info("raid delivered user={} target={} gift={} emoji={} no_room={}", event.raider, event.target,
                     event.gift, event.gift_emoji, event.no_room ? 1 : 0);
            break;
        case RaidEvent::Kind::returned:
            log_info("raid returned user={} target={} loot={} gift={} emoji={}", event.raider,
                     event.target, event.loot, event.gift, event.gift_emoji);
            break;
        }
    }
    return events;
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
