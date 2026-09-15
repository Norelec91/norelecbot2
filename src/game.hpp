#ifndef NORELECBOT_GAME_HPP
#define NORELECBOT_GAME_HPP

#include "storage.hpp"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace norelecbot {

enum class ClaimStatus { taken, already_held, defended, cooldown };

struct ClaimResult {
    ClaimStatus status = ClaimStatus::taken;
    /* The holder who was kicked, or the one whose balloon held. */
    std::string previous_username;
    /* Their Telegram id, zero when they played from IRC: only a Telegram name may be written as a mention. */
    std::int64_t previous_user_id = 0;
    std::int64_t earned = 0;
    /* taken: getting in popped a balloon. defended: the percentage the next attempt will have. */
    bool balloon_popped = false;
    int next_chance = 0;
    /* cooldown: seconds still to wait. defended: the penalty just handed out. */
    std::int64_t penalty_seconds = 0;
};

enum class BalloonStatus { bought, already_owned, insufficient_score };

struct BalloonResult {
    BalloonStatus status = BalloonStatus::bought;
    std::int64_t available_score = 0;
};

struct LeaderboardEntry {
    std::string username;
    std::int64_t score = 0;
    std::int64_t quotes_added = 0;
};

struct Leaderboard {
    std::vector<LeaderboardEntry> entries;
    std::optional<Holder> current;
};

struct ConquisterUser {
    std::string username;
    std::int64_t score = 0;
    std::size_t rank = 0;
    std::int64_t quotes_added = 0;
    bool in_conquister = false;
    std::int64_t since = 0;
};

enum class QuoteAddStatus { added, duplicate, insufficient_score };

struct QuoteAddResult {
    QuoteAddStatus status = QuoteAddStatus::added;
    std::int64_t available_score = 0;
};

struct QuotePage {
    std::vector<std::string> items;
    std::size_t total = 0;
    std::size_t page = 0;
    std::size_t pages = 0;
    std::size_t first_number = 0;
};

/* A failed balloon attempt costs the attacker cooldown_seconds without a claim; 0 disables the penalty. */
[[nodiscard]] ClaimResult conquister_claim(
    Storage &storage,
    std::int64_t user_id,
    const std::string &username,
    std::int64_t now,
    int cooldown_seconds
);
/* limit 0 returns every entry. */
[[nodiscard]] Leaderboard conquister_leaderboard(Storage &storage, std::size_t limit);
/* Case-insensitive lookup; rank is 0 when the user has no score yet. */
[[nodiscard]] std::optional<ConquisterUser> conquister_user(Storage &storage, std::string_view username);
/* One balloon per user: it survives 4 attempts at most, then has to be bought again. */
[[nodiscard]] BalloonResult balloon_buy(Storage &storage, const std::string &username, int cost);

[[nodiscard]] QuoteAddResult quote_add(
    Storage &storage,
    const std::string &username,
    const std::string &quote,
    int cost
);
[[nodiscard]] QuotePage quote_page_load(Storage &storage, int requested_page);
[[nodiscard]] std::optional<std::string> quote_random(Storage &storage);
/* The selector is a position shown by /quotes or the exact quote text. */
[[nodiscard]] std::optional<std::string> quote_delete(Storage &storage, std::string_view selector);

}

#endif
