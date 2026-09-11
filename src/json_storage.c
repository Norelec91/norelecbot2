#include "json_storage_internal.h"

#include "logging.h"

#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

static bool ensure_object_member(json_object *parent, const char *name) {
    json_object *value = NULL;
    if (json_object_object_get_ex(parent, name, &value)) {
        return json_object_is_type(value, json_type_object);
    }
    value = json_object_new_object();
    if (value != NULL && json_object_object_add(parent, name, value) == 0) {
        return true;
    }
    json_object_put(value);
    return false;
}

static json_object *empty_conquister(void) {
    json_object *state = json_object_new_object();
    if (state == NULL) {
        return NULL;
    }
    if (json_object_object_add(state, "current", NULL) != 0 ||
        !ensure_object_member(state, "scores") ||
        !ensure_object_member(state, "quotes_added")) {
        json_object_put(state);
        return NULL;
    }
    return state;
}

static bool normalize_conquister(json_object *state) {
    if (!json_object_is_type(state, json_type_object)) {
        return false;
    }
    json_object *current = NULL;
    if (!json_object_object_get_ex(state, "current", &current)) {
        if (json_object_object_add(state, "current", NULL) != 0) {
            return false;
        }
    } else if (current != NULL && !json_object_is_type(current, json_type_object)) {
        return false;
    }
    return ensure_object_member(state, "scores") &&
           ensure_object_member(state, "quotes_added");
}

json_object *json_storage_load_conquister(Storage *storage) {
    FILE *file = fopen(storage->conquister_path, "rb");
    if (file == NULL) {
        if (errno != ENOENT) {
            log_error(
                "Could not access Conquister state %s: %s",
                storage->conquister_path,
                strerror(errno)
            );
            return NULL;
        }
        return empty_conquister();
    }
    (void)fclose(file);
    json_object *state = json_object_from_file(storage->conquister_path);
    if (state == NULL || !normalize_conquister(state)) {
        log_error("Conquister state %s is not valid", storage->conquister_path);
        json_object_put(state);
        return NULL;
    }
    return state;
}

json_object *json_storage_load_quotes(Storage *storage) {
    FILE *file = fopen(storage->quotes_path, "rb");
    if (file == NULL) {
        if (errno != ENOENT) {
            log_error(
                "Could not access quote collection %s: %s",
                storage->quotes_path,
                strerror(errno)
            );
            return NULL;
        }
        return json_object_new_array();
    }
    (void)fclose(file);
    json_object *quotes = json_object_from_file(storage->quotes_path);
    if (quotes == NULL || !json_object_is_type(quotes, json_type_array)) {
        log_error("Quote collection %s is not a JSON array", storage->quotes_path);
        json_object_put(quotes);
        return NULL;
    }
    size_t count = json_object_array_length(quotes);
    for (size_t index = 0U; index < count; ++index) {
        json_object *entry = json_object_array_get_idx(quotes, index);
        const char *text = entry != NULL && json_object_is_type(entry, json_type_string)
            ? json_object_get_string(entry)
            : NULL;
        if (text == NULL || *text == '\0') {
            log_error("Quote collection %s contains an invalid entry", storage->quotes_path);
            json_object_put(quotes);
            return NULL;
        }
    }
    return quotes;
}

static bool save_json(const char *path, json_object *value) {
    char temporary[1032];
    int length = snprintf(temporary, sizeof(temporary), "%s.tmp", path);
    if (length < 0 || (size_t)length >= sizeof(temporary)) {
        log_error("JSON path is too long: %s", path);
        return false;
    }
    if (json_object_to_file_ext(temporary, value, JSON_C_TO_STRING_PRETTY) != 0) {
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

bool json_storage_save_conquister(Storage *storage, json_object *state) {
    return save_json(storage->conquister_path, state);
}

bool json_storage_save_quotes(Storage *storage, json_object *quotes) {
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

json_object *json_member(json_object *object, const char *name) {
    json_object *value = NULL;
    return object != NULL && json_object_object_get_ex(object, name, &value) ? value : NULL;
}

int64_t json_integer(json_object *object, const char *name, int64_t fallback) {
    json_object *value = json_member(object, name);
    return value != NULL ? json_object_get_int64(value) : fallback;
}

bool json_set_integer(json_object *object, const char *name, int64_t value) {
    json_object *integer = json_object_new_int64(value);
    if (integer == NULL) {
        return false;
    }
    json_object_object_add(object, name, integer);
    return true;
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
    json_object *state = json_storage_load_conquister(storage);
    json_object *quotes = json_storage_load_quotes(storage);
    bool valid = state != NULL && quotes != NULL;
    json_object_put(state);
    json_object_put(quotes);
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
