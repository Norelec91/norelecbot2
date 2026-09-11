#ifndef NORELECBOT_QUOTE_SERVICE_H
#define NORELECBOT_QUOTE_SERVICE_H

#include "arena.h"
#include "storage.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef enum {
    QUOTE_ADDED,
    QUOTE_DUPLICATE,
    QUOTE_INSUFFICIENT_SCORE
} QuoteAddStatus;

typedef struct {
    QuoteAddStatus status;
    int64_t available_score;
} QuoteAddResult;

typedef struct {
    char **items;
    size_t count;
    size_t total;
    size_t page;
    size_t pages;
    size_t first_number;
} QuotePage;

[[nodiscard]] bool quote_add(
    Storage *storage,
    const char *username,
    const char *quote,
    int cost,
    QuoteAddResult *result
);
/* Returned strings and page items live in arena. */
[[nodiscard]] bool quote_page_load(Storage *storage, Arena *arena, int requested_page, QuotePage *page);
[[nodiscard]] bool quote_random(Storage *storage, Arena *arena, char **quote);
[[nodiscard]] bool quote_delete(Storage *storage, Arena *arena, const char *selector, char **removed_quote);

#endif
