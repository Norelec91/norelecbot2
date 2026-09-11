#ifndef NORELECBOT_JSON_STORAGE_INTERNAL_H
#define NORELECBOT_JSON_STORAGE_INTERNAL_H

#include "storage.h"

#include <jansson.h>
#include <stdint.h>

typedef enum {
    STORAGE_STATE = 1,
    STORAGE_QUOTES = 2,
} StorageDocument;

/* Holds the storage lock from a successful storage_begin until storage_end. */
typedef struct {
    Storage *storage;
    json_t *state;
    json_t *quotes;
    json_t *updated_quotes;
    bool state_changed;
} StorageTransaction;

typedef struct {
    const char *username;
    int64_t score;
} StateScore;

/* Loads every requested document; on failure the lock is already released. */
[[nodiscard]] bool storage_begin(Storage *storage, int documents, StorageTransaction *transaction);
/* Saves changed quotes, then the changed state; restores the quotes if the state cannot be saved. */
[[nodiscard]] bool storage_commit(StorageTransaction *transaction);
void storage_end(StorageTransaction *transaction);

[[nodiscard]] const char *state_holder(const StorageTransaction *transaction);
[[nodiscard]] int64_t state_holder_since(const StorageTransaction *transaction, int64_t fallback);
[[nodiscard]] bool state_set_holder(
    StorageTransaction *transaction,
    int64_t user_id,
    const char *username,
    int64_t now
);
[[nodiscard]] int64_t state_score(const StorageTransaction *transaction, const char *username);
[[nodiscard]] bool state_set_score(StorageTransaction *transaction, const char *username, int64_t score);
[[nodiscard]] int64_t state_quotes_added(const StorageTransaction *transaction, const char *username);
[[nodiscard]] bool state_set_quotes_added(
    StorageTransaction *transaction,
    const char *username,
    int64_t count
);
[[nodiscard]] const char *state_find_score(const StorageTransaction *transaction, const char *username);
[[nodiscard]] const char *state_find_quotes_added(
    const StorageTransaction *transaction,
    const char *username
);
[[nodiscard]] size_t state_score_count(const StorageTransaction *transaction);
/* Walks the scores in file order: start with *cursor == nullptr. */
[[nodiscard]] bool state_next_score(
    const StorageTransaction *transaction,
    void **cursor,
    StateScore *entry
);

[[nodiscard]] size_t quotes_count(const StorageTransaction *transaction);
[[nodiscard]] const char *quotes_at(const StorageTransaction *transaction, size_t index);
[[nodiscard]] bool quotes_append(StorageTransaction *transaction, const char *quote);
[[nodiscard]] bool quotes_remove(StorageTransaction *transaction, size_t index);

#endif
