#include "quote_service.h"

#include "dynamic_string.h"
#include "json_storage_internal.h"
#include "logging.h"

#include <errno.h>
#include <json-c/json.h>
#include <limits.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#define QUOTES_PAGE_SIZE 30U

static bool quote_exists(json_object *quotes, const char *quote) {
    size_t count = json_object_array_length(quotes);
    for (size_t index = 0U; index < count; ++index) {
        const char *existing = json_object_get_string(json_object_array_get_idx(quotes, index));
        if (existing != NULL && strcmp(existing, quote) == 0) {
            return true;
        }
    }
    return false;
}

static bool array_add_reference(json_object *array, json_object *value) {
    json_object *reference = json_object_get(value);
    if (json_object_array_add(array, reference) == 0) {
        return true;
    }
    json_object_put(reference);
    return false;
}

static json_object *copy_quotes(json_object *quotes, size_t skipped, const char *addition) {
    json_object *updated = json_object_new_array();
    if (updated == NULL) {
        return NULL;
    }
    size_t count = json_object_array_length(quotes);
    for (size_t index = 0U; index < count; ++index) {
        if (index != skipped &&
            !array_add_reference(updated, json_object_array_get_idx(quotes, index))) {
            json_object_put(updated);
            return NULL;
        }
    }
    if (addition != NULL) {
        json_object *entry = json_object_new_string(addition);
        if (entry == NULL || json_object_array_add(updated, entry) != 0) {
            json_object_put(entry);
            json_object_put(updated);
            return NULL;
        }
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
    json_object *state = json_storage_load_conquister(storage);
    json_object *quotes = json_storage_load_quotes(storage);
    json_object *updated_quotes = NULL;
    bool ok = false;
    if (state == NULL || quotes == NULL) {
        goto cleanup;
    }

    json_object *scores = json_member(state, "scores");
    int64_t score = json_integer(scores, username, 0);
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
    json_object *quotes_added = json_member(state, "quotes_added");
    int64_t added = json_integer(quotes_added, username, 0);
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
    json_object_put(updated_quotes);
    json_object_put(state);
    json_object_put(quotes);
    json_storage_unlock(storage);
    return ok;
}

void quote_page_free(QuotePage *page) {
    if (page == NULL) {
        return;
    }
    if (page->items != NULL) {
        for (size_t index = 0U; index < page->count; ++index) {
            free(page->items[index]);
        }
    }
    free(page->items);
    *page = (QuotePage){0};
}

bool quote_page_load(Storage *storage, int requested_page, QuotePage *page) {
    *page = (QuotePage){0};
    if (!json_storage_lock(storage)) {
        return false;
    }
    json_object *quotes = json_storage_load_quotes(storage);
    bool ok = false;
    if (quotes == NULL) {
        goto cleanup;
    }
    page->total = json_object_array_length(quotes);
    if (page->total == 0U) {
        ok = true;
        goto cleanup;
    }
    page->pages = ((page->total - 1U) / QUOTES_PAGE_SIZE) + 1U;
    page->page = requested_page > 0 ? (size_t)requested_page : 1U;
    if (page->page > page->pages) {
        page->page = page->pages;
    }
    size_t offset = (page->page - 1U) * QUOTES_PAGE_SIZE;
    size_t remaining = page->total - offset;
    page->count = remaining > QUOTES_PAGE_SIZE ? QUOTES_PAGE_SIZE : remaining;
    page->first_number = offset + 1U;
    page->items = calloc(page->count, sizeof(*page->items));
    ok = page->items != NULL;
    for (size_t index = 0U; ok && index < page->count; ++index) {
        const char *quote = json_object_get_string(
            json_object_array_get_idx(quotes, offset + index)
        );
        page->items[index] = string_duplicate(quote);
        ok = page->items[index] != NULL;
    }

cleanup:
    json_object_put(quotes);
    json_storage_unlock(storage);
    if (!ok) {
        quote_page_free(page);
    }
    return ok;
}

bool quote_random(Storage *storage, char **quote) {
    *quote = NULL;
    if (!json_storage_lock(storage)) {
        return false;
    }
    json_object *quotes = json_storage_load_quotes(storage);
    bool ok = false;
    if (quotes == NULL) {
        goto cleanup;
    }
    size_t count = json_object_array_length(quotes);
    ok = true;
    if (count > 0U) {
        size_t index = (size_t)(json_storage_next_quote_random(storage) % count);
        const char *text = json_object_get_string(json_object_array_get_idx(quotes, index));
        *quote = string_duplicate(text);
        ok = *quote != NULL;
    }

cleanup:
    json_object_put(quotes);
    json_storage_unlock(storage);
    return ok;
}

bool quote_delete(Storage *storage, const char *selector, char **removed_quote) {
    *removed_quote = NULL;
    if (!json_storage_lock(storage)) {
        return false;
    }
    json_object *quotes = json_storage_load_quotes(storage);
    json_object *updated = NULL;
    bool ok = false;
    if (quotes == NULL) {
        goto cleanup;
    }
    size_t count = json_object_array_length(quotes);
    size_t selected = SIZE_MAX;
    char *end = NULL;
    errno = 0;
    unsigned long position = strtoul(selector, &end, 10);
    if (errno == 0 && end != selector && *end == '\0' && position > 0U && position <= count) {
        selected = (size_t)(position - 1U);
    } else {
        for (size_t index = 0U; index < count; ++index) {
            const char *quote = json_object_get_string(json_object_array_get_idx(quotes, index));
            if (quote != NULL && strcmp(quote, selector) == 0) {
                selected = index;
                break;
            }
        }
    }
    if (selected == SIZE_MAX) {
        ok = true;
        goto cleanup;
    }

    const char *removed = json_object_get_string(json_object_array_get_idx(quotes, selected));
    updated = copy_quotes(quotes, selected, NULL);
    if (removed != NULL) {
        *removed_quote = string_duplicate(removed);
    }
    ok = updated != NULL && *removed_quote != NULL && json_storage_save_quotes(storage, updated);

cleanup:
    if (!ok) {
        free(*removed_quote);
        *removed_quote = NULL;
    }
    json_object_put(updated);
    json_object_put(quotes);
    json_storage_unlock(storage);
    return ok;
}
