#include "quote_service.h"

#include "json_storage_internal.h"
#include "text.h"

#include <stdckdint.h>
#include <stdint.h>
#include <string.h>

static constexpr size_t QUOTES_PAGE_SIZE = 30;

static bool quote_exists(const StorageTransaction *transaction, const char *quote) {
    size_t count = quotes_count(transaction);
    for (size_t index = 0U; index < count; ++index) {
        const char *existing = quotes_at(transaction, index);
        if (existing != nullptr && strcmp(existing, quote) == 0) {
            return true;
        }
    }
    return false;
}

/* Must be called while the storage lock is held. */
static uint64_t next_random(Storage *storage) {
    uint64_t state = storage->quote_random_state;
    state ^= state >> 12U;
    state ^= state << 25U;
    state ^= state >> 27U;
    storage->quote_random_state = state;
    return state * UINT64_C(2685821657736338717);
}

bool quote_add(
    Storage *storage,
    const char *username,
    const char *quote,
    int cost,
    QuoteAddResult *result
) {
    *result = (QuoteAddResult){};
    StorageTransaction transaction;
    if (!storage_begin(storage, STORAGE_STATE | STORAGE_QUOTES, &transaction)) {
        return false;
    }
    bool ok = true;
    int64_t score = state_score(&transaction, username);
    result->available_score = score;
    if (score < cost) {
        result->status = QUOTE_INSUFFICIENT_SCORE;
    } else if (quote_exists(&transaction, quote)) {
        result->status = QUOTE_DUPLICATE;
    } else {
        int64_t added = 0;
        ok = quotes_append(&transaction, quote) &&
             !ckd_add(&added, state_quotes_added(&transaction, username), 1) &&
             state_set_score(&transaction, username, score - cost) &&
             state_set_quotes_added(&transaction, username, added) &&
             storage_commit(&transaction);
        if (ok) {
            result->status = QUOTE_ADDED;
            result->available_score = score - cost;
        }
    }
    storage_end(&transaction);
    return ok;
}

bool quote_page_load(Storage *storage, Arena *arena, int requested_page, QuotePage *page) {
    *page = (QuotePage){};
    StorageTransaction transaction;
    if (!storage_begin(storage, STORAGE_QUOTES, &transaction)) {
        return false;
    }
    bool ok = true;
    page->total = quotes_count(&transaction);
    if (page->total > 0U) {
        page->pages = ((page->total - 1U) / QUOTES_PAGE_SIZE) + 1U;
        page->page = requested_page > 0 ? (size_t)requested_page : 1U;
        if (page->page > page->pages) {
            page->page = page->pages;
        }
        size_t offset = (page->page - 1U) * QUOTES_PAGE_SIZE;
        size_t remaining = page->total - offset;
        page->count = remaining > QUOTES_PAGE_SIZE ? QUOTES_PAGE_SIZE : remaining;
        page->first_number = offset + 1U;
        page->items = arena_alloc_array(arena, page->count, sizeof(*page->items));
        ok = page->items != nullptr;
        for (size_t index = 0U; ok && index < page->count; ++index) {
            page->items[index] = arena_strdup(arena, quotes_at(&transaction, offset + index));
            ok = page->items[index] != nullptr;
        }
    }
    storage_end(&transaction);
    return ok;
}

bool quote_random(Storage *storage, Arena *arena, char **quote) {
    *quote = nullptr;
    StorageTransaction transaction;
    if (!storage_begin(storage, STORAGE_QUOTES, &transaction)) {
        return false;
    }
    bool ok = true;
    size_t count = quotes_count(&transaction);
    if (count > 0U) {
        size_t index = (size_t)(next_random(storage) % count);
        *quote = arena_strdup(arena, quotes_at(&transaction, index));
        ok = *quote != nullptr;
    }
    storage_end(&transaction);
    return ok;
}

bool quote_delete(Storage *storage, Arena *arena, const char *selector, char **removed_quote) {
    *removed_quote = nullptr;
    StorageTransaction transaction;
    if (!storage_begin(storage, STORAGE_QUOTES, &transaction)) {
        return false;
    }
    size_t count = quotes_count(&transaction);
    size_t selected = SIZE_MAX;
    int64_t position = 0;
    if (text_parse_int64(selector, &position) && position > 0 && (uint64_t)position <= count) {
        selected = (size_t)(position - 1);
    } else {
        for (size_t index = 0U; index < count; ++index) {
            const char *quote = quotes_at(&transaction, index);
            if (quote != nullptr && strcmp(quote, selector) == 0) {
                selected = index;
                break;
            }
        }
    }
    bool ok = true;
    if (selected != SIZE_MAX) {
        *removed_quote = arena_strdup(arena, quotes_at(&transaction, selected));
        ok = *removed_quote != nullptr && quotes_remove(&transaction, selected) &&
             storage_commit(&transaction);
    }
    storage_end(&transaction);
    return ok;
}
