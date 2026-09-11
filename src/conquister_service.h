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

typedef struct {
    bool found;
    char username[CONQUISTER_USERNAME_MAX + 1U];
    int64_t score;
    size_t rank;
    int64_t quotes_added;
    bool in_conquister;
    int64_t since;
} ConquisterUser;

bool conquister_claim(
    Storage *storage,
    int64_t user_id,
    const char *username,
    int64_t now,
    ClaimResult *result
);
bool conquister_leaderboard(Storage *storage, Leaderboard *leaderboard);
void leaderboard_free(Leaderboard *leaderboard);
/* Case-insensitive lookup; rank is 0 when the user has no score yet. */
bool conquister_user(Storage *storage, const char *username, ConquisterUser *user);

#endif
