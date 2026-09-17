#include "game.hpp"

#include "logging.hpp"
#include "text.hpp"
#include "zodiac.hpp"

#include <algorithm>
#include <iterator>
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

ClaimResult conquister_claim(
    Storage &storage,
    std::int64_t user_id,
    const std::string &username,
    std::int64_t now,
    const ClaimRules &rules
) {
    const ClaimResult result = storage.transaction([&](StorageSession &session) {
        ConquisterState &state = session.state();
        ClaimResult outcome;
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
            outcome.earned = now > state.current->since ? now - state.current->since : 0;
            if (const auto boost = find_entry(state.boosts, holder); boost != state.boosts.end()) {
                outcome.boost_multiplier = boost->second;
                if (outcome.earned > std::numeric_limits<std::int64_t>::max() / outcome.boost_multiplier) {
                    log_error("Could not multiply the Conquister score");
                    throw StorageError("Conquister score overflow");
                }
                outcome.earned *= outcome.boost_multiplier;
                state.boosts.erase(holder);
            }
            outcome.zodiac_percent = zodiac::percent_for(holder, now, rules.signs);
            outcome.earned = outcome.earned / 100 * outcome.zodiac_percent +
                             outcome.earned % 100 * outcome.zodiac_percent / 100;
            std::int64_t &score = state.scores[outcome.previous_username];
            if (score > 0 && outcome.earned > std::numeric_limits<std::int64_t>::max() - score) {
                log_error("Could not update Conquister score");
                throw StorageError("Conquister score overflow");
            }
            score += outcome.earned;
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
            "claim defended user={} holder={} next_chance={} penalty={} shield={}",
            username,
            result.previous_username,
            result.next_chance,
            result.penalty_seconds,
            result.shield_seconds
        );
        break;
    case ClaimStatus::cooldown:
        log_info("claim blocked user={} wait={}", username, result.penalty_seconds);
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
            return LeaderboardEntry{entry.first, entry.second, counter(state.quotes_added, entry.first)};
        });
        if (state.current && !state.current->username.empty()) {
            leaderboard.current = state.current;
        }
        return leaderboard;
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
        return removed;
    });
}

}
