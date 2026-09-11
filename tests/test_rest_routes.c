#include "conquister_service.h"
#include "rest_routes.h"
#include "test_paths.h"

#include <assert.h>
#include <jansson.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void write_quotes(const char *path, const char *quote) {
    json_t *quotes = quote != nullptr ? json_pack("[s]", quote) : json_array();
    assert(quotes != nullptr);
    assert(json_dump_file(quotes, path, JSON_INDENT(2)) == 0);
    json_decref(quotes);
}

int main(void) {
    char conquister_path[1024];
    char quotes_path[1024];
    test_paths("rest-route-test", conquister_path, quotes_path);
    write_quotes(quotes_path, nullptr);

    Storage storage = {};
    assert(storage_open(&storage, conquister_path, quotes_path));
    Arena arena = {};
    DynamicString body = {};
    assert(dynamic_string_init(&body, &arena, 256U));
    RestRouteContext context = {.storage = &storage, .arena = &arena};
    RestRouteResponse response = {.status_code = 0, .body = &body};

    assert(!rest_route_dispatch(nullptr, "GET", "/health", &response));
    assert(rest_route_dispatch(&context, "GET", "/health", &response));
    assert(response.status_code == 200);
    assert(strcmp(body.data, "{\"status\":\"ok\"}\n") == 0);
    assert(rest_route_dispatch(&context, "GET", "/missing", &response));
    assert(response.status_code == 404);
    assert(strcmp(body.data, "{\"error\":\"not found\"}\n") == 0);
    assert(rest_route_dispatch(&context, "POST", "/quote", &response));
    assert(response.status_code == 404);

    assert(rest_route_dispatch(&context, "GET", "/quote", &response));
    assert(response.status_code == 404);
    assert(strstr(body.data, "no quotes available") != nullptr);
    write_quotes(quotes_path, "quote di prova");
    assert(rest_route_dispatch(&context, "GET", "/quote", &response));
    assert(response.status_code == 200);
    assert(strcmp(body.data, "{\"quote\":\"quote di prova\"}\n") == 0);

    assert(rest_route_dispatch(&context, "GET", "/leaderboard", &response));
    assert(response.status_code == 200);
    assert(strcmp(body.data, "{\"entries\":[],\"current\":null}\n") == 0);

    ClaimResult claim;
    assert(conquister_claim(&storage, 7, "Norelec", 100, &claim));
    assert(rest_route_dispatch(&context, "GET", "/leaderboard", &response));
    assert(strcmp(
               body.data,
               "{\"entries\":[],\"current\":{\"username\":\"Norelec\",\"since\":100}}\n"
           ) == 0);
    assert(conquister_claim(&storage, 8, "bob", 150, &claim));
    assert(rest_route_dispatch(&context, "GET", "/leaderboard", &response));
    assert(response.status_code == 200);
    assert(strcmp(
               body.data,
               "{\"entries\":[{\"rank\":1,\"username\":\"Norelec\",\"score\":50,\"quotes_added\":0}],"
               "\"current\":{\"username\":\"bob\",\"since\":150}}\n"
           ) == 0);
    assert(rest_route_dispatch(&context, "GET", "/user/norelec", &response));
    assert(response.status_code == 200);
    assert(strcmp(
               body.data,
               "{\"username\":\"Norelec\",\"score\":50,\"rank\":1,\"quotes_added\":0,"
               "\"in_conquister\":false}\n"
           ) == 0);
    assert(rest_route_dispatch(&context, "GET", "/user/@bob", &response));
    assert(response.status_code == 200);
    assert(strcmp(
               body.data,
               "{\"username\":\"bob\",\"score\":0,\"rank\":null,\"quotes_added\":0,"
               "\"in_conquister\":true,\"since\":150}\n"
           ) == 0);
    assert(rest_route_dispatch(&context, "GET", "/user/nessuno", &response));
    assert(response.status_code == 404);
    assert(strcmp(body.data, "{\"error\":\"user not found\"}\n") == 0);
    assert(rest_route_dispatch(&context, "GET", "/user/", &response));
    assert(response.status_code == 404);
    assert(strcmp(body.data, "{\"error\":\"not found\"}\n") == 0);
    assert(rest_route_dispatch(&context, "GET", "/user/bob/extra", &response));
    assert(response.status_code == 404);
    assert(rest_route_dispatch(&context, "POST", "/user/bob", &response));
    assert(response.status_code == 404);

    arena_free(&arena);
    storage_close(&storage);
    test_paths_remove(conquister_path, quotes_path);
    puts("REST route tests: ok");
    return 0;
}
