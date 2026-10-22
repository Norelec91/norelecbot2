#include "sentinel.hpp"

#include "logging.hpp"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <cctype>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <exception>
#include <filesystem>
#include <format>
#include <fstream>
#include <iterator>
#include <numeric>
#include <set>
#include <system_error>
#include <tuple>

namespace norelecbot {
namespace {

using Json = nlohmann::json;

constexpr std::int64_t remembered_seconds = 7 * 24 * 60 * 60;
/* A pause this long ends a sitting: whoever plays never stops for longer for hours on end. */
constexpr std::int64_t pause_seconds = 15 * 60;
/* A start this soon after coming home is quicker than anybody reads and types. */
constexpr std::int64_t instant_seconds = 3;
/* A start later than this after coming home has nothing to do with the homecoming. */
constexpr std::int64_t reaction_seconds = 30 * 60;
constexpr std::int64_t bucket_seconds = 10 * 60;
/* The points from which an account is told as a script, or as one to keep an eye on; and the points a
   pair of accounts needs to be told as the same person. */
constexpr int script_points = 60;
constexpr int suspect_points = 35;
constexpr int pair_points = 40;
constexpr std::size_t report_limit = 3800;
/* Nobody is blocked before the sentinel has watched a whole day. */
constexpr std::int64_t watch_before_blocking = 24 * 60 * 60;

struct Finding {
    int points = 0;
    std::string why;
};

int percent(std::size_t part, std::size_t whole) {
    return whole == 0 ? 0 : static_cast<int>(part * 100 / whole);
}

std::string hours(std::int64_t seconds) {
    return std::format("{}h{:02}", seconds / 3600, seconds % 3600 / 60);
}

std::string wait(std::int64_t seconds) {
    return seconds < 120 ? std::format("{}s", seconds) : std::format("{}m{:02}s", seconds / 60, seconds % 60);
}

template <typename Value>
Value median(std::vector<Value> values) {
    std::ranges::sort(values);
    return values[values.size() / 2];
}

std::vector<std::int64_t> starts_of(const Sentinel::Player &player) {
    std::vector<std::int64_t> starts;
    for (const Sentinel::Act &act : player.acts) {
        if (act.kind != 2) {
            starts.push_back(act.at);
        }
    }
    return starts;
}

/* How long after each homecoming the next ride set off, when it did soon enough to follow from it. */
std::vector<std::int64_t> reactions(const Sentinel::Player &player, const std::vector<std::int64_t> &starts) {
    std::vector<std::int64_t> delays;
    for (const std::int64_t back : player.backs) {
        const auto next = std::ranges::lower_bound(starts, back);
        if (next != starts.end() && *next - back <= reaction_seconds) {
            delays.push_back(*next - back);
        }
    }
    return delays;
}

/* The longest stretch of play without a pause. */
std::int64_t longest_sitting(const Sentinel::Player &player) {
    std::int64_t longest = 0;
    std::int64_t since = 0;
    std::int64_t last = 0;
    for (const Sentinel::Act &act : player.acts) {
        if (last == 0 || act.at - last >= pause_seconds) {
            since = act.at;
        }
        last = act.at;
        longest = std::max(longest, last - since);
    }
    return longest;
}

std::string normalized(std::string_view name) {
    std::string kept;
    for (const char letter : name) {
        if (std::isalpha(static_cast<unsigned char>(letter))) {
            kept.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(letter))));
        }
    }
    return kept;
}

bool names_alike(std::string_view left, std::string_view right) {
    std::string a = normalized(left);
    std::string b = normalized(right);
    if (a.size() > b.size()) {
        std::swap(a, b);
    }
    return a.size() >= 3 && b.starts_with(a);
}

/* The signs that nobody is reading and typing: the points they are worth and what was seen. */
std::vector<Finding> script_signs(const Sentinel::Player &player, std::int64_t commands, std::int64_t chats,
                                  bool chats_count, const std::vector<std::int64_t> &command_times,
                                  const Sentinel::Names &name) {
    std::vector<Finding> found;
    const std::vector<std::int64_t> starts = starts_of(player);
    const std::vector<std::int64_t> delays = reactions(player, starts);
    /* Setting off again the instant he is home, again and again. */
    if (delays.size() >= 10) {
        const auto instant = static_cast<std::size_t>(
            std::ranges::count_if(delays, [](std::int64_t delay) { return delay < instant_seconds; }));
        const int share = percent(instant, delays.size());
        const int points = share >= 50 ? 30 : share >= 25 ? 20 : share >= 10 ? 8 : 0;
        if (points > 0) {
            found.push_back({points, std::format("riparte entro {} secondi dal rientro nel {}% dei casi ({} su {})",
                                                 instant_seconds, share, instant, delays.size())});
        }
    }
    /* Waiting always the same while at it: a person waits a minute, then five, then twenty. */
    std::vector<std::int64_t> waits;
    std::ranges::copy_if(delays, std::back_inserter(waits), [](std::int64_t delay) { return delay >= 60; });
    if (waits.size() >= 15) {
        const std::int64_t usual = median(waits);
        const auto steady = static_cast<std::size_t>(std::ranges::count_if(
            waits, [usual](std::int64_t delay) { return std::abs(delay - usual) * 10 <= usual; }));
        const int share = percent(steady, waits.size());
        const int points = share >= 60 ? 25 : share >= 40 ? 15 : 0;
        if (points > 0) {
            found.push_back({points, std::format("aspetta sempre lo stesso tempo prima di ripartire: {} ±10% nel {}% "
                                                 "dei casi", wait(usual), share)});
        }
    } else if (delays.size() >= 20) {
        /* Quick, but not instant: a random wait of a few seconds is too even to be a person's. */
        std::vector<std::int64_t> sorted = delays;
        std::ranges::sort(sorted);
        const std::int64_t usual = sorted[sorted.size() / 2];
        const std::int64_t most = sorted[sorted.size() * 9 / 10];
        if (usual >= instant_seconds && most * 10 < usual * 16) {
            found.push_back({15, std::format("tempi di ripartenza troppo uguali: metà entro {}, nove su dieci entro {}",
                                             wait(usual), wait(most))});
        }
    }
    /* No sleep. */
    const std::int64_t sitting = longest_sitting(player);
    if (sitting >= 8 * 3600) {
        found.push_back({20, std::format("ha giocato {} di fila senza una pausa di 15 minuti", hours(sitting))});
    } else if (sitting >= 5 * 3600) {
        found.push_back({10, std::format("ha giocato {} di fila senza una pausa di 15 minuti", hours(sitting))});
    }
    std::set<int> hours_of_day;
    for (const Sentinel::Act &act : player.acts) {
        hours_of_day.insert(local_hour(act.at));
    }
    if (hours_of_day.size() >= 22) {
        found.push_back({10, std::format("attivo a tutte le ore del giorno ({} su 24)", hours_of_day.size())});
    }
    /* Only commands, never a word, where the chat lets the bot hear words. */
    if (chats_count && commands >= 50) {
        if (chats == 0) {
            found.push_back({15, std::format("{} comandi e nessun messaggio in chat", commands)});
        } else if (chats * 100 < commands) {
            found.push_back({8, std::format("{} comandi e solo {} messaggi in chat", commands, chats)});
        }
    }
    /* Always the same target, whatever happens to him. */
    std::map<std::string, std::size_t> targets;
    std::size_t raids = 0;
    for (const Sentinel::Act &act : player.acts) {
        if (act.kind == 0) {
            ++targets[act.target];
            ++raids;
        }
    }
    if (raids >= 30) {
        const auto top = std::ranges::max_element(targets, {}, [](const auto &entry) { return entry.second; });
        const int share = percent(top->second, raids);
        if (share >= 90) {
            found.push_back({5, std::format("colpisce sempre {} ({}%)", name(top->first), share)});
        }
    }
    if (player.robbed_by.size() >= 10) {
        const bool struck_back = std::ranges::any_of(player.robbed_by, [&](const auto &robbery) {
            return std::ranges::any_of(player.acts, [&](const Sentinel::Act &act) {
                return act.kind == 0 && act.target == robbery.second && act.at >= robbery.first &&
                    act.at - robbery.first <= reaction_seconds;
            });
        });
        if (!struck_back) {
            found.push_back({5, std::format("derubato {} volte, non ha mai reagito contro chi l'ha colpito",
                                            player.robbed_by.size())});
        }
    }
    /* Bursts quicker than fingers. */
    std::size_t bursts = 0;
    for (std::size_t index = 1; index < command_times.size(); ++index) {
        bursts += command_times[index] - command_times[index - 1] < instant_seconds ? 1 : 0;
    }
    if (bursts >= 10 && percent(bursts, command_times.size()) >= 20) {
        found.push_back({5, std::format("raffiche di comandi: {} a meno di {} secondi dal precedente", bursts,
                                        instant_seconds)});
    }
    return found;
}

int total(const std::vector<Finding> &found) {
    return std::min(100, std::accumulate(found.begin(), found.end(), 0,
                                         [](int sum, const Finding &finding) { return sum + finding.points; }));
}

/* What two accounts are seen doing the way one person would. */
struct Traits {
    std::map<std::string, std::size_t> gifts;
    std::map<std::string, std::size_t> raids;
    std::vector<std::int64_t> starts;
    std::set<std::int64_t> buckets;
    std::set<std::string> hosts;
    std::int64_t first_seen = 0;
    std::size_t acts = 0;
};

std::vector<Finding> pair_signs(const std::string &left, const Traits &a, const std::string &right, const Traits &b,
                                std::int64_t started, const std::map<std::string, std::size_t> &raiders,
                                const Sentinel::Names &name) {
    std::vector<Finding> found;
    const std::string left_name = name(left);
    const std::string right_name = name(right);
    /* Feeding one from the other. */
    const auto given = [](const Traits &from, const std::string &to) {
        const auto entry = from.gifts.find(to);
        return entry == from.gifts.end() ? std::size_t{0} : entry->second;
    };
    const std::size_t there = given(a, right);
    const std::size_t back = given(b, left);
    /* Two presents can be a friend's; more, only ever one way, are feeding; five are enough on their own. */
    if (there >= 2 && back == 0) {
        found.push_back({there >= 5 ? 40 : there >= 3 ? 30 : 15, std::format("regali solo da {} a {} ({})", left_name, right_name, there)});
    } else if (back >= 2 && there == 0) {
        found.push_back({back >= 5 ? 40 : back >= 3 ? 30 : 15, std::format("regali solo da {} a {} ({})", right_name, left_name, back)});
    }
    /* Never at each other's throat, though at the same throats. */
    const auto raided = [](const Traits &from, const std::string &to) {
        const auto entry = from.raids.find(to);
        return entry == from.raids.end() ? std::size_t{0} : entry->second;
    };
    std::size_t a_raids = 0;
    for (const auto &[target, count] : a.raids) {
        a_raids += count;
    }
    std::size_t b_raids = 0;
    for (const auto &[target, count] : b.raids) {
        b_raids += count;
    }
    if (a_raids >= 10 && b_raids >= 10 && raided(a, right) == 0 && raided(b, left) == 0) {
        /* A target everybody goes for says nothing: only one few go for does. */
        for (const auto &[target, count] : a.raids) {
            const auto many = raiders.find(target);
            if (count >= 3 && raided(b, target) >= 3 && many != raiders.end() && many->second <= 3) {
                found.push_back({15, std::format("colpiscono tutti e due {} ma non si attaccano mai", name(target))});
                break;
            }
        }
    }
    /* Moving together, as one script does with two names. */
    if (a.starts.size() >= 10 && b.starts.size() >= 10) {
        std::size_t together = 0;
        for (const std::int64_t start : a.starts) {
            const auto near = std::ranges::lower_bound(b.starts, start - 10);
            together += near != b.starts.end() && *near <= start + 10 ? 1 : 0;
        }
        /* As many as chance alone would give, from how often the other sets off while about. */
        double chance = 0;
        if (!b.buckets.empty()) {
            const double rate = static_cast<double>(b.starts.size()) /
                static_cast<double>(b.buckets.size() * static_cast<std::size_t>(bucket_seconds));
            for (const std::int64_t start : a.starts) {
                chance += b.buckets.contains(start / bucket_seconds) ? 21 * rate : 0;
            }
        }
        const int share = percent(together, std::min(a.starts.size(), b.starts.size()));
        if (share >= 30 && together >= 10 && static_cast<double>(together) >= 4 * chance) {
            found.push_back({30, std::format("partono insieme, a pochi secondi l'uno dall'altro ({} volte)", together)});
        }
    }
    /* Taking turns, as one person does with two names: never together, one stops and the other starts. */
    if (a.buckets.size() >= 12 && b.buckets.size() >= 12) {
        std::size_t shared = 0;
        for (const std::int64_t bucket : a.buckets) {
            shared += b.buckets.contains(bucket) ? 1 : 0;
        }
        if (shared * 20 <= std::min(a.buckets.size(), b.buckets.size())) {
            std::vector<std::pair<std::int64_t, bool>> merged;
            for (const std::int64_t bucket : a.buckets) {
                merged.emplace_back(bucket, true);
            }
            for (const std::int64_t bucket : b.buckets) {
                merged.emplace_back(bucket, false);
            }
            std::ranges::sort(merged);
            std::size_t switches = 0;
            for (std::size_t index = 1; index < merged.size(); ++index) {
                switches += merged[index].second != merged[index - 1].second &&
                    merged[index].first - merged[index - 1].first <= 2 ? 1 : 0;
            }
            if (switches >= 4) {
                found.push_back({20, std::format("mai attivi insieme: quando uno smette l'altro comincia ({} cambi)",
                                                 switches)});
            }
        }
    }
    /* The same machine behind both on IRC. */
    for (const std::string &host : a.hosts) {
        if (b.hosts.contains(host)) {
            found.push_back({40, "stesso host IRC"});
            break;
        }
    }
    if (names_alike(left_name, right_name)) {
        found.push_back({15, "nomi simili"});
    }
    /* Both first seen within two days, once the sentinel had watched long enough to know who was there before. */
    if (a.first_seen > started + 3 * 24 * 3600 && b.first_seen > started + 3 * 24 * 3600 &&
        std::abs(a.first_seen - b.first_seen) <= 2 * 24 * 3600) {
        found.push_back({10, "comparsi a meno di due giorni l'uno dall'altro"});
    }
    return found;
}

std::string reasons(const std::vector<Finding> &found) {
    std::string told;
    for (const Finding &finding : found) {
        told += std::format("\n   · {}", finding.why);
    }
    return told;
}

void forget_before(std::vector<std::int64_t> &times, std::int64_t oldest) {
    std::erase_if(times, [oldest](std::int64_t at) { return at < oldest; });
}

struct Judged {
    std::string player;
    int points = 0;
    std::vector<Finding> found;
};

/* Every account with enough play scored, the rest of those with enough told as normal, and what each did. */
struct Judgement {
    std::vector<Judged> judged;
    std::vector<std::string> normal;
    std::map<std::string, Traits> traits;
};

Judgement judge(const std::map<std::string, Sentinel::Player> &players,
                const std::map<std::string, Sentinel::Identity> &identities, std::int64_t now,
                const Sentinel::Names &name) {
    const std::int64_t oldest = now - remembered_seconds;
    /* Whether the bot hears plain words at all on each side: a Telegram bot may only get commands. */
    bool telegram_words = false;
    bool irc_words = false;
    for (const auto &[key, identity] : identities) {
        const bool any = std::ranges::any_of(identity.chats, [oldest](std::int64_t at) { return at >= oldest; });
        (key.starts_with("tg:") ? telegram_words : irc_words) |= any;
    }

    Judgement judgement;
    std::vector<Judged> &judged = judgement.judged;
    std::vector<std::string> &normal = judgement.normal;
    std::map<std::string, Traits> &traits = judgement.traits;
    for (const auto &[key, player] : players) {
        Sentinel::Player recent = player;
        std::erase_if(recent.acts, [oldest](const Sentinel::Act &act) { return act.at < oldest; });
        forget_before(recent.backs, oldest);
        std::erase_if(recent.robbed_by, [oldest](const auto &robbery) { return robbery.first < oldest; });
        if (recent.acts.empty()) {
            continue;
        }
        std::int64_t commands = 0;
        std::int64_t chats = 0;
        bool chats_count = false;
        std::vector<std::int64_t> command_times;
        Traits &mine = traits[key];
        for (const auto &[identity_key, identity] : identities) {
            if (identity.player != key) {
                continue;
            }
            const auto recent_count = [oldest](const std::vector<std::int64_t> &times) {
                return static_cast<std::int64_t>(
                    std::ranges::count_if(times, [oldest](std::int64_t at) { return at >= oldest; }));
            };
            commands += recent_count(identity.commands);
            chats += recent_count(identity.chats);
            chats_count |= identity_key.starts_with("tg:") ? telegram_words : irc_words;
            std::ranges::copy_if(identity.commands, std::back_inserter(command_times),
                                 [oldest](std::int64_t at) { return at >= oldest; });
            if (!identity.host.empty()) {
                mine.hosts.insert(identity.host);
            }
        }
        std::ranges::sort(command_times);
        mine.first_seen = player.first_seen;
        mine.acts = recent.acts.size();
        for (const Sentinel::Act &act : recent.acts) {
            if (act.kind == 0) {
                ++mine.raids[act.target];
            } else if (act.kind == 1) {
                ++mine.gifts[act.target];
            }
            if (act.kind != 2) {
                mine.starts.push_back(act.at);
            }
            mine.buckets.insert(act.at / bucket_seconds);
        }
        if (recent.acts.size() < 20) {
            continue;
        }
        std::vector<Finding> found = script_signs(recent, commands, chats, chats_count, command_times, name);
        const int points = total(found);
        if (points >= suspect_points) {
            judged.push_back({key, points, std::move(found)});
        } else {
            normal.push_back(name(key));
        }
    }
    std::ranges::sort(judged, std::greater{}, &Judged::points);
    return judgement;
}

}


int local_hour(std::int64_t at) {
    const std::chrono::local_seconds local =
        std::chrono::current_zone()->to_local(std::chrono::sys_seconds{std::chrono::seconds{at}});
    const auto day = std::chrono::floor<std::chrono::days>(local);
    return static_cast<int>(std::chrono::duration_cast<std::chrono::hours>(local - day).count());
}

std::int64_t local_day(std::int64_t at) {
    const std::chrono::local_seconds local =
        std::chrono::current_zone()->to_local(std::chrono::sys_seconds{std::chrono::seconds{at}});
    return std::chrono::floor<std::chrono::days>(local).time_since_epoch().count();
}

Sentinel::Player &Sentinel::player(const std::string &key, std::int64_t now) {
    if (started_ == 0) {
        started_ = now;
    }
    forget(now);
    Player &found = players_[key];
    if (found.first_seen == 0) {
        found.first_seen = now;
    }
    return found;
}

void Sentinel::forget(std::int64_t now) {
    if (now - forgotten_ < 3600) {
        return;
    }
    forgotten_ = now;
    const std::int64_t oldest = now - remembered_seconds;
    for (auto &[key, player] : players_) {
        std::erase_if(player.acts, [oldest](const Act &act) { return act.at < oldest; });
        forget_before(player.backs, oldest);
        std::erase_if(player.robbed_by, [oldest](const auto &robbery) { return robbery.first < oldest; });
    }
    for (auto &[key, identity] : identities_) {
        forget_before(identity.commands, oldest);
        forget_before(identity.chats, oldest);
    }
}

void Sentinel::ride(const std::string &player_key, const std::string &target, bool gift, std::int64_t now) {
    const std::scoped_lock lock{mutex_};
    player(player_key, now).acts.push_back({now, gift ? 1 : 0, target});
}

void Sentinel::claim(const std::string &player_key, std::int64_t now) {
    const std::scoped_lock lock{mutex_};
    player(player_key, now).acts.push_back({now, 2, {}});
}

void Sentinel::back(const std::string &player_key, std::int64_t now) {
    const std::scoped_lock lock{mutex_};
    player(player_key, now).backs.push_back(now);
}

void Sentinel::robbed(const std::string &victim, const std::string &robber, std::int64_t now) {
    const std::scoped_lock lock{mutex_};
    player(victim, now).robbed_by.emplace_back(now, robber);
}

void Sentinel::heard(const std::string &identity, std::string_view host, bool command, std::int64_t now) {
    const std::scoped_lock lock{mutex_};
    if (started_ == 0) {
        started_ = now;
    }
    forget(now);
    Identity &heard = identities_[identity];
    if (!host.empty()) {
        heard.host = std::string{host};
    }
    (command ? heard.commands : heard.chats).push_back(now);
}

void Sentinel::link(const std::string &identity, const std::string &player_key) {
    const std::scoped_lock lock{mutex_};
    identities_[identity].player = player_key;
}

std::vector<std::string> Sentinel::scripts(std::int64_t now) const {
    const std::scoped_lock lock{mutex_};
    std::vector<std::string> sure;
    if (started_ == 0 || now - started_ < watch_before_blocking) {
        return sure;
    }
    for (const Judged &one : judge(players_, identities_, now, [](const std::string &key) { return key; }).judged) {
        if (one.points >= script_points) {
            sure.push_back(one.player);
        }
    }
    return sure;
}

void Sentinel::block(const std::string &player_key, std::int64_t until) {
    const std::scoped_lock lock{mutex_};
    blocked_[player_key] = until;
}

bool Sentinel::unblock(const std::string &player_key) {
    const std::scoped_lock lock{mutex_};
    told_.erase(player_key);
    return blocked_.erase(player_key) > 0;
}

std::int64_t Sentinel::blocked_until(const std::string &player_key, std::int64_t now) const {
    const std::scoped_lock lock{mutex_};
    const auto found = blocked_.find(player_key);
    return found != blocked_.end() && found->second > now ? found->second : 0;
}

bool Sentinel::tell_blocked(const std::string &player_key, std::int64_t now) {
    const std::scoped_lock lock{mutex_};
    const auto told = told_.find(player_key);
    if (told != told_.end() && now - told->second < 3600) {
        return false;
    }
    told_[player_key] = now;
    return true;
}

std::int64_t Sentinel::reported_day() const {
    const std::scoped_lock lock{mutex_};
    return reported_day_;
}

void Sentinel::set_reported_day(std::int64_t day) {
    const std::scoped_lock lock{mutex_};
    reported_day_ = day;
}

std::string Sentinel::report(std::int64_t now, const Names &name) const {
    const std::scoped_lock lock{mutex_};
    Judgement judgement = judge(players_, identities_, now, name);
    std::vector<Judged> &judged = judgement.judged;
    std::vector<std::string> &normal = judgement.normal;
    std::map<std::string, Traits> &traits = judgement.traits;

    /* Pairs that look like one person, gathered into groups. */
    std::vector<std::string> keys;
    for (const auto &[key, mine] : traits) {
        keys.push_back(key);
    }
    std::vector<std::size_t> parent(keys.size());
    std::iota(parent.begin(), parent.end(), std::size_t{0});
    const auto root = [&parent](std::size_t index) {
        while (parent[index] != index) {
            index = parent[index] = parent[parent[index]];
        }
        return index;
    };
    std::map<std::string, std::size_t> raiders;
    for (const auto &[key, mine] : traits) {
        for (const auto &[target, count] : mine.raids) {
            ++raiders[target];
        }
    }
    std::vector<std::tuple<std::size_t, std::size_t, int, std::vector<Finding>>> pairs;
    for (std::size_t left = 0; left < keys.size(); ++left) {
        for (std::size_t right = left + 1; right < keys.size(); ++right) {
            std::vector<Finding> found =
                pair_signs(keys[left], traits[keys[left]], keys[right], traits[keys[right]], started_, raiders, name);
            const int points = total(found);
            if (points >= pair_points) {
                parent[root(left)] = root(right);
                pairs.emplace_back(left, right, points, std::move(found));
            }
        }
    }

    std::string told = std::format("🛡️ Sentinella: ultimi {} giorni",
                                   std::max<std::int64_t>(1, std::min<std::int64_t>(7, (now - started_) / 86400 + 1)));
    const auto section = [&](std::string_view title, int least, int most) {
        std::string part;
        for (const Judged &one : judged) {
            if (one.points >= least && one.points <= most) {
                part += std::format("\n• {} {}/100{}", name(one.player), one.points, reasons(one.found));
            }
        }
        if (!part.empty()) {
            told += std::format("\n\n{}{}", title, part);
        }
    };
    std::string blocked;
    for (const auto &[key, until] : blocked_) {
        if (until > now) {
            blocked += std::format("\n• {} ancora per {}", name(key), hours(until - now));
        }
    }
    if (!blocked.empty()) {
        told += std::format("\n\n🚫 Bloccati (sblocca con /sblocca <nome>){}", blocked);
    }
    section("🤖 Script quasi certi", script_points, 100);
    section("🤔 Da tenere d'occhio", suspect_points, script_points - 1);
    std::map<std::size_t, std::vector<std::size_t>> groups;
    for (const auto &[left, right, points, found] : pairs) {
        groups[root(left)].push_back(left);
        groups[root(left)].push_back(right);
    }
    if (!groups.empty()) {
        told += "\n\n👥 Probabilmente la stessa persona";
        for (auto &[group, members] : groups) {
            std::ranges::sort(members);
            members.erase(std::ranges::unique(members).begin(), members.end());
            std::string who;
            for (const std::size_t member : members) {
                who += (who.empty() ? "" : ", ") + name(keys[member]);
            }
            told += std::format("\n• {}", who);
            for (const auto &[left, right, points, found] : pairs) {
                if (root(left) == group) {
                    told += std::format("\n   {} e {} ({}/100):{}", name(keys[left]), name(keys[right]), points,
                                        reasons(found));
                }
            }
        }
    }
    if (!normal.empty()) {
        std::ranges::sort(normal);
        std::string who;
        for (const std::string &one : normal) {
            who += (who.empty() ? "" : ", ") + one;
        }
        told += std::format("\n\n✅ Nella norma: {}", who);
    }
    if (judged.empty() && groups.empty() && normal.empty()) {
        told += "\n\nAncora pochi dati: serve qualche ora di gioco.";
    }
    if (told.size() > report_limit) {
        std::size_t cut = report_limit;
        while (cut > 0 && (static_cast<unsigned char>(told[cut]) & 0xC0) == 0x80) {
            --cut;
        }
        told = told.substr(0, cut) + "\n…";
    }
    return told;
}

void Sentinel::load(const std::string &path) {
    std::ifstream file{path, std::ios::binary};
    if (!file) {
        return;
    }
    try {
        const Json saved = Json::parse(file);
        const std::scoped_lock lock{mutex_};
        started_ = saved.value("started", std::int64_t{0});
        reported_day_ = saved.value("reported_day", std::int64_t{0});
        blocked_ = saved.value("blocked", std::map<std::string, std::int64_t>{});
        const Json players = saved.value("players", Json::object());
        for (const auto &[key, entry] : players.items()) {
            Player &player = players_[key];
            player.first_seen = entry.value("first_seen", std::int64_t{0});
            for (const Json &act : entry.value("acts", Json::array())) {
                player.acts.push_back({act.at(0).get<std::int64_t>(), act.at(1).get<int>(), act.at(2).get<std::string>()});
            }
            player.backs = entry.value("backs", std::vector<std::int64_t>{});
            for (const Json &robbery : entry.value("robbed_by", Json::array())) {
                player.robbed_by.emplace_back(robbery.at(0).get<std::int64_t>(), robbery.at(1).get<std::string>());
            }
        }
        const Json identities = saved.value("identities", Json::object());
        for (const auto &[key, entry] : identities.items()) {
            Identity &identity = identities_[key];
            identity.player = entry.value("player", std::string{});
            identity.host = entry.value("host", std::string{});
            identity.commands = entry.value("commands", std::vector<std::int64_t>{});
            identity.chats = entry.value("chats", std::vector<std::int64_t>{});
        }
    } catch (const std::exception &error) {
        log_warning("The sentinel file {} could not be read: {}", path, error.what());
    }
}

void Sentinel::save(const std::string &path) const {
    if (path.empty()) {
        return;
    }
    Json saved = Json::object();
    {
        const std::scoped_lock lock{mutex_};
        saved["started"] = started_;
        saved["reported_day"] = reported_day_;
        saved["blocked"] = blocked_;
        Json players = Json::object();
        for (const auto &[key, player] : players_) {
            Json acts = Json::array();
            for (const Act &act : player.acts) {
                acts.push_back(Json::array({act.at, act.kind, act.target}));
            }
            Json robbed = Json::array();
            for (const auto &[at, robber] : player.robbed_by) {
                robbed.push_back(Json::array({at, robber}));
            }
            players[key] = {{"first_seen", player.first_seen}, {"acts", acts}, {"backs", player.backs},
                            {"robbed_by", robbed}};
        }
        saved["players"] = players;
        Json identities = Json::object();
        for (const auto &[key, identity] : identities_) {
            identities[key] = {{"player", identity.player}, {"host", identity.host},
                               {"commands", identity.commands}, {"chats", identity.chats}};
        }
        saved["identities"] = identities;
    }
    const std::string temporary = path + ".tmp";
    {
        std::ofstream file{temporary, std::ios::binary | std::ios::trunc};
        if (!file) {
            log_warning("The sentinel file {} could not be written", temporary);
            return;
        }
        file << saved.dump();
    }
    std::error_code error;
    std::filesystem::rename(temporary, path, error);
    if (error) {
        log_warning("The sentinel file {} could not be replaced: {}", path, error.message());
    }
}

Sentinel &sentinel() {
    static Sentinel watching;
    return watching;
}

}
