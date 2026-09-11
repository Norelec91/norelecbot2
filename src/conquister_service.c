#include "conquister_service.h"

#include "json_storage_internal.h"
#include "logging.h"
#include "text.h"

#include <stdckdint.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct {
    const char *username;
    int64_t score;
} ScoreEntry;

static json_t *new_current(int64_t user_id, const char *username, int64_t now) {
    return json_pack(
        "{s:I, s:s, s:I}",
        "user_id", (json_int_t)user_id,
        "username", username,
        "since", (json_int_t)now
    );
}

bool conquister_claim(
    Storage *storage,
    int64_t user_id,
    const char *username,
    int64_t now,
    ClaimResult *result
) {
    *result = (ClaimResult){};
    if (!json_storage_lock(storage)) {
        return false;
    }
    json_t *state = json_storage_load_conquister(storage);
    bool ok = false;
    if (state == nullptr) {
        goto cleanup;
    }

    json_t *current = json_object_get(state, "current");
    const char *holder = json_string_value(json_object_get(current, "username"));
    if (holder != nullptr && strcmp(holder, username) == 0) {
        result->status = CLAIM_ALREADY_HELD;
        ok = true;
        goto cleanup;
    }

    result->status = CLAIM_TAKEN;
    json_t *scores = json_object_get(state, "scores");
    if (holder != nullptr && *holder != '\0') {
        (void)snprintf(
            result->previous_username,
            sizeof(result->previous_username),
            "%s",
            holder
        );
        int64_t since = json_integer_member(current, "since", now);
        result->earned = now > since ? now - since : 0;
        int64_t updated_score = 0;
        if (ckd_add(&updated_score, json_integer_member(scores, holder, 0), result->earned) ||
            !json_set_integer(scores, holder, updated_score)) {
            log_error("Could not update Conquister score");
            goto cleanup;
        }
    }

    if (json_object_set_new(state, "current", new_current(user_id, username, now)) != 0) {
        goto cleanup;
    }
    ok = json_storage_save_conquister(storage, state);

cleanup:
    json_decref(state);
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

bool conquister_leaderboard(
    Storage *storage,
    Arena *arena,
    size_t limit,
    Leaderboard *leaderboard
) {
    *leaderboard = (Leaderboard){};
    if (!json_storage_lock(storage)) {
        return false;
    }
    json_t *state = json_storage_load_conquister(storage);
    json_t *scores = json_object_get(state, "scores");
    size_t count = json_object_size(scores);
    bool ok = state != nullptr;
    if (ok && count > 0U) {
        ScoreEntry *sorted = arena_alloc_array(arena, count, sizeof(*sorted));
        leaderboard->count = limit == 0U || count < limit ? count : limit;
        leaderboard->entries = arena_alloc_array(
            arena,
            leaderboard->count,
            sizeof(*leaderboard->entries)
        );
        ok = sorted != nullptr && leaderboard->entries != nullptr;

        size_t index = 0U;
        const char *key;
        json_t *value;
        json_object_foreach(scores, key, value) {
            if (ok) {
                sorted[index++] = (ScoreEntry){
                    .username = key,
                    .score = (int64_t)json_integer_value(value),
                };
            }
        }
        if (ok) {
            qsort(sorted, count, sizeof(*sorted), compare_scores);
        }

        json_t *quotes_added = json_object_get(state, "quotes_added");
        for (index = 0U; ok && index < leaderboard->count; ++index) {
            LeaderboardEntry *entry = &leaderboard->entries[index];
            entry->username = arena_strdup(arena, sorted[index].username);
            entry->score = sorted[index].score;
            entry->quotes_added = json_integer_member(quotes_added, sorted[index].username, 0);
            ok = entry->username != nullptr;
        }
    }
    json_t *current = json_object_get(state, "current");
    const char *holder = json_string_value(json_object_get(current, "username"));
    if (ok && holder != nullptr && *holder != '\0') {
        leaderboard->current_username = arena_strdup(arena, holder);
        leaderboard->current_since = json_integer_member(current, "since", 0);
        ok = leaderboard->current_username != nullptr;
    }

    json_decref(state);
    json_storage_unlock(storage);
    return ok;
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

bool conquister_user(Storage *storage, const char *username, ConquisterUser *user) {
    *user = (ConquisterUser){};
    if (!json_storage_lock(storage)) {
        return false;
    }
    json_t *state = json_storage_load_conquister(storage);
    bool ok = state != nullptr;
    if (!ok) {
        goto cleanup;
    }

    const char *name = nullptr;
    json_t *current = json_object_get(state, "current");
    const char *holder = json_string_value(json_object_get(current, "username"));
    if (holder != nullptr && *holder != '\0' && text_equals_ignore_case(holder, username)) {
        name = holder;
        user->in_conquister = true;
        user->since = json_integer_member(current, "since", 0);
    }
    json_t *scores = json_object_get(state, "scores");
    json_t *quotes_added = json_object_get(state, "quotes_added");
    const char *score_name = find_key_ignore_case(scores, username);
    const char *quotes_name = find_key_ignore_case(quotes_added, username);
    if (name == nullptr) {
        name = score_name != nullptr ? score_name : quotes_name;
    }
    if (name == nullptr) {
        goto cleanup;
    }

    user->found = true;
    (void)snprintf(user->username, sizeof(user->username), "%s", name);
    if (score_name != nullptr) {
        ScoreEntry self = {
            .username = score_name,
            .score = json_integer_member(scores, score_name, 0),
        };
        user->score = self.score;
        user->rank = 1U;
        const char *key;
        json_t *value;
        json_object_foreach(scores, key, value) {
            ScoreEntry other = {.username = key, .score = (int64_t)json_integer_value(value)};
            if (compare_scores(&other, &self) < 0) {
                ++user->rank;
            }
        }
    }
    if (quotes_name != nullptr) {
        user->quotes_added = json_integer_member(quotes_added, quotes_name, 0);
    }

cleanup:
    json_decref(state);
    json_storage_unlock(storage);
    return ok;
}
