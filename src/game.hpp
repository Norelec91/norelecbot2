#ifndef NORELECBOT_GAME_HPP
#define NORELECBOT_GAME_HPP

#include "storage.hpp"
#include "zodiac.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace norelecbot {

/* Where an emoji with a power does its work. A player keeps his emoji in two places: his house, where
   they stay and work whether he is in or out, and on him, where they go wherever he goes. One of the
   home kind works in the house; one carried works on him, at home, on the road or in @TheConquister37;
   one thrown works nowhere until it is thrown, when it leaves him for good and lands on somebody else.
   Kept where it does not work, an emoji only sits there. */
enum class PowerKind { home, carried, thrown };

/* How many emoji a player can have on him: the house has the rest. */
inline constexpr std::size_t carried_limit = 5;

/* Whether the house of the day and the signs change what a hold or a raid is worth. Switched off: every day
   is worth the same to everybody, and nobody is told about signs. */
inline constexpr bool zodiac_counts = false;

/* What the house of the day makes a hold or a raid worth to a player, in percent: always 100 while the
   zodiac does not count. */
[[nodiscard]] int day_percent(std::string_view username, std::int64_t now, zodiac::Overrides signs = {});

/* An emoji that does something beyond hanging beside a name. */
struct Power {
    std::string_view emoji;
    PowerKind kind;
    /* The moment the copies on the name are counted: nothing hung or burnt afterwards changes it. */
    std::string_view counted;
    std::string_view effect;
    /* Nothing takes it off a name against its owner's will: no explosion, no theft, no newborn. */
    bool untouchable = false;
    /* Other emoji that are this one in all but looks: they do exactly what it does. */
    std::array<std::string_view, 2> also{};
};

namespace power {

inline constexpr Power pleading{"🥺", PowerKind::home, "when a raider reaches the house",
                                "each makes him take a share less"};
inline constexpr Power rocket{"🚀", PowerKind::carried, "when he sets off", "each makes both legs of the ride faster"};
inline constexpr Power bolt{"⚡", PowerKind::carried, "when he enters @TheConquister37",
                            "each makes the hold worth a share more"};
inline constexpr Power lobster{"🦞", PowerKind::carried, "when he enters @TheConquister37 kicking somebody out",
                               "becomes what the kicked holder has on him in the same slot until he leaves"};
inline constexpr Power poo{"💩", PowerKind::thrown, "when it lands", "makes whoever it hits \"lo smerdato\" for a while",
                           false, {"🇷🇺", "🇮🇱"}};
inline constexpr Power bomb{"💣", PowerKind::thrown, "when it lands",
                            "takes one emoji with a power among those that are where it lands"};
inline constexpr Power nuke{"☢️", PowerKind::thrown, "when it lands",
                            "starts over: on @TheConquister37 the whole game, at a house the player who lives there"};
inline constexpr Power dog{"🐶", PowerKind::home, "when a raider reaches the house",
                           "each is a chance of sending him away with nothing"};
inline constexpr Power mailbox{"📮", PowerKind::home, "when something thrown lands on the house",
                               "each is a chance of sending it back to whoever threw it"};
inline constexpr Power ninja{"🥷", PowerKind::carried, "when he reaches a house to rob it, or comes for the place",
                             "each is a chance of slipping past the 🎈 and the 🐶 that guard it"};
inline constexpr Power alarm{"🔊", PowerKind::home, "when a raider with a 🥷 reaches the house",
                             "each takes a share off his chance of slipping past unnoticed"};
inline constexpr Power pirate{"🏴‍☠️", PowerKind::carried, "when a raid of his gets through",
                              "each is a chance of carrying off one emoji that is at the house too"};
inline constexpr Power seed{"💦", PowerKind::thrown, "when it lands on somebody who is there",
                            "in time a child is born on the name of whoever it hits, taking a slot"};
inline constexpr Power hen{"🐔", PowerKind::home, "every minute", "each lays palle for its owner, whether he is in or out"};
inline constexpr Power dino{"🦖", PowerKind::home, "when a raider reaches the house",
                            "each may eat one of the emoji the raider has with him, robbed or not"};
inline constexpr Power salt{"🧂", PowerKind::home, "when the same raider reaches the house again",
                            "each makes him hand over a share of his own palle, if the last time was not long ago"};
inline constexpr Power ice{"🧊", PowerKind::thrown, "when it lands",
                           "freezes whoever it hits for a while: no entering the place, no setting off"};
inline constexpr Power fire{"🔥", PowerKind::carried, "when a 🧊 hits him",
                            "each melts a share of the time he stays frozen"};
inline constexpr Power hourglass{"⏳", PowerKind::carried, "when a 🎈 holds off his attempt at the place",
                                 "each takes a share off the penalty he is left with"};
/* The one that works in either place: on him it guards him wherever he is, kept in the house it guards
   the house even while he is out. */
inline constexpr Power balloon{"🎈", PowerKind::carried, "when somebody tries to get past it",
                               "holds off whoever comes for his place or his house until it pops, then is as good as new",
                               true};

}

/* Every emoji with a power: a new one is a row here, of a declared kind, plus what it does. */
inline constexpr std::array powers{power::pleading, power::rocket, power::bolt, power::lobster,
                                   power::poo,      power::bomb,   power::nuke, power::dog,
                                   power::balloon,  power::mailbox, power::ninja, power::alarm,
                                   power::pirate,   power::seed,    power::hen,
                                   power::ice,      power::fire,    power::hourglass,
                                   power::dino,     power::salt};

/* How long a 🧂 remembers a raider: one who reaches the same house again within it pays the salt. */
inline constexpr std::int64_t salt_seconds = 5 * 60;

/* Whether an emoji is that power's, drawn in colour or not and whatever the tone of its skin. */
[[nodiscard]] bool is_power(std::string_view emoji, const Power &power);
/* The power an emoji has, or nothing for one that only hangs there. */
[[nodiscard]] const Power *power_of(std::string_view emoji);

/* frozen: a 🧊 hit him not long ago. */
enum class ClaimStatus { taken, already_held, defended, cooldown, travelling, frozen };

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
    /* taken: a 🥷 took him past the holder's 🎈, unnoticed and untouched. */
    bool sneaked = false;
    /* taken: what the new hold is worth in percent, set by the ⚡ on his name; 0 without one. */
    std::int64_t entered_lightning = 0;
    /* taken: what the 🦞 on his name became, slot by slot, copying the kicked holder's; empty if none did. */
    std::vector<std::string> lobsters_became;
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

/* child_there: the slot named holds a child, which nothing replaces. */
enum class FurnitureStatus {
    bought,
    full,
    invalid_position,
    already_there,
    insufficient_score,
    not_home,
    child_there
};

struct FurnitureResult {
    FurnitureStatus status = FurnitureStatus::bought;
    std::int64_t available_score = 0;
    /* What he has on him now and what is in his house: the emoji in their slots, "[]" for an empty one in
       between. */
    std::string shown;
    std::string house;
    /* Bought: it went on him, rather than into the house. */
    bool with_him = false;
    /* The slot of the house, from 1, and what was there before, empty if nothing was; 0 when it went on him. */
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
    /* How the house reads now, the emoji that moved, and the one it swapped places with. */
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
    /* Only when an emoji is taken along: he has none like it, the target has no empty slot, or he has no
       free slot on him to carry it in. */
    no_such_emoji,
    no_room,
    hands_full,
    /* A 🧊 hit him not long ago: he cannot set off until he thaws. */
    frozen
};

enum class RaidTargetKind { any, telegram, irc };

enum class BurnStatus { burned, invalid_amount, insufficient_score, travelling };

struct BurnResult {
    BurnStatus status = BurnStatus::burned;
    std::int64_t amount = 0;
    /* What is left once they are gone, or what there was when they were too few. */
    std::int64_t score = 0;
};

struct RaidRules {
    /* How much road buys a palla. */
    int loot_divisor = 50;
    /* Units of distance per second of travel. */
    int travel_divisor = 1000;
    zodiac::Overrides signs;
    /* How many emoji a house can keep: an emoji brought to a full one has nowhere to go. */
    std::size_t furniture_limit = 10;
    /* How long a player hit by a 💩 stays "lo smerdato". */
    std::int64_t smeared_seconds = 86400;
    /* How much faster every 🚀 on the name as he sets off makes the ride, in percent: they add up. */
    std::int64_t rocket_percent = 0;
    /* How much less a raider takes for every 🥺 on the target's name when he gets there, in percent:
       they add up, to all of it at most. */
    std::int64_t pleading_percent = 0;
    /* How often, in percent, a 💣 is a dud that goes off in the thrower's hand. */
    std::int64_t bomb_dud_percent = 0;
    /* The chance, in percent, that each 🐶 on the target's name gives of stopping a raid: they add up. */
    std::int64_t dog_percent = 0;
    /* The chance, in percent, that each 📮 on the target's name gives of sending back what is thrown at
       the house: they add up. */
    std::int64_t mailbox_percent = 0;
    /* The chance, in percent, that each 🥷 the raider has with him gives of slipping past the 🎈 and the
       🐶 at the house: they add up. */
    std::int64_t ninja_percent = 0;
    /* What each 🔊 at the house takes off that chance, in percent points: they add up. */
    std::int64_t alarm_percent = 0;
    /* The chance, in percent, that each 🏴‍☠️ the raider has with him gives of carrying off an emoji from
       the house he robs: they add up. */
    std::int64_t pirate_percent = 0;
    /* How long after a 💦 lands the child is born. */
    std::int64_t pregnancy_seconds = 9 * 60 * 60;
    /* How long a child stays in each of its four ages before it moves on, and at last leaves. */
    std::int64_t child_stage_seconds = 24 * 60 * 60;
    /* The palle each 🐔 on a name lays for its owner every minute. */
    std::int64_t hen_per_minute = 0;
    /* The palle a child makes for the name it lives on every second of its grown-up age. */
    std::int64_t adult_per_second = 0;
    /* The chance, in percent, that a grown-up girl and a grown-up boy of the same house have a child. */
    std::int64_t mating_percent = 0;
    /* The chance, in percent, that each 🦖 at the house gives of eating one of the emoji the raider has
       with him: they add up. */
    std::int64_t dino_percent = 0;
    /* The share of his own palle each 🧂 at the house makes a raider hand over to the target when he
       reaches him again within salt_seconds of the last time, in percent: they add up, to all of them at
       most. */
    std::int64_t salt_percent = 0;
    /* How long a player hit by a 🧊 stays frozen. */
    std::int64_t frozen_seconds = 300;
    /* How much of the time a 🧊 freezes him each 🔥 he has with him melts away, in percent: they add up. */
    std::int64_t fire_percent = 0;
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
    /* gone: a child that lived all its ages leaves the target's name; gift_emoji is what it was last.
       born: a child conceived by a 💦 comes into the world. The raider is the father, the target the
       mother; with no raider its parents are two grown-up children of the target's own house; gift_emoji is the child, blown what it took the slot of when the name was full. */
    enum class Kind { stolen, delivered, returned, born, gone };

    Kind kind = Kind::stolen;
    std::string raider;
    std::string target;
    std::int64_t loot = 0;
    /* stolen: a 🐶 at the house caught the raider, who takes nothing. */
    bool intercepted = false;
    /* stolen: a 🥷 took the raider past the 🎈 or the 🐶 that were there, unnoticed. */
    bool sneaked = false;
    /* stolen: his 🥷 would have done it, but a 🔊 at the house gave him away. */
    bool alarmed = false;
    /* stolen: the emoji his 🏴‍☠️ carried off from the house and onto his own name; empty when none. */
    std::string boarded{};
    /* stolen: the emoji he had with him that a 🦖 at the house ate; empty when none. */
    std::string eaten{};
    /* stolen: what the 🧂 of the house made him hand over, for coming again so soon; 0 when nothing. */
    std::int64_t salted = 0;
    /* stolen: what the 🥺 on the target's name talked the raider out of, and by how much in percent. */
    std::int64_t spared = 0;
    std::int64_t pleaded_percent = 0;
    /* The palle carried from home: handed to the target on delivery, brought back on a turnaround. */
    std::int64_t gift = 0;
    /* The emoji carried from home: hung on the target on delivery, or brought back. no_room: the
       target's name was full on arrival. */
    std::string gift_emoji;
    bool no_room = false;
    /* delivered: the emoji was a present, hung as it is, even one that is meant to be thrown. */
    bool intact = false;
    /* delivered: the present went on the target, who was home with room for it, rather than in his house. */
    bool gift_with_him = false;
    /* delivered, with a 💣: the emoji it took off the target's name, empty when it found nothing to take. */
    std::vector<std::string> blown{};
    /* The 💣 was a dud: what it took is the raider's own, among what he had with him. */
    bool backfired = false;
    /* delivered, with a ☢️: the target starts over. */
    bool reset = false;
    /* delivered, with a 💦: a child is on the way, born after this many seconds. */
    std::int64_t expecting = 0;
    /* delivered, with a 🧊: whoever it hit is frozen for this many seconds; melted, his 🔥 shortened it,
       to nothing when froze is 0. */
    std::int64_t froze = 0;
    bool melted = false;
    /* delivered, with a 💦: nobody was home to receive it, and the raider takes it back with him. */
    bool nobody_home = false;
    /* delivered, with something thrown: a 📮 sent it back, and what it did it did to the raider, at his
       own house. */
    bool sent_back = false;
    /* What the two have on them, shown beside their names. */
    std::string raider_emoji;
    std::string target_emoji;
    /* Whether each of the two is "lo smerdato" right now: a delivered 💩 makes the target one. */
    bool raider_smeared = false;
    bool target_smeared = false;
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
    /* The chance, in percent, that each 🥷 on the claimer's name gives of slipping past the holder's 🎈. */
    std::int64_t ninja = 0;
    /* How much of the penalty each ⏳ on the claimer's name takes off, in percent: they add up. */
    std::int64_t hourglass = 0;
};

[[nodiscard]] ClaimResult conquister_claim(
    Storage &storage,
    std::int64_t user_id,
    const std::string &username,
    std::int64_t now,
    const ClaimRules &rules = {}
);
/* Where a player is right now. Away from home is either of the other two: on the road to somebody and
   back, or in @TheConquister37, where the ride takes no time and he stays until he leaves or is kicked
   out. Away, his house has no balloon and what he carries is with him. */
enum class Whereabouts { home, conquister, road };

/* Everything the group can know about one player. */
struct Profile {
    std::string name;
    /* What he has on him, shown beside his name, and what is in his house. */
    std::string furniture;
    std::string house;
    /* Hit by a 💩 not long ago: he is "lo smerdato". */
    bool smeared = false;
    /* How many 🐔 lay for him, in his house. */
    std::int64_t hens = 0;
    /* Seconds until he thaws after a 🧊; 0 when he is not frozen. */
    std::int64_t frozen_for = 0;
    /* Whether he plays from Telegram, where his home is written with the mention. */
    bool on_telegram = false;
    std::int64_t score = 0;
    /* 0 when he has no palle on file yet, out of how many players are in the ranking. */
    std::size_t rank = 0;
    std::size_t players = 0;
    std::int64_t quotes_added = 0;
    Whereabouts place = Whereabouts::home;
    /* conquister: since when, and what the hold is worth in percent (0 without ⚡). */
    std::int64_t since = 0;
    std::int64_t lightning_percent = 0;
    /* road: the player he rides to or back from, whether he is already coming back, and when he is home. */
    std::string heading;
    bool returning = false;
    std::int64_t home_in = 0;
};

/* A player named as on that platform; nothing for a name nobody plays under. */
[[nodiscard]] std::optional<Profile> player_profile(Storage &storage, std::string_view name, RaidTargetKind platform,
                                                   std::int64_t now);
/* The player behind an internal key, even one with nothing on file yet. */
[[nodiscard]] Profile player_profile_of(Storage &storage, const std::string &key, std::int64_t now);
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
                                      std::string_view account_name = {},
                                      /* A player seen for the first time is handed his 🎈, if it fits. */
                                      std::size_t furniture_limit = 10);
/* Everybody starts with a 🎈: hands one, once, to every known player who has none and a free slot. */
void balloons_hand_out(Storage &storage, std::size_t furniture_limit);
/* Sorts what saves from before had on a single name: the 🎈 and what works on him go on him, as many as
   fit, in their order; the rest stays in the house, each in its slot. Once, after a backup. */
void equipment_sort_out(Storage &storage);
enum class LinkStatus { pending, linked, unknown_account, conflict, self, already_linked };
[[nodiscard]] LinkStatus player_link(Storage &storage, std::int64_t user_id, const std::string &username,
                                     std::string_view other_name, std::string_view account_name = {});

/* The debug switch frees one person only: his purchases are free, everyone else pays. */
void debug_set(Storage &storage, const std::string &username, bool wanted);
[[nodiscard]] bool debug_on(Storage &storage, const std::string &username);

/* A house, or what is on a player, slot by slot: an emoji, or an empty string where "[]" marks an empty slot. */
[[nodiscard]] std::vector<std::string> furniture_slots(std::string_view stored);
/* The slots back into what is saved and shown: "[]" for an empty one, none after the last emoji. */
[[nodiscard]] std::string furniture_stored(std::vector<std::string> slots);

/* Buys one emoji. Named a slot of the house, from 1, it goes there, over what was there. Otherwise one that
   works on him goes on him if he has room, and everything else in the first empty slot of the house.
   Only at home. The price is the base cost grown by the inflation percent for every copy of that
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
/* Moves the emoji in one slot of the house to another, both from 1, swapping it with whatever is there.
   Free, and only at home. */
[[nodiscard]] FurnitureMoveResult furniture_move(Storage &storage, const std::string &username,
                                                 std::int64_t from, std::int64_t to, std::size_t limit,
                                                 std::int64_t now, zodiac::Overrides signs = {});
/* What everybody has on him, for whoever only has names to write. */
[[nodiscard]] Authors furniture_all(Storage &storage);

/* Palle brought back to @TheConquister37 leave the game: nobody receives them. Not from the road. */
[[nodiscard]] BurnResult palle_burn(Storage &storage, const std::string &player, std::int64_t amount);
/* child: what he named is a child of his, which stays until it leaves by itself. */
enum class FurnitureBurnStatus { burned, not_owned, travelling, child };

struct FurnitureBurnResult {
    FurnitureBurnStatus status = FurnitureBurnStatus::burned;
    /* What he has on him once it is gone. */
    std::string shown;
    /* What is thrown at the place lands on whoever holds it, unless that is the thrower himself: who
       he is, how his name reads afterwards and, for a 💣, the emoji it took from the ones he carries. */
    std::string hit;
    bool hit_on_telegram = false;
    std::string hit_furniture;
    std::vector<std::string> blown{};
    /* The 💣 was a dud: what it took is the thrower's own, among what he had with him. */
    bool backfired = false;
    /* It was a ☢️: the game started over. */
    bool reset = false;
    /* It was a 💦: the holder is expecting, and the child is born after this many seconds. */
    std::int64_t expecting = 0;
    /* It was a 🧊: the holder is frozen for this many seconds; melted, his 🔥 shortened it, to nothing
       when froze is 0. */
    std::int64_t froze = 0;
    bool melted = false;
};

/* An emoji brought back to @TheConquister37 leaves the game too: the first slot that holds it is
   emptied, on him before the house. Not from the road. One that is thrown lands on the holder instead: a 💩 makes him "lo
   smerdato", a 💣 takes the emoji he carries. Destroyed, it only goes up in smoke, whatever it is. */
[[nodiscard]] FurnitureBurnResult furniture_burn(Storage &storage, const std::string &player,
                                                 const std::string &emoji, std::int64_t now = 0,
                                                 const RaidRules &rules = {}, bool destroy = false);
/* The players who are "lo smerdato" right now. */
[[nodiscard]] std::vector<std::string> smeared_all(Storage &storage, std::int64_t now);

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
    /* An emoji of his own to hang on the target on arrival instead of robbing him: it travels on him,
       taken from the house if it is not on him already. */
    std::string_view gift_emoji = {},
    /* The emoji is given, not thrown: it is hung as it is, even one that would land on him. */
    bool intact = false
);

/* Settles the raids that have reached the target or come home by now. */
[[nodiscard]] std::vector<RaidEvent> raid_due(Storage &storage, std::int64_t now, const RaidRules &rules);

/* not_owned: none in the house, already_on: it is on him already, child: children stay in the house,
   full: he has no room on him and named nothing to put back, swap_missing: what he named is not on him.
   stored: put back in the house; not_on: none on him, house_full: no room in the house for it. */
enum class GearStatus { taken, swapped, stored, not_home, not_owned, already_on, child, full, swap_missing, not_on,
                        house_full };

struct GearResult {
    GearStatus status = GearStatus::taken;
    /* What he has on him and what is in the house, once done. */
    std::string shown;
    std::string house;
    /* swapped: what went back in the house to make room. */
    std::string swapped;
    Departure departure;
};

/* Takes an emoji from the house and puts it on him; with no room on him, the one named as swap goes
   back in the house in its place. Only at home. */
[[nodiscard]] GearResult gear_take(Storage &storage, const std::string &player, const std::string &emoji,
                                   const std::string &swap, std::int64_t now, zodiac::Overrides signs = {});
/* Puts an emoji he has on him back in the house, in its first empty slot. Only at home. */
[[nodiscard]] GearResult gear_store(Storage &storage, const std::string &player, const std::string &emoji,
                                    std::size_t limit, std::int64_t now, zodiac::Overrides signs = {});

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
