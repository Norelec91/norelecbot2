#include "json_storage_internal.h"

#include "logging.h"
#include "platform.h"
#include "text.h"

#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

static bool ensure_object_member(json_t *parent, const char *name) {
    json_t *value = json_object_get(parent, name);
    if (value != nullptr) {
        return json_is_object(value);
    }
    return json_object_set_new(parent, name, json_object()) == 0;
}

static bool normalize_conquister(json_t *state) {
    if (!json_is_object(state)) {
        return false;
    }
    json_t *current = json_object_get(state, "current");
    if (current == nullptr) {
        if (json_object_set_new(state, "current", json_null()) != 0) {
            return false;
        }
    } else if (!json_is_null(current) && !json_is_object(current)) {
        return false;
    }
    return ensure_object_member(state, "scores") &&
           ensure_object_member(state, "quotes_added");
}

static bool file_missing(const char *path, const char *description) {
    FILE *file = fopen(path, "rb");
    if (file != nullptr) {
        (void)fclose(file);
        return false;
    }
    if (errno != ENOENT) {
        log_error("Could not access %s %s: %s", description, path, strerror(errno));
    }
    return errno == ENOENT;
}

static json_t *load_state(const Storage *storage) {
    const char *path = storage->conquister_path;
    if (file_missing(path, "Conquister state")) {
        return json_pack("{s:n, s:{}, s:{}}", "current", "scores", "quotes_added");
    }
    json_error_t error;
    json_t *state = json_load_file(path, 0, &error);
    if (state == nullptr || !normalize_conquister(state)) {
        log_error(
            "Conquister state %s is not valid: %s",
            path,
            state == nullptr ? error.text : "unexpected structure"
        );
        json_decref(state);
        return nullptr;
    }
    return state;
}

static json_t *load_quotes(const Storage *storage) {
    const char *path = storage->quotes_path;
    if (file_missing(path, "quote collection")) {
        return json_array();
    }
    json_error_t error;
    json_t *quotes = json_load_file(path, 0, &error);
    if (quotes == nullptr || !json_is_array(quotes)) {
        log_error(
            "Quote collection %s is not a JSON array: %s",
            path,
            quotes == nullptr ? error.text : "unexpected structure"
        );
        json_decref(quotes);
        return nullptr;
    }
    size_t index;
    json_t *entry;
    json_array_foreach(quotes, index, entry) {
        const char *text = json_string_value(entry);
        if (text == nullptr || *text == '\0') {
            log_error("Quote collection %s contains an invalid entry", path);
            json_decref(quotes);
            return nullptr;
        }
    }
    return quotes;
}

static bool save_json(const char *path, json_t *value) {
    char temporary[1032];
    int length = snprintf(temporary, sizeof(temporary), "%s.tmp", path);
    if (length < 0 || (size_t)length >= sizeof(temporary)) {
        log_error("JSON path is too long: %s", path);
        return false;
    }
    if (json_dump_file(value, temporary, JSON_INDENT(2)) != 0) {
        int saved_errno = errno;
        (void)remove(temporary);
        log_error("Could not write JSON file %s: %s", temporary, strerror(saved_errno));
        return false;
    }
    if (!platform_replace_file(temporary, path)) {
        (void)remove(temporary);
        log_error("Could not replace JSON file %s", path);
        return false;
    }
    return true;
}

static bool lock_storage(Storage *storage) {
    if (!storage->initialized || mtx_lock(&storage->mutex) != thrd_success) {
        log_error("Could not lock JSON storage");
        return false;
    }
    return true;
}

static void unlock_storage(Storage *storage) {
    if (!storage->initialized || mtx_unlock(&storage->mutex) != thrd_success) {
        log_error("Could not unlock JSON storage");
    }
}

bool storage_begin(Storage *storage, int documents, StorageTransaction *transaction) {
    *transaction = (StorageTransaction){.storage = storage};
    if (!lock_storage(storage)) {
        return false;
    }
    bool ok = true;
    if ((documents & STORAGE_STATE) != 0) {
        transaction->state = load_state(storage);
        ok = transaction->state != nullptr;
    }
    if ((documents & STORAGE_QUOTES) != 0) {
        transaction->quotes = load_quotes(storage);
        ok = ok && transaction->quotes != nullptr;
    }
    if (!ok) {
        storage_end(transaction);
    }
    return ok;
}

bool storage_commit(StorageTransaction *transaction) {
    const Storage *storage = transaction->storage;
    if (transaction->updated_quotes != nullptr &&
        !save_json(storage->quotes_path, transaction->updated_quotes)) {
        return false;
    }
    if (transaction->state_changed && !save_json(storage->conquister_path, transaction->state)) {
        if (transaction->updated_quotes != nullptr &&
            !save_json(storage->quotes_path, transaction->quotes)) {
            log_error("Could not roll back quote collection after Conquister save failure");
        }
        return false;
    }
    return true;
}

void storage_end(StorageTransaction *transaction) {
    json_decref(transaction->updated_quotes);
    json_decref(transaction->state);
    json_decref(transaction->quotes);
    unlock_storage(transaction->storage);
    *transaction = (StorageTransaction){};
}

static int64_t integer_member(json_t *object, const char *name, int64_t fallback) {
    json_t *value = json_object_get(object, name);
    return json_is_integer(value) ? (int64_t)json_integer_value(value) : fallback;
}

static bool set_state_integer(
    StorageTransaction *transaction,
    const char *table,
    const char *username,
    int64_t value
) {
    json_t *object = json_object_get(transaction->state, table);
    if (json_object_set_new(object, username, json_integer((json_int_t)value)) != 0) {
        return false;
    }
    transaction->state_changed = true;
    return true;
}

static const char *find_key_ignore_case(json_t *object, const char *name) {
    const char *key;
    [[maybe_unused]] json_t *value;
    json_object_foreach(object, key, value) {
        if (text_equals_ignore_case(key, name)) {
            return key;
        }
    }
    return nullptr;
}

const char *state_holder(const StorageTransaction *transaction) {
    json_t *current = json_object_get(transaction->state, "current");
    return json_string_value(json_object_get(current, "username"));
}

int64_t state_holder_since(const StorageTransaction *transaction, int64_t fallback) {
    return integer_member(json_object_get(transaction->state, "current"), "since", fallback);
}

bool state_set_holder(
    StorageTransaction *transaction,
    int64_t user_id,
    const char *username,
    int64_t now
) {
    json_t *holder = json_pack(
        "{s:I, s:s, s:I}",
        "user_id", (json_int_t)user_id,
        "username", username,
        "since", (json_int_t)now
    );
    if (json_object_set_new(transaction->state, "current", holder) != 0) {
        return false;
    }
    transaction->state_changed = true;
    return true;
}

int64_t state_score(const StorageTransaction *transaction, const char *username) {
    return integer_member(json_object_get(transaction->state, "scores"), username, 0);
}

bool state_set_score(StorageTransaction *transaction, const char *username, int64_t score) {
    return set_state_integer(transaction, "scores", username, score);
}

int64_t state_quotes_added(const StorageTransaction *transaction, const char *username) {
    return integer_member(json_object_get(transaction->state, "quotes_added"), username, 0);
}

bool state_set_quotes_added(StorageTransaction *transaction, const char *username, int64_t count) {
    return set_state_integer(transaction, "quotes_added", username, count);
}

const char *state_find_score(const StorageTransaction *transaction, const char *username) {
    return find_key_ignore_case(json_object_get(transaction->state, "scores"), username);
}

const char *state_find_quotes_added(const StorageTransaction *transaction, const char *username) {
    return find_key_ignore_case(json_object_get(transaction->state, "quotes_added"), username);
}

size_t state_score_count(const StorageTransaction *transaction) {
    return json_object_size(json_object_get(transaction->state, "scores"));
}

bool state_next_score(const StorageTransaction *transaction, void **cursor, StateScore *entry) {
    json_t *scores = json_object_get(transaction->state, "scores");
    *cursor = *cursor == nullptr ? json_object_iter(scores) : json_object_iter_next(scores, *cursor);
    if (*cursor == nullptr) {
        return false;
    }
    *entry = (StateScore){
        .username = json_object_iter_key(*cursor),
        .score = (int64_t)json_integer_value(json_object_iter_value(*cursor)),
    };
    return true;
}

static json_t *quote_list(const StorageTransaction *transaction) {
    return transaction->updated_quotes != nullptr ? transaction->updated_quotes : transaction->quotes;
}

static json_t *writable_quotes(StorageTransaction *transaction) {
    if (transaction->updated_quotes == nullptr) {
        transaction->updated_quotes = json_copy(transaction->quotes);
    }
    return transaction->updated_quotes;
}

size_t quotes_count(const StorageTransaction *transaction) {
    return json_array_size(quote_list(transaction));
}

const char *quotes_at(const StorageTransaction *transaction, size_t index) {
    return json_string_value(json_array_get(quote_list(transaction), index));
}

bool quotes_append(StorageTransaction *transaction, const char *quote) {
    json_t *quotes = writable_quotes(transaction);
    return quotes != nullptr && json_array_append_new(quotes, json_string(quote)) == 0;
}

bool quotes_remove(StorageTransaction *transaction, size_t index) {
    json_t *quotes = writable_quotes(transaction);
    return quotes != nullptr && json_array_remove(quotes, index) == 0;
}

bool storage_open(Storage *storage, const char *conquister_path, const char *quotes_path) {
    *storage = (Storage){};
    if (!text_copy(storage->conquister_path, sizeof(storage->conquister_path), conquister_path) ||
        !text_copy(storage->quotes_path, sizeof(storage->quotes_path), quotes_path)) {
        log_error("JSON storage path is too long");
        return false;
    }
    if (mtx_init(&storage->mutex, mtx_plain) != thrd_success) {
        log_error("Could not initialize JSON storage mutex");
        return false;
    }
    storage->initialized = true;

    struct timespec now = {};
    (void)timespec_get(&now, TIME_UTC);
    storage->quote_random_state = (uint64_t)now.tv_sec ^ ((uint64_t)now.tv_nsec << 32U) ^
                                  (uint64_t)(uintptr_t)storage;
    if (storage->quote_random_state == 0U) {
        storage->quote_random_state = UINT64_C(0x9e3779b97f4a7c15);
    }

    StorageTransaction transaction;
    if (!storage_begin(storage, STORAGE_STATE | STORAGE_QUOTES, &transaction)) {
        storage_close(storage);
        return false;
    }
    storage_end(&transaction);

    log_info(
        "JSON storage ready (conquister=%s, quotes=%s)",
        storage->conquister_path,
        storage->quotes_path
    );
    return true;
}

void storage_close(Storage *storage) {
    if (!storage->initialized) {
        return;
    }
    mtx_destroy(&storage->mutex);
    storage->initialized = false;
}
