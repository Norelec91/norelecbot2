#include "game.hpp"

#include "logging.hpp"
#include "position.hpp"
#include "text.hpp"
#include "zodiac.hpp"

#include <algorithm>
#include <array>
#include <cctype>
#include <cmath>
#include <format>
#include <iterator>
#include <limits>
#include <map>
#include <ranges>
#include <utility>

namespace norelecbot {
namespace {

constexpr std::size_t quotes_page_size = 30;
std::string display_name(const ConquisterState &state, const std::string &key);
/* What hangs beside a name as the group sees it: the holder's 🦞 show what they came in as. */
std::string shown_furniture(const ConquisterState &state, const std::string &player);
/* Where a player is right now: the one question every rule about home and away asks. */
Whereabouts whereabouts(const ConquisterState &state, const std::string &player);
/* What a thrown emoji did where it landed. */
struct Landing {
    bool smeared = false;
    /* A 💣 that was a dud went off in the thrower's hand: what is blown is his, not the victim's. */
    bool backfired = false;
    /* A ☢️ made the victim start over. */
    bool reset = false;
    /* A 🌀 flung the victim far away. */
    bool flung = false;
    std::vector<std::string> blown;
};
/* A thrown emoji lands where somebody is, his house or @TheConquister37, and works on what is there. */
Landing land(StorageSession &session, ConquisterState &state, std::string_view thrown, const std::string &thrower,
             const std::string &victim, Whereabouts site, std::int64_t now, const RaidRules &rules);
/* Hit by a 💩 and not clean yet. */
bool is_smeared(const ConquisterState &state, const std::string &player, std::int64_t now);
/* Flung far away by a 🌀. */
bool is_flung(const ConquisterState &state, const std::string &player);
/* Where the emoji with a power in a slot is. */
Whereabouts site_of(const ConquisterState &state, const std::string &player, std::size_t slot, const Power &power);
/* Hands a player the 🎈 everybody starts with, once, if he has none and a free slot. */
void welcome(ConquisterState &state, const std::string &player, std::size_t limit);
bool has_balloon(const ConquisterState &state, const std::string &player);
bool hang_balloon(ConquisterState &state, const std::string &player, std::size_t limit);
/* How many copies of a power's emoji hang on a name as the group sees it. */
std::int64_t copies_of(const ConquisterState &state, const std::string &player, const Power &power);
/* The copies he has with him: one hung at his house while he was out stayed there. */
std::int64_t carried_copies(const ConquisterState &state, const std::string &player, const Power &power);
bool stayed_home(const ConquisterState &state, const std::string &player, std::size_t slot);
std::optional<std::size_t> balloon_slot(const ConquisterState &state, const std::string &player, Whereabouts site);
/* For every 🦞 on the claimer's name, the emoji the kicked holder has in that same slot. */
std::map<std::size_t, std::string> lobsters_copying(const ConquisterState &state, const std::string &claimer,
                                                    const std::string &kicked);

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

/* The slot of the 🎈 a player has in a place, the one he is in or his house; nothing without one there. */
std::optional<std::size_t> balloon_slot(const ConquisterState &state, const std::string &player, Whereabouts site) {
    const std::vector<std::string> slots = furniture_slots(shown_furniture(state, player));
    for (std::size_t slot = 0; slot < slots.size(); ++slot) {
        if (is_power(slots[slot], power::balloon) && site_of(state, player, slot, power::balloon) == site) {
            return slot;
        }
    }
    return std::nullopt;
}

/* The 🎈 is an emoji like the others: no 🎈 there, no defence. The file keeps how many attempts his has
   survived; one that pops lets the attempt through and is as good as new at once, still on his name. */
BalloonRoll balloon_attempt(StorageSession &session, ConquisterState &state, const std::string &player,
                            Whereabouts site) {
    const std::optional<std::size_t> slot = balloon_slot(state, player, site);
    if (!slot) {
        return BalloonRoll::none;
    }
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

/* What the hold made since it was last counted, without paying it. */
Settlement hold_value(const ConquisterState &state, const Holder &hold, std::int64_t now, zodiac::Overrides signs) {
    const std::string &holder = hold.username;
    Settlement settled;
    const std::int64_t from = std::max(hold.since, hold.counted_from);
    settled.earned = now > from ? now - from : 0;
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
    return settled;
}

Settlement settle_hold(ConquisterState &state, const Holder &hold, std::int64_t now, zodiac::Overrides signs) {
    Settlement settled = hold_value(state, hold, now, signs);
    /* What was put aside when a ⚡ was lost is paid with the rest. */
    settled.earned += hold.banked;
    std::int64_t &score = state.scores[hold.username];
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

bool is_flung(const ConquisterState &state, const std::string &player) {
    return state.flung.find(player) != state.flung.end();
}

Whereabouts whereabouts(const ConquisterState &state, const std::string &player) {
    if (state.current && text::equals_ignore_case(state.current->username, player)) {
        return Whereabouts::conquister;
    }
    const bool travelling = std::ranges::any_of(state.raids, [&player](const Raid &raid) {
        return raid.raider == player;
    });
    return travelling ? Whereabouts::road : Whereabouts::home;
}

/* Whether what hangs in a slot was put there while its owner was out. */
bool stayed_home(const ConquisterState &state, const std::string &player, std::size_t slot) {
    const auto mine = state.stayed.find(player);
    return mine != state.stayed.end() &&
        std::ranges::find(mine->second, static_cast<std::int64_t>(slot)) != mine->second.end();
}

/* Leaving from home he takes along everything he can carry, whenever it was hung. */
void leave_home(ConquisterState &state, const std::string &player) {
    state.stayed.erase(player);
}

/* From @TheConquister37 a line meant for home takes him there first; anywhere else nothing happens. */
Departure go_home(ConquisterState &state, const std::string &player, std::int64_t now, zodiac::Overrides signs) {
    if (whereabouts(state, player) != Whereabouts::conquister) {
        return {};
    }
    const Settlement settled = leave_place(state, now, signs);
    log_info("left the place user={} earned={}", player, settled.earned);
    return {.left = true, .earned = settled.earned, .lightning = settled.lightning,
            .zodiac_percent = settled.zodiac_percent};
}

/* Occupying @TheConquister37 is not the same as being home, and neither is a raid, which takes the
   player away until the return trip ends. */
bool at_home(const ConquisterState &state, const std::string &username) {
    return whereabouts(state, username) == Whereabouts::home;
}

bool on_the_road(const ConquisterState &state, const std::string &username) {
    return whereabouts(state, username) == Whereabouts::road;
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

}

namespace {

/* Everybody the game has anything on file about. */
std::vector<std::string> known_players(const ConquisterState &state) {
    std::vector<std::string> players;
    const auto add = [&players](const std::string &player) {
        if (std::ranges::find(players, player) == players.end()) {
            players.push_back(player);
        }
    };
    if (state.current && !state.current->username.empty()) {
        add(state.current->username);
    }
    for (const Counters *known : {&state.scores, &state.ids, &state.balloons, &state.telegram_ids,
                                  &state.irc_names}) {
        for (const auto &[player, value] : *known) {
            add(player);
        }
    }
    for (const Authors *known : {&state.furniture, &state.display_names}) {
        for (const auto &[player, value] : *known) {
            add(player);
        }
    }
    return players;
}

}

BalloonPortResult balloon_port(Storage &storage, const std::string &player, std::size_t furniture_limit) {
    const BalloonPortResult result = storage.transaction([&](StorageSession &session) {
        ConquisterState &state = session.state();
        BalloonPortResult outcome;
        if (has_balloon(state, player)) {
            outcome.status = BalloonPortStatus::has_one;
        } else if (counter(state.balloon_ported, player) != 0) {
            outcome.status = BalloonPortStatus::taken_already;
        } else if (!hang_balloon(state, player, furniture_limit)) {
            outcome.status = BalloonPortStatus::full;
        } else {
            state.balloon_ported[player] = 1;
        }
        outcome.shown = shown_furniture(state, player);
        return outcome;
    });
    log_info("balloon port user={} status={}", player, static_cast<int>(result.status));
    return result;
}

void balloons_hand_out(Storage &storage, std::size_t furniture_limit) {
    storage.transaction([furniture_limit](StorageSession &session) {
        ConquisterState &state = session.state();
        for (const std::string &player : known_players(state)) {
            welcome(state, player, furniture_limit);
        }
        return 0;
    });
}

std::string player_seen(Storage &storage, std::int64_t user_id, const std::string &username,
                        std::string_view account_name, std::size_t furniture_limit) {
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
        welcome(state, key, furniture_limit);
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
        if (whereabouts(state, username) == Whereabouts::conquister) {
            outcome.status = ClaimStatus::already_held;
            return outcome;
        }
        if (is_flung(state, username)) {
            outcome.status = ClaimStatus::too_far;
            return outcome;
        }
        if (state.current && !state.current->username.empty()) {
            const std::string holder = state.current->username;
            outcome.previous_user_id = !ambiguous_legacy(state, holder) &&
                counter(state.telegram_ids, holder) != 0
                ? counter(state.telegram_ids, holder) : state.current->user_id;
            /* The 🥷 on his name may take him past the holder's 🎈, which is not even touched. */
            const std::int64_t stealth = std::min<std::int64_t>(
                100, carried_copies(state, username, power::ninja) * std::max<std::int64_t>(rules.ninja, 0));
            outcome.sneaked = stealth > 0 && balloon_slot(state, holder, Whereabouts::conquister).has_value() &&
                static_cast<std::int64_t>(session.random_index(100)) < stealth;
            const BalloonRoll balloon = outcome.sneaked
                ? BalloonRoll::none : balloon_attempt(session, state, holder, Whereabouts::conquister);
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
            /* Kicked out, his 🎈 is fresh again; one he was slipped past is as worn as it was. */
            if (!outcome.sneaked) {
                state.balloons.erase(holder);
            }
            outcome.previous_username = display_name(state, holder);
            outcome.previous_key = holder;
            const Settlement settled = settle_hold(state, *state.current, now, rules.signs);
            outcome.earned = settled.earned;
            outcome.lightning = settled.lightning;
            outcome.zodiac_percent = settled.zodiac_percent;
        }
        /* His 🦞 turn into what the holder he kicks out has in the same slots, and count as that. */
        std::map<std::size_t, std::string> lobsters;
        if (!outcome.previous_key.empty()) {
            lobsters = lobsters_copying(state, username, outcome.previous_key);
        }
        for (const auto &[slot, emoji] : lobsters) {
            outcome.lobsters_became.push_back(emoji);
        }
        leave_home(state, username);
        state.current = Holder{.user_id = user_id, .username = username, .since = now,
                               .lightning_percent = 0, .bolts = 0, .banked = 0, .counted_from = 0,
                               .lobsters = std::move(lobsters)};
        /* The ⚡ on his name as he comes in set what this hold is worth, each one adding its share: one
           hung later does not raise it, one lost inside lowers it from then on. He brings his own
           balloon, as worn as it is. */
        const std::int64_t bolts = copies_of(state, username, power::bolt);
        outcome.entered_lightning = bolts > 0 && rules.lightning > 0 ? 100 + bolts * rules.lightning : 0;
        state.current->lightning_percent = outcome.entered_lightning;
        state.current->bolts = outcome.entered_lightning > 0 ? bolts : 0;
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
    case ClaimStatus::too_far:
        log_info("claim refused user={} flung", username);
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
    profile.furniture = shown_furniture(state, key);
    profile.smeared = is_smeared(state, key, now);
    profile.flung = is_flung(state, key);
    profile.on_telegram = counter(state.telegram_ids, key) != 0;
    profile.players = state.scores.size();
    if (const Counters::value_type *score = find_ignore_case(state.scores, key); score != nullptr) {
        profile.score = score->second;
        profile.rank = static_cast<std::size_t>(std::ranges::count_if(state.scores,
            [score](const Counters::value_type &other) { return ranks_before(other, *score); })) + 1;
    }
    profile.quotes_added = counter(state.quotes_added, key);
    profile.place = whereabouts(state, key);
    if (profile.place == Whereabouts::conquister && state.current) {
        profile.since = state.current->since;
        profile.lightning_percent = state.current->lightning_percent;
    } else if (const Raid *trip = raid_of(state, key); trip != nullptr) {
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

namespace {

/* The same emoji whatever the tone of its skin: 🥷🏿 is a 🥷. */
std::string without_tone(std::string emoji) {
    for (const std::string_view tone : {"🏻", "🏼", "🏽", "🏾", "🏿"}) {
        for (std::size_t at = emoji.find(tone); at != std::string::npos; at = emoji.find(tone, at)) {
            emoji.erase(at, tone.size());
        }
    }
    return emoji;
}

}

bool is_power(std::string_view emoji, const Power &power) {
    return without_tone(without_variation(emoji)) == without_tone(without_variation(power.emoji));
}

const Power *power_of(std::string_view emoji) {
    const auto found = std::ranges::find_if(powers, [emoji](const Power &power) { return is_power(emoji, power); });
    return found == powers.end() ? nullptr : &*found;
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

bool is_lobster(std::string_view emoji) {
    return is_power(emoji, power::lobster);
}

/* Thrown, an emoji is not hung on the target: it needs no room there. */
bool is_thrown(std::string_view emoji) {
    const Power *power = power_of(emoji);
    return power != nullptr && power->kind == PowerKind::thrown;
}

std::int64_t carried_copies(const ConquisterState &state, const std::string &player, const Power &power) {
    const std::vector<std::string> slots = furniture_slots(shown_furniture(state, player));
    std::int64_t copies = 0;
    for (std::size_t slot = 0; slot < slots.size(); ++slot) {
        if (is_power(slots[slot], power) && !stayed_home(state, player, slot)) {
            ++copies;
        }
    }
    return copies;
}

std::int64_t copies_of(const ConquisterState &state, const std::string &player, const Power &power) {
    return std::ranges::count_if(furniture_slots(shown_furniture(state, player)), [&power](const std::string &slot) {
        return !slot.empty() && is_power(slot, power);
    });
}

bool is_smeared(const ConquisterState &state, const std::string &player, std::int64_t now) {
    return counter(state.smeared, player) > now;
}

/* A 💩 that lands makes him "lo smerdato" from now, however long he already was. */
void smear(ConquisterState &state, const std::string &player, std::int64_t now, std::int64_t seconds) {
    if (seconds > 0) {
        state.smeared[player] = now + seconds;
    }
}

/* Whoever is clean again loses his entry. */
void wash(ConquisterState &state, std::int64_t now) {
    std::vector<std::string> clean;
    for (const auto &[player, until] : state.smeared) {
        if (until <= now) {
            clean.push_back(player);
        }
    }
    for (const std::string &player : clean) {
        state.smeared.erase(player);
    }
}

std::vector<std::string> slots_of(const ConquisterState &state, const std::string &player) {
    return furniture_slots(furniture_of(state, player));
}

std::map<std::size_t, std::string> lobsters_copying(const ConquisterState &state, const std::string &claimer,
                                                    const std::string &kicked) {
    const std::vector<std::string> mine = slots_of(state, claimer);
    const std::vector<std::string> theirs = slots_of(state, kicked);
    std::map<std::size_t, std::string> copied;
    for (std::size_t slot = 0; slot < std::min(mine.size(), theirs.size()); ++slot) {
        if (is_lobster(mine[slot]) && !theirs[slot].empty() && !is_lobster(theirs[slot])) {
            copied[slot] = theirs[slot];
        }
    }
    return copied;
}

std::string shown_furniture(const ConquisterState &state, const std::string &player) {
    if (!state.current || state.current->username != player || state.current->lobsters.empty()) {
        return furniture_of(state, player);
    }
    std::vector<std::string> slots = slots_of(state, player);
    for (const auto &[slot, emoji] : state.current->lobsters) {
        /* One burnt from the place is gone, and so is what it had become. */
        if (slot < slots.size() && is_lobster(slots[slot])) {
            slots[slot] = emoji;
        }
    }
    return furniture_stored(std::move(slots));
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
    /* Hung while he is out, it is at home: he did not take it along. */
    if (whereabouts(state, player) != Whereabouts::home) {
        state.stayed[player].push_back(static_cast<std::int64_t>(*slot));
    }
    return true;
}

bool has_balloon(const ConquisterState &state, const std::string &player) {
    return std::ranges::any_of(slots_of(state, player), [](const std::string &slot) {
        return is_power(slot, power::balloon);
    });
}

/* Hangs a 🎈 in his first empty slot; false when there is none. It is his wherever he is now: it is
   with him, not left at home. */
bool hang_balloon(ConquisterState &state, const std::string &player, std::size_t limit) {
    std::vector<std::string> slots = slots_of(state, player);
    const std::optional<std::size_t> slot = free_slot(slots, limit);
    if (!slot) {
        return false;
    }
    if (*slot >= slots.size()) {
        slots.resize(*slot + 1);
    }
    slots[*slot] = std::string{power::balloon.emoji};
    hang(state, player, std::move(slots));
    return true;
}

void welcome(ConquisterState &state, const std::string &player, std::size_t limit) {
    if (counter(state.welcomed, player) != 0) {
        return;
    }
    state.welcomed[player] = 1;
    if (!has_balloon(state, player)) {
        static_cast<void>(hang_balloon(state, player, limit));
    }
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

namespace {

/* The holder who lost a ⚡ he came in with, burnt or blown up, keeps what the hold made so far and
   earns less from now on: each one takes its share of the percent with it. */
void lose_bolts(ConquisterState &state, std::int64_t now, zodiac::Overrides signs) {
    if (!state.current || state.current->bolts <= 0) {
        return;
    }
    Holder &hold = *state.current;
    /* Only the ones he has with him count: one hung at home since he came in was never part of it. */
    const std::vector<std::string> slots = furniture_slots(shown_furniture(state, hold.username));
    std::int64_t left = 0;
    for (std::size_t slot = 0; slot < slots.size(); ++slot) {
        if (is_power(slots[slot], power::bolt) && !stayed_home(state, hold.username, slot)) {
            ++left;
        }
    }
    if (left >= hold.bolts) {
        return;
    }
    hold.banked += hold_value(state, hold, now, signs).earned;
    hold.counted_from = std::max(now, hold.since);
    const std::int64_t share = (hold.lightning_percent - 100) / hold.bolts;
    hold.bolts = left;
    hold.lightning_percent = left > 0 ? 100 + left * share : 0;
}

}

namespace {

/* A ☢️ on @TheConquister37 starts the game over: everybody back to no palle and no emoji, nobody in
   the place or on the road, every balloon new. Who the players are, where they live and the quotes stay. */
void start_over(ConquisterState &state) {
    for (auto &[player, score] : state.scores) {
        score = 0;
    }
    /* Everybody starts again as he first started: with a 🎈. */
    const std::vector<std::string> players = known_players(state);
    state.current.reset();
    state.furniture.clear();
    for (const std::string &player : players) {
        state.furniture[player] = std::string{power::balloon.emoji};
    }
    state.raids.clear();
    state.balloons.clear();
    state.cooldowns.clear();
    state.smeared.clear();
    state.stayed.clear();
    /* And where he first lived: nobody is far away any more. */
    state.flung.clear();
}

/* On a house it does the same to the one who lives there: no palle, no emoji, out of the place
   unpaid, a new balloon. If he is on the road he is home at once, with nothing. */
void start_over(ConquisterState &state, const std::string &player, std::int64_t now) {
    if (const auto score = find_entry(state.scores, player); score != state.scores.end()) {
        score->second = 0;
    }
    if (whereabouts(state, player) == Whereabouts::conquister) {
        state.current.reset();
    }
    /* His ride is not taken off the list here, where the list may be being walked: it is over now and
       the keeper settles it as a homecoming. */
    for (Raid &raid : state.raids) {
        if (raid.raider == player) {
            raid.arrived = true;
            raid.back = now;
            raid.loot = 0;
            raid.gift = 0;
            raid.gift_emoji.clear();
        }
    }
    state.furniture[player] = std::string{power::balloon.emoji};
    state.balloons.erase(player);
    state.cooldowns.erase(player);
    state.smeared.erase(player);
    state.stayed.erase(player);
}

}

FurnitureBurnResult furniture_burn(Storage &storage, const std::string &player, const std::string &emoji,
                                   std::int64_t now, const RaidRules &rules) {
    const FurnitureBurnResult result = storage.transaction([&](StorageSession &session) {
        ConquisterState &state = session.state();
        FurnitureBurnResult outcome;
        if (on_the_road(state, player)) {
            outcome.status = FurnitureBurnStatus::travelling;
            return outcome;
        }
        /* The place is a year of road away for him too, unless he is already standing in it. */
        if (is_flung(state, player)) {
            outcome.status = FurnitureBurnStatus::too_far;
            return outcome;
        }
        if (!take_emoji(state, player, emoji)) {
            outcome.status = FurnitureBurnStatus::not_owned;
            return outcome;
        }
        if (is_power(emoji, power::nuke)) {
            session.backup(std::format("before-reset-{}", now));
            start_over(state);
            outcome.reset = true;
            return outcome;
        }
        outcome.shown = shown_furniture(state, player);
        /* Thrown at the place, it lands on whoever holds it, unless he threw it himself. */
        if (is_thrown(emoji) && state.current && !state.current->username.empty() &&
            state.current->username != player) {
            const std::string holder = state.current->username;
            Landing landing = land(session, state, emoji, player, holder, Whereabouts::conquister, now, rules);
            outcome.backfired = landing.backfired;
            outcome.flung = landing.flung;
            outcome.blown = std::move(landing.blown);
            outcome.shown = shown_furniture(state, player);
            outcome.hit = display_name(state, holder);
            outcome.hit_on_telegram = counter(state.telegram_ids, holder) != 0;
            outcome.hit_furniture = shown_furniture(state, holder);
        }
        lose_bolts(state, now, rules.signs);
        return outcome;
    });
    if (result.reset) {
        log_warning("GAME RESET by user={} with {}: the state before it is in the before-reset-{} backup", player,
                    emoji, now);
    } else if (result.status == FurnitureBurnStatus::burned) {
        log_info("emoji burned user={} emoji={} hit={}", player, emoji, result.hit);
    }
    return result;
}

RecallResult player_recall(Storage &storage, std::string_view name, RaidTargetKind platform) {
    const RecallResult result = storage.transaction([&](StorageSession &session) {
        ConquisterState &state = session.state();
        RecallResult outcome;
        const std::optional<std::string> key = player_by_name(state, name, platform);
        if (!key) {
            outcome.status = RecallStatus::unknown;
            return outcome;
        }
        outcome.name = display_name(state, *key);
        if (!is_flung(state, *key)) {
            outcome.status = RecallStatus::not_flung;
            return outcome;
        }
        state.flung.erase(*key);
        return outcome;
    });
    log_info("recall name={} status={}", name, static_cast<int>(result.status));
    return result;
}

std::vector<std::string> smeared_all(Storage &storage, std::int64_t now) {
    return storage.transaction([now](StorageSession &session) {
        const ConquisterState &state = session.state();
        std::vector<std::string> smeared;
        for (const auto &[player, until] : state.smeared) {
            if (until > now) {
                smeared.push_back(player);
            }
        }
        return smeared;
    });
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
    return storage.transaction([](StorageSession &session) {
        const ConquisterState &state = session.state();
        Authors shown = state.furniture;
        if (state.current) {
            if (const auto mine = find_entry(shown, state.current->username); mine != shown.end()) {
                mine->second = shown_furniture(state, state.current->username);
            }
        }
        return shown;
    });
}

BurnResult palle_burn(Storage &storage, const std::string &player, std::int64_t amount) {
    const BurnResult result = storage.transaction([&](StorageSession &session) {
        ConquisterState &state = session.state();
        BurnResult outcome;
        const std::int64_t score = counter(state.scores, player);
        outcome.score = score;
        if (on_the_road(state, player)) {
            outcome.status = BurnStatus::travelling;
            return outcome;
        }
        if (is_flung(state, player)) {
            outcome.status = BurnStatus::too_far;
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
        const bool holds_place = whereabouts(state, username) == Whereabouts::conquister;
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
            /* What is thrown is not hung: it needs no room on the target's name. */
            if (!is_thrown(gift_emoji) && !has_room(state, *known, rules.furniture_limit)) {
                outcome.status = RaidStatus::no_room;
                return outcome;
            }
            static_cast<void>(take_emoji(state, username, gift_emoji));
        }
        outcome.target = display_name(state, *known);
        const position::Point home = position::coordinates_of(player_id(session, state, username));
        const position::Point theirs = position::coordinates_of(player_id(session, state, *known));
        outcome.seconds = position::travel_seconds(position::distance(home, theirs), rules.travel_divisor);
        /* Whoever a 🌀 flung away is a year of road from everybody, whichever of the two he is. */
        if (is_flung(state, username) || is_flung(state, *known)) {
            outcome.seconds = flung_seconds;
        }
        /* The 🚀 still on his name as he leaves speed up both legs; one carried as a gift does not. */
        const std::int64_t rockets = copies_of(state, username, power::rocket);
        if (rockets > 0 && rules.rocket_percent > 0) {
            outcome.seconds = std::max(position::shortest_travel,
                                       outcome.seconds * 100 / (100 + rockets * rules.rocket_percent));
        }
        leave_home(state, username);
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

/* The 🥷 a raider has with him may take him past what guards the house, the 🎈 that is there and the 🐶,
   without touching either. Only where there is something to slip past. */
bool sneaks(StorageSession &session, const ConquisterState &state, const Raid &raid, const RaidRules &rules,
            RaidEvent &event) {
    const bool guarded = balloon_slot(state, raid.target, Whereabouts::home).has_value() ||
        copies_of(state, raid.target, power::dog) > 0;
    const std::int64_t ninjas = carried_copies(state, raid.raider, power::ninja);
    const std::int64_t chance = std::min<std::int64_t>(100, ninjas * std::max<std::int64_t>(rules.ninja_percent, 0));
    if (!guarded || chance <= 0) {
        return false;
    }
    /* The 🔊 at the house stay there and take their share off it, whether the owner is in or out. */
    const std::int64_t quiet = std::max<std::int64_t>(
        0, chance - copies_of(state, raid.target, power::alarm) * std::max<std::int64_t>(rules.alarm_percent, 0));
    const auto roll = static_cast<std::int64_t>(session.random_index(100));
    event.sneaked = roll < quiet;
    /* A roll his 🥷 alone would have won: it is the alarm that gave him away. */
    event.alarmed = !event.sneaked && roll < chance;
    return event.sneaked;
}

/* A raid meets the 🎈 that is at the house first: the one of a player at home, since he carries it with
   him when he goes out. */
bool defended_at_home(StorageSession &session, ConquisterState &state, const std::string &target,
                      RaidEvent &event) {
    switch (balloon_attempt(session, state, target, Whereabouts::home)) {
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

/* Where the emoji with a power in a slot is: one he carries is wherever he is, unless it was hung
   while he was out; everything else stays at home. */
Whereabouts site_of(const ConquisterState &state, const std::string &player, std::size_t slot, const Power &power) {
    return power.kind == PowerKind::carried && !stayed_home(state, player, slot) ? whereabouts(state, player)
                                                                              : Whereabouts::home;
}

/* A 💣 going off takes one emoji with a power, drawn among those that are there: at his house what he
   left at home, in @TheConquister37 what he carries. In the thrower's own hand it is only among what
   he carries that it draws. The 🎈 is never among them. */
std::vector<std::string> blow_up(StorageSession &session, ConquisterState &state, const std::string &target,
                                 Whereabouts site, bool carried_only = false) {
    std::vector<std::string> slots = slots_of(state, target);
    const auto exposed = [&](std::size_t slot) {
        const Power *power = slots[slot].empty() ? nullptr : power_of(slots[slot]);
        /* No explosion takes a 🎈. */
        return power != nullptr && !is_power(slots[slot], power::balloon) &&
            site_of(state, target, slot, *power) == site && (!carried_only || power->kind == PowerKind::carried);
    };
    std::vector<std::size_t> there;
    for (std::size_t slot = 0; slot < slots.size(); ++slot) {
        if (exposed(slot)) {
            there.push_back(slot);
        }
    }
    if (there.empty()) {
        return {};
    }
    const std::size_t hit = there[session.random_index(there.size())];
    std::vector<std::string> blown{slots[hit]};
    slots[hit].clear();
    hang(state, target, std::move(slots));
    return blown;
}

Landing land(StorageSession &session, ConquisterState &state, std::string_view thrown, const std::string &thrower,
             const std::string &victim, Whereabouts site, std::int64_t now, const RaidRules &rules) {
    Landing landing;
    if (is_power(thrown, power::poo)) {
        smear(state, victim, now, rules.smeared_seconds);
        landing.smeared = true;
    } else if (is_power(thrown, power::bomb)) {
        /* A dud goes off in his hand as he throws it, among what he has with him, and spares the victim. */
        if (rules.bomb_dud_percent > 0 &&
            static_cast<std::int64_t>(session.random_index(100)) < rules.bomb_dud_percent) {
            landing.backfired = true;
            landing.blown = blow_up(session, state, thrower, whereabouts(state, thrower), true);
        } else {
            landing.blown = blow_up(session, state, victim, site);
        }
    } else if (is_power(thrown, power::nuke)) {
        session.backup(std::format("before-reset-{}", now));
        start_over(state, victim, now);
        landing.reset = true;
    } else if (is_power(thrown, power::vortex)) {
        /* Wherever he was, he is far from it now: out of the place, paid for the time he held it. */
        if (whereabouts(state, victim) == Whereabouts::conquister) {
            static_cast<void>(leave_place(state, now, rules.signs));
        }
        state.flung[victim] = now;
        landing.flung = true;
    }
    return landing;
}
}

std::vector<RaidEvent> raid_due(Storage &storage, std::int64_t now, const RaidRules &rules) {
    const std::vector<RaidEvent> events = storage.transaction([&](StorageSession &session) {
        ConquisterState &state = session.state();
        wash(state, now);
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
                event.raider_emoji = shown_furniture(state, raid.raider);
                event.target_emoji = shown_furniture(state, raid.target);
                event.raider_smeared = is_smeared(state, raid.raider, now);
                event.target_smeared = is_smeared(state, raid.target, now);
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
                        /* Poo splatters on arrival, making the target "lo smerdato", and is gone; any
                           other emoji is hung, and a name that filled up meanwhile sends it back the
                           way it came. */
                        if (is_thrown(raid.gift_emoji)) {
                            const std::string thrown = raid.gift_emoji;
                            /* The 📮 at the house may send it back: it then lands on the raider's own
                               house, as it is, and no 📮 of his sends it on again. */
                            const std::int64_t chance = std::min<std::int64_t>(
                                100, copies_of(state, raid.target, power::mailbox) *
                                         std::max<std::int64_t>(rules.mailbox_percent, 0));
                            event.sent_back = chance > 0 &&
                                static_cast<std::int64_t>(session.random_index(100)) < chance;
                            Landing landing;
                            if (event.sent_back) {
                                RaidRules as_it_is = rules;
                                as_it_is.bomb_dud_percent = 0;
                                landing = land(session, state, thrown, raid.target, raid.raider, Whereabouts::home,
                                               now, as_it_is);
                                event.raider_smeared = event.raider_smeared || landing.smeared;
                            } else {
                                landing = land(session, state, thrown, raid.raider, raid.target, Whereabouts::home,
                                               now, rules);
                                event.target_smeared = event.target_smeared || landing.smeared;
                            }
                            raid.gift_emoji.clear();
                            event.backfired = landing.backfired;
                            event.reset = landing.reset;
                            event.flung = landing.flung;
                            event.blown = std::move(landing.blown);
                            event.raider_emoji = shown_furniture(state, raid.raider);
                            event.target_emoji = shown_furniture(state, raid.target);
                        } else if (has_room(state, raid.target, rules.furniture_limit) &&
                            give_emoji(state, raid.target, raid.gift_emoji, rules.furniture_limit)) {
                            raid.gift_emoji.clear();
                            event.target_emoji = shown_furniture(state, raid.target);
                        } else {
                            event.no_room = true;
                        }
                    }
                } else if (!sneaks(session, state, raid, rules, event) &&
                           defended_at_home(session, state, raid.target, event)) {
                    raid.loot = 0;
                } else if (const std::int64_t chance = event.sneaked ? 0 : std::min<std::int64_t>(
                               100, copies_of(state, raid.target, power::dog) * std::max<std::int64_t>(rules.dog_percent, 0));
                           chance > 0 && static_cast<std::int64_t>(session.random_index(100)) < chance) {
                    /* The dogs stay at home and guard it whether he is in or out: one of them caught him. */
                    event.intercepted = true;
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
                    /* The road says what can be taken, and nobody loses more than he has; every 🥺
                       on his name as the raider arrives makes him take a share less. */
                    const std::int64_t taken = std::min(theirs, carried);
                    const std::int64_t pleas = copies_of(state, raid.target, power::pleading);
                    event.pleaded_percent = std::min<std::int64_t>(100, pleas * std::max<std::int64_t>(
                        rules.pleading_percent, 0));
                    event.spared = taken * event.pleaded_percent / 100;
                    event.loot = taken - event.spared;
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
                    .raider_emoji = shown_furniture(state, raid.raider),
                    .target_emoji = shown_furniture(state, raid.target),
                    .raider_smeared = is_smeared(state, raid.raider, now),
                    .target_smeared = is_smeared(state, raid.target, now),
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
            if (event.reset) {
                log_warning("PLAYER RESET of {} by user={}: the state before it is in the before-reset-{} backup",
                            event.target, event.raider, now);
            }
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
