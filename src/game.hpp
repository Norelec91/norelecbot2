#ifndef NORELECBOT_GAME_HPP
#define NORELECBOT_GAME_HPP

#include "storage.hpp"
#include "zodiac.hpp"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace norelecbot {

enum class ClaimStatus { taken, already_held, defended, cooldown, travelling };

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
    /* defended: the palle the failed attempt cost, never more than the attacker had. */
    std::int64_t attack_cost = 0;
    /* defended: how long a balloon no attempt can pop still holds, zero for an ordinary one. */
    std::int64_t shield_seconds = 0;
    /* travelling: how long before the claimer is home again. */
    std::int64_t travel_seconds = 0;
    /* taken: the multiplier the kicked holder had bought, zero when there was none. */
    std::int64_t boost_multiplier = 0;
    /* taken: what the house of the day was worth to the kicked holder, 100 when it was indifferent. */
    int zodiac_percent = 100;
};

enum class BalloonStatus { bought, already_owned, has_boost, insufficient_score };

enum class BoostStatus { bought, already_owned, has_balloon, insufficient_score };

struct BalloonResult {
    BalloonStatus status = BalloonStatus::bought;
    std::int64_t available_score = 0;
    /* How long the balloon just bought cannot be popped, zero for an ordinary one. */
    std::int64_t shield_seconds = 0;
};

struct BoostResult {
    BoostStatus status = BoostStatus::bought;
    std::int64_t available_score = 0;
    std::int64_t multiplier = 0;
};

enum class RaidStatus { started, already_travelling, holding_place, unknown_target, oneself };

struct RaidRules {
    /* Seconds of travel per unit of distance, and the share of the loot: a quarter by default. */
    int travel_divisor = 1000;
    int loot_share = 4;
    /* What a raid the balloon turns back costs the raider. */
    int attack_cost = 0;
    zodiac::Overrides signs;
};

struct RaidResult {
    RaidStatus status = RaidStatus::started;
    /* The spelling the target has on file. */
    std::string target;
    /* Seconds to get there, or still to wait when already on the road. */
    std::int64_t seconds = 0;
    /* oneself: the palle that robbing himself cost him. */
    std::int64_t lost = 0;
};

struct RaidEvent {
    enum class Kind { stolen, defended, returned };

    Kind kind = Kind::stolen;
    std::string raider;
    std::string target;
    std::int64_t loot = 0;
    std::int64_t cost = 0;
    /* The ride home. */
    std::int64_t seconds = 0;
    int raider_percent = 100;
    int target_percent = 100;
    bool balloon_popped = false;
    /* The target was away, so there was nothing to get past. */
    bool undefended = false;
    /* Whether the target is known to be on Telegram, where a mention reaches them. */
    bool target_on_telegram = false;
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
struct ClaimRules {
    /* The penalty a failed attempt leaves behind. */
    int cooldown_seconds = 0;
    /* What an attempt against a balloon that holds costs the attacker. */
    int attack_cost = 0;
    /* Set for a player who pops a shielded balloon on his first attempt, as the owner asked for some. */
    bool ignores_shield = false;
    zodiac::Overrides signs;
};

[[nodiscard]] ClaimResult conquister_claim(
    Storage &storage,
    std::int64_t user_id,
    const std::string &username,
    std::int64_t now,
    const ClaimRules &rules = {}
);
/* limit 0 returns every entry. */
[[nodiscard]] Leaderboard conquister_leaderboard(Storage &storage, std::size_t limit);
/* Case-insensitive lookup; rank is 0 when the user has no score yet. */
[[nodiscard]] std::optional<ConquisterUser> conquister_user(Storage &storage, std::string_view username);
/* One balloon per user: it survives 4 attempts at most, then has to be bought again. */
/* With shield_seconds the balloon cannot be popped until it deflates, instead of lasting until an
   attempt pops it. */
[[nodiscard]] BalloonResult balloon_buy(
    Storage &storage,
    const std::string &username,
    int cost,
    std::int64_t now,
    std::int64_t shield_seconds
);

/* Sends a player to rob another one, if he is at home and the target is somebody the bot knows. */
[[nodiscard]] RaidResult raid_start(
    Storage &storage,
    std::int64_t user_id,
    const std::string &username,
    std::string_view target,
    std::int64_t now,
    const RaidRules &rules
);

/* Settles the raids that have reached the target or come home by now. */
[[nodiscard]] std::vector<RaidEvent> raid_due(Storage &storage, std::int64_t now, const RaidRules &rules);

/* The multiplier is kept until the place is taken from the buyer, and rules out a balloon meanwhile. */
[[nodiscard]] BoostResult boost_buy(
    Storage &storage,
    const std::string &username,
    int cost,
    std::int64_t multiplier,
    std::int64_t now
);

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
