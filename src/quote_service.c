#include "quote_service.h"

#include "json_storage_internal.h"
#include "logging.h"
#include "text.h"

#include <limits.h>
#include <stdint.h>
#include <string.h>

#define QUOTES_PAGE_SIZE 30U

static bool quote_exists(json_t *quotes, const char *quote) {
    size_t index;
    json_t *entry;
    json_array_foreach(quotes, index, entry) {
        const char *existing = json_string_value(entry);
        if (existing != NULL && strcmp(existing, quote) == 0) {
            return true;
        }
    }
    return false;
}

static json_t *copy_quotes(json_t *quotes, size_t skipped, const char *addition) {
    json_t *updated = json_copy(quotes);
    if (updated == NULL) {
        return NULL;
    }
    if ((skipped != SIZE_MAX && json_array_remove(updated, skipped) != 0) ||
        (addition != NULL && json_array_append_new(updated, json_string(addition)) != 0)) {
        json_decref(updated);
        return NULL;
    }
    return updated;
}

bool quote_add(
    Storage *storage,
    const char *username,
    const char *quote,
    int cost,
    QuoteAddResult *result
) {
    *result = (QuoteAddResult){0};
    if (!json_storage_lock(storage)) {
        return false;
    }
    json_t *state = json_storage_load_conquister(storage);
    json_t *quotes = json_storage_load_quotes(storage);
    json_t *updated_quotes = NULL;
    bool ok = false;
    if (state == NULL || quotes == NULL) {
        goto cleanup;
    }

    json_t *scores = json_object_get(state, "scores");
    int64_t score = json_integer_member(scores, username, 0);
    result->available_score = score;
    if (score < cost) {
        result->status = QUOTE_INSUFFICIENT_SCORE;
        ok = true;
        goto cleanup;
    }
    if (quote_exists(quotes, quote)) {
        result->status = QUOTE_DUPLICATE;
        ok = true;
        goto cleanup;
    }

    updated_quotes = copy_quotes(quotes, SIZE_MAX, quote);
    json_t *quotes_added = json_object_get(state, "quotes_added");
    int64_t added = json_integer_member(quotes_added, username, 0);
    if (updated_quotes == NULL || added == INT64_MAX ||
        !json_set_integer(scores, username, score - cost) ||
        !json_set_integer(quotes_added, username, added + 1)) {
        goto cleanup;
    }
    if (!json_storage_save_quotes(storage, updated_quotes)) {
        goto cleanup;
    }
    if (!json_storage_save_conquister(storage, state)) {
        if (!json_storage_save_quotes(storage, quotes)) {
            log_error("Could not roll back quote collection after Conquister save failure");
        }
        goto cleanup;
    }

    result->status = QUOTE_ADDED;
    result->available_score = score - cost;
    ok = true;

cleanup:
    json_decref(updated_quotes);
    json_decref(state);
    json_decref(quotes);
    json_storage_unlock(storage);
    return ok;
}

bool quote_page_load(Storage *storage, Arena *arena, int requested_page, QuotePage *page) {
    *page = (QuotePage){0};
    if (!json_storage_lock(storage)) {
        return false;
    }
    json_t *quotes = json_storage_load_quotes(storage);
    bool ok = quotes != NULL;
    page->total = json_array_size(quotes);
    if (ok && page->total > 0U) {
        page->pages = ((page->total - 1U) / QUOTES_PAGE_SIZE) + 1U;
        page->page = requested_page > 0 ? (size_t)requested_page : 1U;
        if (page->page > page->pages) {
            page->page = page->pages;
        }
        size_t offset = (page->page - 1U) * QUOTES_PAGE_SIZE;
        size_t remaining = page->total - offset;
        page->count = remaining > QUOTES_PAGE_SIZE ? QUOTES_PAGE_SIZE : remaining;
        page->first_number = offset + 1U;
        page->items = arena_alloc(arena, page->count * sizeof(*page->items));
        ok = page->items != NULL;
        for (size_t index = 0U; ok && index < page->count; ++index) {
            page->items[index] = arena_strdup(
                arena,
                json_string_value(json_array_get(quotes, offset + index))
            );
            ok = page->items[index] != NULL;
        }
    }
    json_decref(quotes);
    json_storage_unlock(storage);
    return ok;
}

bool quote_random(Storage *storage, Arena *arena, char **quote) {
    *quote = NULL;
    if (!json_storage_lock(storage)) {
        return false;
    }
    json_t *quotes = json_storage_load_quotes(storage);
    bool ok = quotes != NULL;
    size_t count = json_array_size(quotes);
    if (ok && count > 0U) {
        size_t index = (size_t)(json_storage_next_quote_random(storage) % count);
        *quote = arena_strdup(arena, json_string_value(json_array_get(quotes, index)));
        ok = *quote != NULL;
    }
    json_decref(quotes);
    json_storage_unlock(storage);
    return ok;
}

bool quote_delete(Storage *storage, Arena *arena, const char *selector, char **removed_quote) {
    *removed_quote = NULL;
    if (!json_storage_lock(storage)) {
        return false;
    }
    json_t *quotes = json_storage_load_quotes(storage);
    json_t *updated = NULL;
    bool ok = quotes != NULL;
    size_t count = json_array_size(quotes);
    size_t selected = SIZE_MAX;
    int64_t position = 0;
    if (text_parse_int64(selector, &position) && position > 0 && (uint64_t)position <= count) {
        selected = (size_t)(position - 1);
    } else {
        size_t index;
        json_t *entry;
        json_array_foreach(quotes, index, entry) {
            const char *quote = json_string_value(entry);
            if (quote != NULL && strcmp(quote, selector) == 0) {
                selected = index;
                break;
            }
        }
    }
    if (ok && selected != SIZE_MAX) {
        *removed_quote = arena_strdup(arena, json_string_value(json_array_get(quotes, selected)));
        updated = copy_quotes(quotes, selected, NULL);
        ok = *removed_quote != NULL && updated != NULL &&
             json_storage_save_quotes(storage, updated);
    }
    json_decref(updated);
    json_decref(quotes);
    json_storage_unlock(storage);
    return ok;
}
