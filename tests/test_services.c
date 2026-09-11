#include "conquister_service.h"
#include "quote_service.h"
#include "storage.h"
#include "test_paths.h"

#include <assert.h>
#include <json-c/json.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void assert_json_formats(const char *conquister_path, const char *quotes_path) {
    json_object *state = json_object_from_file(conquister_path);
    assert(state != NULL);
    assert(json_object_is_type(state, json_type_object));
    json_object *member = NULL;
    assert(json_object_object_get_ex(state, "current", &member));
    assert(member != NULL && json_object_is_type(member, json_type_object));
    assert(json_object_object_get_ex(state, "scores", &member));
    assert(json_object_is_type(member, json_type_object));
    assert(json_object_object_get_ex(state, "quotes_added", &member));
    assert(json_object_is_type(member, json_type_object));
    json_object_put(state);

    json_object *quotes = json_object_from_file(quotes_path);
    assert(quotes != NULL);
    assert(json_object_is_type(quotes, json_type_array));
    json_object_put(quotes);
}

int main(void) {
    char conquister_path[1024];
    char quotes_path[1024];
    test_paths("telegram-service-test", conquister_path, quotes_path);

    Storage storage = {0};
    assert(storage_open(&storage, conquister_path, quotes_path));

    QuotePage page;
    assert(quote_page_load(&storage, 1, &page));
    assert(page.total == 0U);
    quote_page_free(&page);
    char *random_quote = NULL;
    assert(quote_random(&storage, &random_quote));
    assert(random_quote == NULL);

    ClaimResult claim;
    assert(conquister_claim(&storage, 1, "alice", 100, &claim));
    assert(claim.status == CLAIM_TAKEN);
    assert(claim.previous_username[0] == '\0');
    assert(conquister_claim(&storage, 2, "bob", 1100, &claim));
    assert(strcmp(claim.previous_username, "alice") == 0);
    assert(claim.earned == 1000);

    Leaderboard leaderboard;
    assert(conquister_leaderboard(&storage, &leaderboard));
    assert(leaderboard.count == 1U);
    assert(strcmp(leaderboard.entries[0].username, "alice") == 0);
    assert(leaderboard.entries[0].score == 1000);
    assert(strcmp(leaderboard.current_username, "bob") == 0);
    leaderboard_free(&leaderboard);

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
    assert(quote_random(&storage, &random_quote));
    assert(random_quote != NULL && strcmp(random_quote, "quote di prova") == 0);
    free(random_quote);

    assert(conquister_claim(&storage, 1, "alice", 1101, &claim));
    assert(conquister_claim(&storage, 2, "bob", 2101, &claim));
    assert(quote_add(&storage, "alice", "quote di prova", 1000, &addition));
    assert(addition.status == QUOTE_DUPLICATE);
    assert(quote_page_load(&storage, 1, &page));
    assert(page.count == 1U);
    assert(strcmp(page.items[0], "quote di prova") == 0);
    quote_page_free(&page);

    char *removed = NULL;
    assert(quote_delete(&storage, "1", &removed));
    assert(removed != NULL && strcmp(removed, "quote di prova") == 0);
    free(removed);
    assert(quote_page_load(&storage, 1, &page));
    assert(page.total == 0U);
    quote_page_free(&page);

    storage_close(&storage);
    assert_json_formats(conquister_path, quotes_path);
    assert(storage_open(&storage, conquister_path, quotes_path));
    assert(conquister_leaderboard(&storage, &leaderboard));
    assert(leaderboard.count > 0U);
    assert(strcmp(leaderboard.entries[0].username, "alice") == 0);
    leaderboard_free(&leaderboard);
    storage_close(&storage);

    test_paths_remove(conquister_path, quotes_path);
    puts("service tests: ok");
    return 0;
}
