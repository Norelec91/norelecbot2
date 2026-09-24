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
    std::string previous_key;
    /* Their Telegram id, zero when they played from IRC: only a Telegram name may be written as a mention. */
    std::int64_t previous_user_id = 0;
    std::int64_t earned = 0;
    /* taken: getting in popped a balloon. defended: the percentage the next attempt will have. */
    bool balloon_popped = false;
    /* A successful claim grants a temporary balloon unless a boost is active. */
    bool balloon_active = false;
    int next_chance = 0;
    /* cooldown: seconds still to wait. defended: the penalty just handed out. */
    std::int64_t penalty_seconds = 0;
    /* defended: the palle the failed attempt cost, never more than the attacker had. */
    std::int64_t attack_cost = 0;
    /* travelling: how long before the claimer is home again. */
    std::int64_t travel_seconds = 0;
    /* taken: the multiplier the kicked holder had bought, zero when there was none. */
    std::int64_t boost_multiplier = 0;
    /* taken: what the house of the day was worth to the kicked holder, 100 when it was indifferent. */
    int zodiac_percent = 100;
};

enum class BoostStatus { bought, already_owned, holding_place, insufficient_score };

enum class FurnitureStatus { bought, too_many, insufficient_score };

struct FurnitureResult {
    FurnitureStatus status = FurnitureStatus::bought;
    std::int64_t available_score = 0;
    /* How the name reads now, and how many pieces hang from it. */
    std::string shown;
    std::size_t howmany = 0;
};

struct BoostResult {
    BoostStatus status = BoostStatus::bought;
    std::int64_t available_score = 0;
    std::int64_t multiplier = 0;
};

enum class RaidStatus {
    started,
    already_travelling,
    holding_place,
    unknown_target,
    left_place,
    coming_home,
    home_already,
    /* Only when palle are taken along: not enough of them, or a number that makes no sense. */
    insufficient_score,
    invalid_amount
};

enum class RaidTargetKind { any, telegram, irc };

enum class BurnStatus { burned, invalid_amount, insufficient_score };

struct BurnResult {
    BurnStatus status = BurnStatus::burned;
    std::int64_t amount = 0;
    /* What is left once they are gone, or what there was when they were too few. */
    std::int64_t score = 0;
};

enum class InvestmentStatus { deposited, withdrawn, not_self, not_home, insufficient_score,
                              no_investment, invalid_amount, balance_limit };

struct InvestmentResult {
    InvestmentStatus status = InvestmentStatus::no_investment;
    std::int64_t amount = 0;
    std::int64_t interest = 0;
    std::int64_t score = 0;
    int zodiac_percent = 100;
    int daily_rate = 0;
};

struct RaidRules {
    /* How much road buys a palla. */
    int loot_divisor = 50;
    /* At most this fraction of what the target owns is taken; zero means all of it. */
    int loot_share = 3;
    /* Seconds of travel per unit of distance, and the share of the loot: a quarter by default. */
    int travel_divisor = 1000;
    zodiac::Overrides signs;
};

struct RaidResult {
    RaidStatus status = RaidStatus::started;
    /* The spelling the target has on file. */
    std::string target;
    /* Seconds to get there, or still to wait when already on the road. */
    std::int64_t seconds = 0;
    /* left_place: what the hold he just gave up was worth, and what made it worth that. */
    std::int64_t earned = 0;
    std::int64_t boost_multiplier = 0;
    int zodiac_percent = 100;
    /* What is left after the palle taken along were picked up, or what there was when they were too few. */
    std::int64_t score = 0;
};

struct RaidEvent {
    enum class Kind { stolen, delivered, returned };

    Kind kind = Kind::stolen;
    std::string raider;
    std::string target;
    std::int64_t loot = 0;
    /* The palle carried from home: handed to the target on delivery, brought back on a turnaround. */
    std::int64_t gift = 0;
    /* The furniture hung beside the two names, to be shown along with them. */
    std::string raider_emoji;
    std::string target_emoji;
    /* The road between the two, which is also what can be carried off. */
    std::int64_t distance = 0;
    /* The ride home. */
    std::int64_t seconds = 0;
    int raider_percent = 100;
    int target_percent = 100;
    /* The target's raid resistance absorbed part of the remaining loot. */
    std::int64_t resistance_absorbed = 0;
    /* The target was away, so there was nothing to get past. */
    bool undefended = false;
    /* Whether the target is known to be on Telegram, where a mention reaches them. */
    bool target_on_telegram = false;
};

struct LeaderboardEntry {
    std::string username;
    std::string player_key;
    std::int64_t score = 0;
    std::int64_t quotes_added = 0;
};

struct Leaderboard {
    std::vector<LeaderboardEntry> entries;
    std::optional<Holder> current;
    std::string current_key;
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
    /* Who added each of them, empty for the ones added before the bot wrote it down. */
    std::vector<std::string> authors;
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
[[nodiscard]] std::optional<ConquisterUser> conquister_user(Storage &storage, std::string_view username,
                                                           RaidTargetKind platform = RaidTargetKind::any);
/* Bind a verified platform account to its player, recording its current public name. */
[[nodiscard]] std::string player_seen(Storage &storage, std::int64_t user_id, const std::string &username,
                                      std::string_view account_name = {});
enum class LinkStatus { pending, linked, unknown_account, conflict, self, already_linked };
[[nodiscard]] LinkStatus player_link(Storage &storage, std::int64_t user_id, const std::string &username,
                                     std::string_view other_name, std::string_view account_name = {});
/* What is going around, so the prices can keep up with it. */
struct Wealth {
    std::int64_t total = 0;
    /* The median, not the average: one player sitting on the seat all day should not set the
       prices for everybody else. */
    std::int64_t middle = 0;
    std::size_t players = 0;
};

[[nodiscard]] Wealth wealth_now(Storage &storage);

/* The debug switch frees one person only: his purchases are free, everyone else pays. */
void debug_set(Storage &storage, const std::string &username, bool wanted);
[[nodiscard]] bool debug_on(Storage &storage, const std::string &username);

/* Hangs the bought emoji on the name, if they fit and the palle are enough. */
[[nodiscard]] FurnitureResult furniture_buy(
    Storage &storage,
    const std::string &username,
    const std::string &emoji,
    int cost,
    std::size_t limit
);
/* Everybody's emoji, for whoever only has names to write. */
[[nodiscard]] Authors furniture_all(Storage &storage);

/* Palle brought back to @TheConquister37 leave the game: nobody receives them. */
[[nodiscard]] BurnResult palle_burn(Storage &storage, const std::string &player, std::int64_t amount);

/* Sends a player to rob another one, if he is at home and the target is somebody the bot knows.
   Naming himself sends him home instead: at once from @TheConquister37, at the end of the ride if he
   is on the road. */
[[nodiscard]] RaidResult raid_start(
    Storage &storage,
    std::int64_t user_id,
    const std::string &username,
    std::string_view target,
    std::int64_t now,
    const RaidRules &rules,
    RaidTargetKind target_kind = RaidTargetKind::any,
    /* Palle to hand over on arrival instead of robbing the target. */
    std::int64_t gift = 0
);

/* Funds leave the stealable score until withdrawn while the owner is home. */
[[nodiscard]] InvestmentResult investment_deposit(Storage &storage, const std::string &player,
    std::string_view target, RaidTargetKind platform, std::int64_t amount, std::int64_t now,
    zodiac::Overrides signs = {});
[[nodiscard]] InvestmentResult investment_withdraw(Storage &storage, const std::string &player,
    std::string_view target, RaidTargetKind platform, std::int64_t now,
    zodiac::Overrides signs = {});

/* Settles the raids that have reached the target or come home by now. */
[[nodiscard]] std::vector<RaidEvent> raid_due(Storage &storage, std::int64_t now, const RaidRules &rules);

/* The multiplier is kept until the place is taken from the buyer, and rules out a balloon meanwhile. */
[[nodiscard]] BoostResult boost_buy(
    Storage &storage,
    const std::string &username,
    int cost,
    std::int64_t multiplier
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
