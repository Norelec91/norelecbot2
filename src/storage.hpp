#ifndef NORELECBOT_STORAGE_HPP
#define NORELECBOT_STORAGE_HPP

#include <nlohmann/json.hpp>

#include <cstddef>
#include <cstdint>
#include <map>
#include <mutex>
#include <optional>
#include <random>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace norelecbot {

/* Keeps object keys in insertion order, like the files and responses have always had. */
using Json = nlohmann::ordered_json;

/* The cause, when known, is logged where the error is thrown. */
class StorageError : public std::runtime_error {
public:
    using std::runtime_error::runtime_error;
};

struct Holder {
    std::int64_t user_id = 0;
    std::string username;
    std::int64_t since = 0;
    /* What this hold is worth in percent, set on the way in by the ⚡ he had on his name; 0 for nothing.
       A ⚡ hung later does not raise it; one lost while he is inside lowers it from then on. */
    std::int64_t lightning_percent = 0;
    /* How many ⚡ that percent stands for, so that a lost one takes its share with it. */
    std::int64_t bolts = 0;
    /* What the hold earned up to the instant a ⚡ was lost, and that instant: the rest is counted from
       there at the lower percent. */
    std::int64_t banked = 0;
    std::int64_t counted_from = 0;
    /* The 🦞 on his name, by slot from 0, that came in as the emoji the kicked holder had in that same
       slot: they count and show as that emoji until he leaves. */
    std::map<std::size_t, std::string> lobsters{};

    bool operator==(const Holder &) const = default;
};

/* Usernames in file order. */
using Counters = nlohmann::ordered_map<std::string, std::int64_t>;
/* Quotes mapped to whoever added them, in file order. */
using Authors = nlohmann::ordered_map<std::string, std::string>;
/* Players mapped to slots of their names, from 0, in file order. */
using Slots = nlohmann::ordered_map<std::string, std::vector<std::int64_t>>;


/* A player away from home, robbing another one. */
struct Raid {
    std::string raider;
    std::string target;
    /* When the raider reaches the target, and when he is back home. */
    std::int64_t arrive = 0;
    std::int64_t back = 0;
    bool arrived = false;
    /* What he is carrying home, set when he arrives. */
    std::int64_t loot = 0;
    /* Palle taken along for the target, handed over on arrival and carried back if he turns around. */
    std::int64_t gift = 0;
    /* An emoji taken off his own name for the target, handed over like the palle. */
    std::string gift_emoji;
    /* The emoji is a present: hung on the target as it is, even one that is meant to be thrown. */
    bool intact = false;

    bool operator==(const Raid &) const = default;
};

/* A child on the way: the player a 💦 landed on, whoever threw it, and when it is born. With no father
   named it is the child of two grown-up children of the house itself. */
struct Pregnancy {
    std::string mother;
    std::string father;
    std::int64_t due = 0;

    bool operator==(const Pregnancy &) const = default;
};

/* A child born of a 💦, in a slot of its mother's name, from 0: it grows there, stage by stage, and
   nothing can take it away before it leaves by itself. */
struct Child {
    std::string owner;
    std::int64_t slot = 0;
    bool male = false;
    std::int64_t born = 0;
    /* Up to when its grown-up age has been paid to the owner; 0 before the first time. */
    std::int64_t paid = 0;
    /* A grown-up girl who already had her chance with a grown-up boy of the same house. */
    bool courted = false;

    bool operator==(const Child &) const = default;
};

/* A raid that reached the house: who came for whom, and when. Kept only as long as a 🧂 can still
   remember it. */
struct Knock {
    std::string raider;
    std::string target;
    std::int64_t at = 0;

    bool operator==(const Knock &) const = default;
};

struct ConquisterState {
    std::optional<Holder> current;
    Counters scores;
    Counters quotes_added;
    /* The attempts each player's 🎈 already survived, where it guards him: @TheConquister37 while he
       holds it, his home while he is there. A fresh one has no entry, and one that pops is fresh again. */
    Counters balloons;
    /* Users mapped to the instant their claim penalty expires. */
    Counters cooldowns;
    /* Where each player lives: an id of ours, drawn once, which spells out a point on the map. */
    Counters ids;
    /* Players known to be on Telegram, mapped to their id there, so a message can reach them. */
    Counters telegram_ids;
    /* Players seen on IRC, independently of whether they also play on Telegram. */
    Counters irc_names;
    /* Verified platform account -> internal player key. Never infer a cross-platform link by name. */
    Authors accounts;
    /* Internal player key -> most recently seen public name. */
    Authors display_names;
    /* Current public names used to resolve platform-specific raid targets. */
    Authors telegram_names;
    Authors irc_nicks;
    /* Both authenticated accounts must request each other before they share a player. */
    Authors link_requests;
    /* The raids under way, in the order they left. */
    std::vector<Raid> raids;
    /* Who added each quote, for the ones added since the bot started writing it down. */
    Authors quote_authors;
    /* The emoji each player bought to hang beside his name. */
    Authors furniture;
    /* Players who turned the debug switch on for themselves: their purchases are free. */
    Counters debugging;
    /* Players hit by a thrown 💩, mapped to the instant they stop being "lo smerdato". */
    Counters smeared;
    /* The slots filled while the player was away: what was hung there is at home, even of a kind he
       would carry, until he next leaves from home. */
    Slots stayed;
    /* The players who were handed the 🎈 everybody starts with: nobody gets it twice. */
    Counters welcomed;
    /* The players a 🧊 froze, mapped to the instant they thaw: until then no place and no leaving. */
    Counters frozen;
    /* The children on the way, in the order they were conceived. */
    std::vector<Pregnancy> pregnancies;
    std::vector<Child> children;
    /* Up to when the 🐔 have been paid for their eggs; 0 before the first time. */
    std::int64_t eggs_at = 0;
    /* The raids that reached a house lately and were not paid for with salt yet, oldest first. */
    std::vector<Knock> knocks{};

    bool operator==(const ConquisterState &) const = default;
};

using Quotes = std::vector<std::string>;

class Storage;

/* Loads each document on first use; the transaction saves the documents that changed. */
class StorageSession {
public:
    ConquisterState &state();
    Quotes &quotes();
    [[nodiscard]] std::size_t random_index(std::size_t count);
    /* Copies the state as it is on disk, before this transaction, beside it under a name ending in the
       tag; throws if it cannot. A state never saved has nothing to copy. */
    void backup(std::string_view tag) const;

private:
    friend class Storage;

    explicit StorageSession(Storage &storage);
    void save() const;

    template <typename Document>
    struct Loaded {
        Document original;
        Document current;
    };

    Storage &storage_;
    std::optional<Loaded<ConquisterState>> state_;
    std::optional<Loaded<Quotes>> quotes_;
};

/* Serializes every access to the two JSON files, which are validated on construction. */
class Storage {
public:
    Storage(std::string conquister_path, std::string quotes_path);

    /* Runs function(session) under the lock. If it returns, changed quotes are saved first and then
       the changed state; the quotes are restored if the state cannot be saved. */
    template <typename Function>
    auto transaction(Function &&function) {
        const std::lock_guard lock{mutex_};
        StorageSession session{*this};
        auto result = std::forward<Function>(function)(session);
        session.save();
        return result;
    }

private:
    friend class StorageSession;

    std::string conquister_path_;
    std::string quotes_path_;
    std::mutex mutex_;
    std::mt19937_64 random_;
};

}

#endif
