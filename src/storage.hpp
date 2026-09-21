#ifndef NORELECBOT_STORAGE_HPP
#define NORELECBOT_STORAGE_HPP

#include <nlohmann/json.hpp>

#include <cstddef>
#include <cstdint>
#include <mutex>
#include <optional>
#include <random>
#include <stdexcept>
#include <string>
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

    bool operator==(const Holder &) const = default;
};

/* Usernames in file order. */
using Counters = nlohmann::ordered_map<std::string, std::int64_t>;
/* Quotes mapped to whoever added them, in file order. */
using Authors = nlohmann::ordered_map<std::string, std::string>;

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

    bool operator==(const Raid &) const = default;
};

struct ConquisterState {
    std::optional<Holder> current;
    Counters scores;
    Counters quotes_added;
    /* Balloon owners mapped to the attempts their balloon has already survived. */
    Counters balloons;
    /* Users mapped to the instant their claim penalty expires. */
    Counters cooldowns;
    /* Owners of a balloon that no attempt can pop, mapped to the instant it deflates. */
    Counters shields;
    /* Users mapped to the multiplier their next hold earns, until someone takes the place from them. */
    Counters boosts;
    /* Where each player lives: an id of ours, drawn once, which spells out a point on the map. */
    Counters ids;
    /* Players known to be on Telegram, mapped to their id there, so a message can reach them. */
    Counters telegram_ids;
    /* The raids under way, in the order they left. */
    std::vector<Raid> raids;
    /* Who added each quote, for the ones added since the bot started writing it down. */
    Authors quote_authors;
    /* Le emoji che ognuno si è comprato da appendere al nome. */
    Authors furniture;

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
