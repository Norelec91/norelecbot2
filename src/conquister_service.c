#include "conquister_service.h"

#include "json_storage_internal.h"
#include "logging.h"
#include "text.h"

#include <stdckdint.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int compare_scores(const void *left, const void *right) {
    const StateScore *first = left;
    const StateScore *second = right;
    if (first->score < second->score) {
        return 1;
    }
    if (first->score > second->score) {
        return -1;
    }
    return strcmp(first->username, second->username);
}

bool conquister_claim(
    Storage *storage,
    int64_t user_id,
    const char *username,
    int64_t now,
    ClaimResult *result
) {
    *result = (ClaimResult){};
    StorageTransaction transaction;
    if (!storage_begin(storage, STORAGE_STATE, &transaction)) {
        return false;
    }
    bool ok = true;
    const char *holder = state_holder(&transaction);
    if (holder != nullptr && strcmp(holder, username) == 0) {
        result->status = CLAIM_ALREADY_HELD;
    } else {
        result->status = CLAIM_TAKEN;
        if (holder != nullptr && *holder != '\0') {
            (void)snprintf(
                result->previous_username,
                sizeof(result->previous_username),
                "%s",
                holder
            );
            int64_t since = state_holder_since(&transaction, now);
            result->earned = now > since ? now - since : 0;
            int64_t score = 0;
            ok = !ckd_add(&score, state_score(&transaction, holder), result->earned) &&
                 state_set_score(&transaction, holder, score);
            if (!ok) {
                log_error("Could not update Conquister score");
            }
        }
        ok = ok && state_set_holder(&transaction, user_id, username, now) &&
             storage_commit(&transaction);
    }
    storage_end(&transaction);
    return ok;
}

bool conquister_leaderboard(
    Storage *storage,
    Arena *arena,
    size_t limit,
    Leaderboard *leaderboard
) {
    *leaderboard = (Leaderboard){};
    StorageTransaction transaction;
    if (!storage_begin(storage, STORAGE_STATE, &transaction)) {
        return false;
    }
    bool ok = true;
    size_t count = state_score_count(&transaction);
    if (count > 0U) {
        StateScore *sorted = arena_alloc_array(arena, count, sizeof(*sorted));
        leaderboard->count = limit == 0U || count < limit ? count : limit;
        leaderboard->entries = arena_alloc_array(
            arena,
            leaderboard->count,
            sizeof(*leaderboard->entries)
        );
        ok = sorted != nullptr && leaderboard->entries != nullptr;
        void *cursor = nullptr;
        for (size_t index = 0U; ok && index < count; ++index) {
            ok = state_next_score(&transaction, &cursor, &sorted[index]);
        }
        if (ok) {
            qsort(sorted, count, sizeof(*sorted), compare_scores);
        }
        for (size_t index = 0U; ok && index < leaderboard->count; ++index) {
            LeaderboardEntry *entry = &leaderboard->entries[index];
            entry->username = arena_strdup(arena, sorted[index].username);
            entry->score = sorted[index].score;
            entry->quotes_added = state_quotes_added(&transaction, sorted[index].username);
            ok = entry->username != nullptr;
        }
    }
    const char *holder = state_holder(&transaction);
    if (ok && holder != nullptr && *holder != '\0') {
        leaderboard->current_username = arena_strdup(arena, holder);
        leaderboard->current_since = state_holder_since(&transaction, 0);
        ok = leaderboard->current_username != nullptr;
    }
    storage_end(&transaction);
    return ok;
}

bool conquister_user(Storage *storage, const char *username, ConquisterUser *user) {
    *user = (ConquisterUser){};
    StorageTransaction transaction;
    if (!storage_begin(storage, STORAGE_STATE, &transaction)) {
        return false;
    }
    const char *name = nullptr;
    const char *holder = state_holder(&transaction);
    if (holder != nullptr && *holder != '\0' && text_equals_ignore_case(holder, username)) {
        name = holder;
        user->in_conquister = true;
        user->since = state_holder_since(&transaction, 0);
    }
    const char *score_name = state_find_score(&transaction, username);
    const char *quotes_name = state_find_quotes_added(&transaction, username);
    if (name == nullptr) {
        name = score_name != nullptr ? score_name : quotes_name;
    }
    if (name != nullptr) {
        user->found = true;
        (void)snprintf(user->username, sizeof(user->username), "%s", name);
    }
    if (score_name != nullptr) {
        StateScore self = {.username = score_name, .score = state_score(&transaction, score_name)};
        user->score = self.score;
        user->rank = 1U;
        void *cursor = nullptr;
        StateScore other;
        while (state_next_score(&transaction, &cursor, &other)) {
            if (compare_scores(&other, &self) < 0) {
                ++user->rank;
            }
        }
    }
    if (quotes_name != nullptr) {
        user->quotes_added = state_quotes_added(&transaction, quotes_name);
    }
    storage_end(&transaction);
    return true;
}
