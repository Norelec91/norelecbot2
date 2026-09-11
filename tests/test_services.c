#include "conquister_service.h"
#include "quote_service.h"
#include "storage.h"
#include "test_paths.h"

#include <assert.h>
#include <jansson.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void assert_json_formats(const char *conquister_path, const char *quotes_path) {
    json_error_t error;
    json_t *state = json_load_file(conquister_path, 0, &error);
    assert(state != nullptr);
    assert(json_is_object(state));
    assert(json_is_object(json_object_get(state, "current")));
    assert(json_is_object(json_object_get(state, "scores")));
    assert(json_is_object(json_object_get(state, "quotes_added")));
    json_decref(state);

    json_t *quotes = json_load_file(quotes_path, 0, &error);
    assert(quotes != nullptr);
    assert(json_is_array(quotes));
    json_decref(quotes);
}

int main(void) {
    char conquister_path[1024];
    char quotes_path[1024];
    test_paths("telegram-service-test", conquister_path, quotes_path);

    Storage storage = {};
    Arena arena = {};
    assert(storage_open(&storage, conquister_path, quotes_path));

    QuotePage page;
    assert(quote_page_load(&storage, &arena, 1, &page));
    assert(page.total == 0U);
    char *random_quote = nullptr;
    assert(quote_random(&storage, &arena, &random_quote));
    assert(random_quote == nullptr);

    ClaimResult claim;
    assert(conquister_claim(&storage, 1, "alice", 100, &claim));
    assert(claim.status == CLAIM_TAKEN);
    assert(claim.previous_username[0] == '\0');
    assert(conquister_claim(&storage, 2, "bob", 1100, &claim));
    assert(strcmp(claim.previous_username, "alice") == 0);
    assert(claim.earned == 1000);

    Leaderboard leaderboard;
    assert(conquister_leaderboard(&storage, &arena, 10U, &leaderboard));
    assert(leaderboard.count == 1U);
    assert(strcmp(leaderboard.entries[0].username, "alice") == 0);
    assert(leaderboard.entries[0].score == 1000);
    assert(strcmp(leaderboard.current_username, "bob") == 0);
    assert(leaderboard.current_since == 1100);

    ConquisterUser user;
    assert(conquister_user(&storage, "ALICE", &user));
    assert(user.found && strcmp(user.username, "alice") == 0);
    assert(user.score == 1000 && user.rank == 1U && user.quotes_added == 0);
    assert(!user.in_conquister);
    assert(conquister_user(&storage, "bob", &user));
    assert(user.found && user.in_conquister && user.since == 1100);
    assert(user.score == 0 && user.rank == 0U);
    assert(conquister_user(&storage, "carol", &user));
    assert(!user.found);

    QuoteAddResult addition;
    assert(quote_add(&storage, "alice", "quote di prova", 1000, &addition));
    assert(addition.status == QUOTE_ADDED);
    assert(addition.available_score == 0);
    assert(quote_random(&storage, &arena, &random_quote));
    assert(random_quote != nullptr && strcmp(random_quote, "quote di prova") == 0);

    assert(conquister_claim(&storage, 1, "alice", 1101, &claim));
    assert(conquister_claim(&storage, 2, "bob", 2101, &claim));
    assert(quote_add(&storage, "alice", "quote di prova", 1000, &addition));
    assert(addition.status == QUOTE_DUPLICATE);
    assert(quote_page_load(&storage, &arena, 1, &page));
    assert(page.count == 1U);
    assert(strcmp(page.items[0], "quote di prova") == 0);

    char *removed = nullptr;
    assert(quote_delete(&storage, &arena, "1", &removed));
    assert(removed != nullptr && strcmp(removed, "quote di prova") == 0);
    assert(quote_delete(&storage, &arena, "1", &removed));
    assert(removed == nullptr);
    assert(quote_page_load(&storage, &arena, 1, &page));
    assert(page.total == 0U);

    storage_close(&storage);
    assert_json_formats(conquister_path, quotes_path);
    assert(storage_open(&storage, conquister_path, quotes_path));
    assert(conquister_leaderboard(&storage, &arena, 10U, &leaderboard));
    assert(leaderboard.count > 0U);
    assert(strcmp(leaderboard.entries[0].username, "alice") == 0);
    assert(conquister_leaderboard(&storage, &arena, 1U, &leaderboard));
    assert(leaderboard.count == 1U);
    assert(conquister_leaderboard(&storage, &arena, 0U, &leaderboard));
    assert(leaderboard.count == 2U);
    assert(strcmp(leaderboard.entries[1].username, "bob") == 0);
    storage_close(&storage);
    arena_free(&arena);
    test_paths_remove(conquister_path, quotes_path);

    // A quote saved before a failed state save must be rolled back.
    test_paths("rollback-test", conquister_path, quotes_path);
    FILE *file = fopen(quotes_path, "wb");
    assert(file != nullptr);
    int written = fputs("[\"originale\"]", file);
    int closed = fclose(file);
    assert(written >= 0 && closed == 0);
    assert(storage_open(&storage, "rollback-missing-directory/conquister.json", quotes_path));
    assert(!quote_add(&storage, "alice", "nuova", 0, &addition));
    assert(quote_page_load(&storage, &arena, 1, &page));
    assert(page.total == 1U && strcmp(page.items[0], "originale") == 0);
    storage_close(&storage);
    arena_free(&arena);
    test_paths_remove(conquister_path, quotes_path);
    puts("service tests: ok");
    return 0;
}
