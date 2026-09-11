#include "conquister_service.h"

#include "dynamic_string.h"
#include "json_storage_internal.h"
#include "logging.h"

#include <json-c/json.h>
#include <limits.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct {
    const char *username;
    int64_t score;
} ScoreEntry;

static json_object *new_current(int64_t user_id, const char *username, int64_t now) {
    json_object *current = json_object_new_object();
    json_object *id = json_object_new_int64(user_id);
    json_object *name = json_object_new_string(username);
    json_object *since = json_object_new_int64(now);
    if (current == NULL || id == NULL || name == NULL || since == NULL) {
        json_object_put(current);
        json_object_put(id);
        json_object_put(name);
        json_object_put(since);
        return NULL;
    }
    json_object_object_add(current, "user_id", id);
    json_object_object_add(current, "username", name);
    json_object_object_add(current, "since", since);
    return current;
}

bool conquister_claim(
    Storage *storage,
    int64_t user_id,
    const char *username,
    int64_t now,
    ClaimResult *result
) {
    *result = (ClaimResult){0};
    if (!json_storage_lock(storage)) {
        return false;
    }
    json_object *state = json_storage_load_conquister(storage);
    bool ok = false;
    if (state == NULL) {
        goto cleanup;
    }

    json_object *current = json_member(state, "current");
    json_object *username_value = json_member(current, "username");
    const char *holder = username_value != NULL ? json_object_get_string(username_value) : NULL;
    if (holder != NULL && strcmp(holder, username) == 0) {
        result->status = CLAIM_ALREADY_HELD;
        ok = true;
        goto cleanup;
    }

    result->status = CLAIM_TAKEN;
    json_object *scores = json_member(state, "scores");
    if (holder != NULL && *holder != '\0') {
        (void)snprintf(
            result->previous_username,
            sizeof(result->previous_username),
            "%s",
            holder
        );
        int64_t since = json_integer(current, "since", now);
        result->earned = now > since ? now - since : 0;
        int64_t previous_score = json_integer(scores, holder, 0);
        if (previous_score > INT64_MAX - result->earned ||
            !json_set_integer(scores, holder, previous_score + result->earned)) {
            log_error("Could not update Conquister score");
            goto cleanup;
        }
    }

    json_object *replacement = new_current(user_id, username, now);
    if (replacement == NULL) {
        goto cleanup;
    }
    json_object_object_add(state, "current", replacement);
    ok = json_storage_save_conquister(storage, state);

cleanup:
    json_object_put(state);
    json_storage_unlock(storage);
    return ok;
}

static int compare_scores(const void *left, const void *right) {
    const ScoreEntry *first = left;
    const ScoreEntry *second = right;
    if (first->score < second->score) {
        return 1;
    }
    if (first->score > second->score) {
        return -1;
    }
    return strcmp(first->username, second->username);
}

void leaderboard_free(Leaderboard *leaderboard) {
    if (leaderboard == NULL) {
        return;
    }
    if (leaderboard->entries != NULL) {
        for (size_t index = 0U; index < leaderboard->count; ++index) {
            free(leaderboard->entries[index].username);
        }
    }
    free(leaderboard->entries);
    free(leaderboard->current_username);
    *leaderboard = (Leaderboard){0};
}

bool conquister_leaderboard(Storage *storage, Leaderboard *leaderboard) {
    *leaderboard = (Leaderboard){0};
    if (!json_storage_lock(storage)) {
        return false;
    }
    json_object *state = json_storage_load_conquister(storage);
    ScoreEntry *sorted = NULL;
    bool ok = false;
    if (state == NULL) {
        goto cleanup;
    }
    json_object *scores = json_member(state, "scores");
    size_t count = (size_t)json_object_object_length(scores);
    if (count == 0U) {
        ok = true;
        goto cleanup;
    }
    if (count > SIZE_MAX / sizeof(ScoreEntry)) {
        goto cleanup;
    }
    sorted = malloc(count * sizeof(*sorted));
    if (sorted == NULL) {
        goto cleanup;
    }
    size_t index = 0U;
    json_object_object_foreach(scores, key, value) {
        sorted[index] = (ScoreEntry){.username = key, .score = json_object_get_int64(value)};
        ++index;
    }
    qsort(sorted, count, sizeof(*sorted), compare_scores);

    size_t shown = count < 10U ? count : 10U;
    leaderboard->entries = calloc(shown, sizeof(*leaderboard->entries));
    leaderboard->count = shown;
    ok = leaderboard->entries != NULL;
    json_object *quotes_added = json_member(state, "quotes_added");
    for (index = 0U; ok && index < shown; ++index) {
        leaderboard->entries[index].username = string_duplicate(sorted[index].username);
        leaderboard->entries[index].score = sorted[index].score;
        leaderboard->entries[index].quotes_added = json_integer(
            quotes_added,
            sorted[index].username,
            0
        );
        ok = leaderboard->entries[index].username != NULL;
    }
    json_object *current = json_member(state, "current");
    json_object *holder_value = json_member(current, "username");
    const char *holder = holder_value != NULL ? json_object_get_string(holder_value) : NULL;
    if (ok && holder != NULL && *holder != '\0') {
        leaderboard->current_username = string_duplicate(holder);
        ok = leaderboard->current_username != NULL;
    }

cleanup:
    free(sorted);
    json_object_put(state);
    json_storage_unlock(storage);
    if (!ok) {
        leaderboard_free(leaderboard);
    }
    return ok;
}
