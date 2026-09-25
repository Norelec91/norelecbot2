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
    /* taken: what the new hold is worth in percent, fixed by the ⚡ on his name; 0 without one. */
    std::int64_t entered_lightning = 0;
    int next_chance = 0;
    /* cooldown: seconds still to wait. defended: the penalty just handed out. */
    std::int64_t penalty_seconds = 0;
    /* travelling: how long before the claimer is home again. */
    std::int64_t travel_seconds = 0;
    /* taken: what the kicked holder's ⚡ made the hold worth in percent, zero when there was none. */
    std::int64_t lightning = 0;
    /* taken: what the house of the day was worth to the kicked holder, 100 when it was indifferent. */
    int zodiac_percent = 100;
};

/* A line meant for home, written from @TheConquister37, takes him home first: what the hold he gave
   up was worth, and what made it worth that. */
struct Departure {
    bool left = false;
    std::int64_t earned = 0;
    std::int64_t lightning = 0;
    int zodiac_percent = 100;
};

enum class FurnitureStatus { bought, full, invalid_position, already_there, insufficient_score, not_home };

struct FurnitureResult {
    FurnitureStatus status = FurnitureStatus::bought;
    std::int64_t available_score = 0;
    /* How the name reads now: the emoji in their slots, "[]" for an empty one in between. */
    std::string shown;
    /* The slot, from 1, and what was hanging there before, empty if nothing was. */
    std::size_t position = 0;
    std::string replaced;
    /* How many of that emoji already hung from anybody's name, and what it cost for that. */
    std::size_t copies = 0;
    std::int64_t charged = 0;
    Departure departure;
};

enum class FurnitureMoveStatus { moved, swapped, empty_slot, invalid_position, same_position, not_home };

struct FurnitureMoveResult {
    FurnitureMoveStatus status = FurnitureMoveStatus::moved;
    /* How the name reads now, the emoji that moved, and the one it swapped places with. */
    std::string shown;
    std::string moved;
    std::string swapped;
    Departure departure;
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
    invalid_amount,
    /* Only when an emoji is taken along: he has none like it, or the target has no empty slot. */
    no_such_emoji,
    no_room
};

enum class RaidTargetKind { any, telegram, irc };

enum class BurnStatus { burned, invalid_amount, insufficient_score, travelling };

struct BurnResult {
    BurnStatus status = BurnStatus::burned;
    std::int64_t amount = 0;
    /* What is left once they are gone, or what there was when they were too few. */
    std::int64_t score = 0;
};

enum class InvestmentStatus { deposited, withdrawn, not_self, not_home, insufficient_score,
                              no_investment, invalid_amount, balance_limit, locked };

struct InvestmentResult {
    InvestmentStatus status = InvestmentStatus::no_investment;
    std::int64_t amount = 0;
    std::int64_t interest = 0;
    std::int64_t score = 0;
    int zodiac_percent = 100;
    int daily_rate = 0;
    Departure departure;
    /* Deposits still inside their lock: the palle put in, and how long until the first one frees. */
    std::int64_t still_locked = 0;
    std::int64_t unlock_in = 0;
};

struct RaidRules {
    /* How much road buys a palla. */
    int loot_divisor = 50;
    /* Units of distance per second of travel. */
    int travel_divisor = 1000;
    zodiac::Overrides signs;
    /* How many emoji a name can carry: an emoji brought to a full name has nowhere to go. */
    std::size_t furniture_limit = 10;
};

struct RaidResult {
    RaidStatus status = RaidStatus::started;
    /* The spelling the target has on file. */
    std::string target;
    /* Seconds to get there, or still to wait when already on the road. */
    std::int64_t seconds = 0;
    /* left_place: what the hold he just gave up was worth, and what made it worth that. */
    std::int64_t earned = 0;
    std::int64_t lightning = 0;
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
    /* The emoji carried from home: hung on the target on delivery, or brought back. no_room: the
       target's name was full on arrival. */
    std::string gift_emoji;
    bool no_room = false;
    /* The furniture hung beside the two names, to be shown along with them. */
    std::string raider_emoji;
    std::string target_emoji;
    /* The road between the two, which is also what can be carried off. */
    std::int64_t distance = 0;
    /* The ride home. */
    std::int64_t seconds = 0;
    int raider_percent = 100;
    int target_percent = 100;
    /* The target was away, so there was nothing to get past. */
    bool undefended = false;
    /* The target was home with his balloon: it held and nothing was taken, with the chance the next
       raid has of popping it, or it popped and the raid went on. */
    bool balloon_held = false;
    bool balloon_popped = false;
    int next_chance = 0;
    /* Whether the target is known to be on Telegram, where a mention reaches them. */
    bool target_on_telegram = false;
    /* The same for the raider, whose planet is named after him. */
    bool raider_on_telegram = false;
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
    zodiac::Overrides signs;
    /* What every ⚡ on the claimer's name adds to the hold, in percent: they add up. */
    std::int64_t lightning = 0;
};

[[nodiscard]] ClaimResult conquister_claim(
    Storage &storage,
    std::int64_t user_id,
    const std::string &username,
    std::int64_t now,
    const ClaimRules &rules = {}
);
/* Where a player is right now. */
enum class ProfilePlace { home, conquister, road };

/* Everything the group can know about one player. */
struct Profile {
    std::string name;
    std::string furniture;
    /* Whether he plays from Telegram, where his home is written with the mention. */
    bool on_telegram = false;
    std::int64_t score = 0;
    /* 0 when he has no palle on file yet, out of how many players are in the ranking. */
    std::size_t rank = 0;
    std::size_t players = 0;
    std::int64_t quotes_added = 0;
    ProfilePlace place = ProfilePlace::home;
    /* conquister: since when, and what the hold is worth in percent (0 without ⚡). */
    std::int64_t since = 0;
    std::int64_t lightning_percent = 0;
    /* road: the player he rides to or back from, whether he is already coming back, and when he is home. */
    std::string heading;
    bool returning = false;
    std::int64_t home_in = 0;
    /* What he put in the bank, and what it is worth now. */
    std::int64_t invested = 0;
    std::int64_t investment_value = 0;
};

/* A player named as on that platform; nothing for a name nobody plays under. */
[[nodiscard]] std::optional<Profile> player_profile(Storage &storage, std::string_view name, RaidTargetKind platform,
                                                   std::int64_t now, zodiac::Overrides signs = {},
                                                   std::int64_t lightning = 0);
/* The player behind an internal key, even one with nothing on file yet. */
[[nodiscard]] Profile player_profile_of(Storage &storage, const std::string &key, std::int64_t now,
                                        zodiac::Overrides signs = {}, std::int64_t lightning = 0);
/* limit 0 returns every entry. */
[[nodiscard]] Leaderboard conquister_leaderboard(Storage &storage, std::size_t limit);
/* Case-insensitive lookup; rank is 0 when the user has no score yet. */
[[nodiscard]] std::optional<ConquisterUser> conquister_user(Storage &storage, std::string_view username,
                                                           RaidTargetKind platform = RaidTargetKind::any);
/* Seconds until a traveller already on his way back is home; nothing when he is not coming back yet. */
[[nodiscard]] std::optional<std::int64_t> returning_in(Storage &storage, const std::string &player, std::int64_t now);
/* Whether a name, as written on that platform, is this very player. */
[[nodiscard]] bool names_player(Storage &storage, const std::string &player, std::string_view name,
                                RaidTargetKind platform);
/* Bind a verified platform account to its player, recording its current public name. */
[[nodiscard]] std::string player_seen(Storage &storage, std::int64_t user_id, const std::string &username,
                                      std::string_view account_name = {});
enum class LinkStatus { pending, linked, unknown_account, conflict, self, already_linked };
[[nodiscard]] LinkStatus player_link(Storage &storage, std::int64_t user_id, const std::string &username,
                                     std::string_view other_name, std::string_view account_name = {});

/* The debug switch frees one person only: his purchases are free, everyone else pays. */
void debug_set(Storage &storage, const std::string &username, bool wanted);
[[nodiscard]] bool debug_on(Storage &storage, const std::string &username);

/* A name's furniture slot by slot: an emoji, or an empty string where "[]" marks an empty slot. */
[[nodiscard]] std::vector<std::string> furniture_slots(std::string_view stored);
/* The slots back into what is saved and shown: "[]" for an empty one, none after the last emoji. */
[[nodiscard]] std::string furniture_stored(std::vector<std::string> slots);

/* Hangs one emoji in a slot, from 1, overwriting what was there; position 0 takes the first empty
   one. Only at home. The price is the base cost grown by the inflation percent for every copy of that
   emoji already in the game: 100 doubles it each time. */
[[nodiscard]] FurnitureResult furniture_buy(
    Storage &storage,
    const std::string &username,
    const std::string &emoji,
    std::int64_t position,
    std::int64_t cost,
    std::size_t limit,
    std::int64_t now,
    zodiac::Overrides signs = {},
    std::int64_t inflation = 100
);
/* Moves the emoji in one slot to another, both from 1, swapping it with whatever hangs there. Free,
   and only at home. */
[[nodiscard]] FurnitureMoveResult furniture_move(Storage &storage, const std::string &username,
                                                 std::int64_t from, std::int64_t to, std::size_t limit,
                                                 std::int64_t now, zodiac::Overrides signs = {});
/* Everybody's emoji, for whoever only has names to write. */
[[nodiscard]] Authors furniture_all(Storage &storage);

/* Palle brought back to @TheConquister37 leave the game: nobody receives them. Not from the road. */
[[nodiscard]] BurnResult palle_burn(Storage &storage, const std::string &player, std::int64_t amount);
enum class FurnitureBurnStatus { burned, not_owned, travelling };

struct FurnitureBurnResult {
    FurnitureBurnStatus status = FurnitureBurnStatus::burned;
    /* How his name reads once it is gone. */
    std::string shown;
};

/* An emoji brought back to @TheConquister37 leaves the game too: the first slot that holds it is
   emptied. Not from the road. */
[[nodiscard]] FurnitureBurnResult furniture_burn(Storage &storage, const std::string &player,
                                                 const std::string &emoji, std::int64_t now);

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
    std::int64_t gift = 0,
    /* An emoji of his own to hang on the target on arrival instead of robbing him. */
    std::string_view gift_emoji = {}
);

/* Funds leave the stealable score until withdrawn while the owner is home. From @TheConquister37 both
   take him home first, paying what the hold earned; a withdrawal only when there is something to
   withdraw. */
[[nodiscard]] InvestmentResult investment_deposit(Storage &storage, const std::string &player,
    std::string_view target, RaidTargetKind platform, std::int64_t amount, std::int64_t now,
    zodiac::Overrides signs = {}, std::int64_t lightning = 0);
/* Only deposits older than lock_seconds come out; the younger ones stay in the bank. */
[[nodiscard]] InvestmentResult investment_withdraw(Storage &storage, const std::string &player,
    std::string_view target, RaidTargetKind platform, std::int64_t now,
    zodiac::Overrides signs = {}, std::int64_t lightning = 0, std::int64_t lock_seconds = 0);

/* Settles the raids that have reached the target or come home by now. */
[[nodiscard]] std::vector<RaidEvent> raid_due(Storage &storage, std::int64_t now, const RaidRules &rules);

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
