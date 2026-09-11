#ifndef NORELECBOT_STORAGE_HPP
#define NORELECBOT_STORAGE_HPP

#include "json.hpp"

#include <cstddef>
#include <cstdint>
#include <mutex>
#include <optional>
#include <random>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace norelecbot {

/* The cause, when known, is logged where the error is thrown. */
class StorageError : public std::runtime_error {
public:
    using std::runtime_error::runtime_error;
};

/* Validates both JSON documents on construction. */
class Storage {
public:
    Storage(std::string conquister_path, std::string quotes_path);

private:
    friend class StorageTransaction;

    std::string conquister_path_;
    std::string quotes_path_;
    std::mutex mutex_;
    std::mt19937_64 random_;
};

enum class StorageDocuments { state, quotes, all };

struct ScoreEntry {
    std::string username;
    std::int64_t score = 0;
};

/* Holds the storage lock and the requested documents for its whole lifetime. */
class StorageTransaction {
public:
    StorageTransaction(Storage &storage, StorageDocuments documents);

    /* Saves changed quotes, then the changed state; restores the quotes if the state cannot be saved. */
    void commit();

    [[nodiscard]] std::string_view holder() const;
    [[nodiscard]] std::int64_t holder_since(std::int64_t fallback) const;
    void set_holder(std::int64_t user_id, const std::string &username, std::int64_t now);
    [[nodiscard]] std::int64_t score(const std::string &username) const;
    void set_score(const std::string &username, std::int64_t value);
    [[nodiscard]] std::int64_t quotes_added(const std::string &username) const;
    void set_quotes_added(const std::string &username, std::int64_t count);
    /* Case-insensitive lookups returning the stored spelling of the name. */
    [[nodiscard]] std::optional<std::string> find_score(std::string_view username) const;
    [[nodiscard]] std::optional<std::string> find_quotes_added(std::string_view username) const;
    /* Scores in file order. */
    [[nodiscard]] std::vector<ScoreEntry> scores() const;

    [[nodiscard]] std::size_t quotes_count() const;
    [[nodiscard]] std::string_view quote(std::size_t index) const;
    void append_quote(const std::string &text);
    void remove_quote(std::size_t index);
    [[nodiscard]] std::size_t random_index(std::size_t count);

private:
    [[nodiscard]] const Json &quote_list() const;
    Json &writable_quotes();

    Storage &storage_;
    std::unique_lock<std::mutex> lock_;
    Json state_;
    Json quotes_;
    std::optional<Json> updated_quotes_;
    bool state_changed_ = false;
};

}

#endif
