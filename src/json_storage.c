#include "json_storage_internal.h"

#include "logging.h"

#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

static bool ensure_object_member(json_t *parent, const char *name) {
    json_t *value = json_object_get(parent, name);
    if (value != NULL) {
        return json_is_object(value);
    }
    return json_object_set_new(parent, name, json_object()) == 0;
}

static bool normalize_conquister(json_t *state) {
    if (!json_is_object(state)) {
        return false;
    }
    json_t *current = json_object_get(state, "current");
    if (current == NULL) {
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
    if (file != NULL) {
        (void)fclose(file);
        return false;
    }
    if (errno != ENOENT) {
        log_error("Could not access %s %s: %s", description, path, strerror(errno));
    }
    return errno == ENOENT;
}

json_t *json_storage_load_conquister(Storage *storage) {
    const char *path = storage->conquister_path;
    if (file_missing(path, "Conquister state")) {
        return json_pack("{s:n, s:{}, s:{}}", "current", "scores", "quotes_added");
    }
    json_error_t error;
    json_t *state = json_load_file(path, 0, &error);
    if (state == NULL || !normalize_conquister(state)) {
        log_error(
            "Conquister state %s is not valid: %s",
            path,
            state == NULL ? error.text : "unexpected structure"
        );
        json_decref(state);
        return NULL;
    }
    return state;
}

json_t *json_storage_load_quotes(Storage *storage) {
    const char *path = storage->quotes_path;
    if (file_missing(path, "quote collection")) {
        return json_array();
    }
    json_error_t error;
    json_t *quotes = json_load_file(path, 0, &error);
    if (quotes == NULL || !json_is_array(quotes)) {
        log_error(
            "Quote collection %s is not a JSON array: %s",
            path,
            quotes == NULL ? error.text : "unexpected structure"
        );
        json_decref(quotes);
        return NULL;
    }
    size_t index;
    json_t *entry;
    json_array_foreach(quotes, index, entry) {
        const char *text = json_string_value(entry);
        if (text == NULL || *text == '\0') {
            log_error("Quote collection %s contains an invalid entry", path);
            json_decref(quotes);
            return NULL;
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

bool json_storage_save_conquister(Storage *storage, json_t *state) {
    return save_json(storage->conquister_path, state);
}

bool json_storage_save_quotes(Storage *storage, json_t *quotes) {
    return save_json(storage->quotes_path, quotes);
}

bool json_storage_lock(Storage *storage) {
    if (!platform_mutex_lock(&storage->mutex)) {
        log_error("Could not lock JSON storage");
        return false;
    }
    return true;
}

void json_storage_unlock(Storage *storage) {
    if (!platform_mutex_unlock(&storage->mutex)) {
        log_error("Could not unlock JSON storage");
    }
}

uint64_t json_storage_next_quote_random(Storage *storage) {
    uint64_t state = storage->quote_random_state;
    state ^= state >> 12U;
    state ^= state << 25U;
    state ^= state >> 27U;
    storage->quote_random_state = state;
    return state * UINT64_C(2685821657736338717);
}

int64_t json_integer_member(json_t *object, const char *name, int64_t fallback) {
    json_t *value = json_object_get(object, name);
    return json_is_integer(value) ? (int64_t)json_integer_value(value) : fallback;
}

bool json_set_integer(json_t *object, const char *name, int64_t value) {
    return json_object_set_new(object, name, json_integer((json_int_t)value)) == 0;
}

static bool copy_path(char *destination, size_t capacity, const char *source) {
    int length = snprintf(destination, capacity, "%s", source);
    return length >= 0 && (size_t)length < capacity;
}

bool storage_open(Storage *storage, const char *conquister_path, const char *quotes_path) {
    *storage = (Storage){0};
    if (!copy_path(storage->conquister_path, sizeof(storage->conquister_path), conquister_path) ||
        !copy_path(storage->quotes_path, sizeof(storage->quotes_path), quotes_path)) {
        log_error("JSON storage path is too long");
        return false;
    }
    if (!platform_mutex_init(&storage->mutex)) {
        log_error("Could not initialize JSON storage mutex");
        return false;
    }

    struct timespec now = {0};
    (void)timespec_get(&now, TIME_UTC);
    storage->quote_random_state = (uint64_t)now.tv_sec ^ ((uint64_t)now.tv_nsec << 32U) ^
                                  (uint64_t)(uintptr_t)storage;
    if (storage->quote_random_state == 0U) {
        storage->quote_random_state = UINT64_C(0x9e3779b97f4a7c15);
    }

    if (!json_storage_lock(storage)) {
        platform_mutex_destroy(&storage->mutex);
        return false;
    }
    json_t *state = json_storage_load_conquister(storage);
    json_t *quotes = json_storage_load_quotes(storage);
    bool valid = state != NULL && quotes != NULL;
    json_decref(state);
    json_decref(quotes);
    json_storage_unlock(storage);
    if (!valid) {
        platform_mutex_destroy(&storage->mutex);
        return false;
    }

    storage->initialized = true;
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
    platform_mutex_destroy(&storage->mutex);
    storage->initialized = false;
}
