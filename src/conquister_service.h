#ifndef NORELECBOT_CONQUISTER_SERVICE_H
#define NORELECBOT_CONQUISTER_SERVICE_H

#include "storage.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define CONQUISTER_USERNAME_MAX 64

typedef enum {
    CLAIM_TAKEN,
    CLAIM_ALREADY_HELD
} ClaimStatus;

typedef struct {
    ClaimStatus status;
    char previous_username[CONQUISTER_USERNAME_MAX + 1U];
    int64_t earned;
} ClaimResult;

typedef struct {
    char *username;
    int64_t score;
    int64_t quotes_added;
} LeaderboardEntry;

typedef struct {
    LeaderboardEntry *entries;
    size_t count;
    char *current_username;
} Leaderboard;

bool conquister_claim(
    Storage *storage,
    int64_t user_id,
    const char *username,
    int64_t now,
    ClaimResult *result
);
bool conquister_leaderboard(Storage *storage, Leaderboard *leaderboard);
void leaderboard_free(Leaderboard *leaderboard);

#endif
