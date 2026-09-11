#include "rest_routes.h"
#include "test_paths.h"

#include <assert.h>
#include <json-c/json.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void write_quotes(const char *path, const char *quote) {
    json_object *quotes = json_object_new_array();
    assert(quotes != NULL);
    if (quote != NULL) {
        assert(json_object_array_add(quotes, json_object_new_string(quote)) == 0);
    }
    assert(json_object_to_file_ext(path, quotes, JSON_C_TO_STRING_PRETTY) == 0);
    json_object_put(quotes);
}

int main(void) {
    char conquister_path[1024];
    char quotes_path[1024];
    test_paths("rest-route-test", conquister_path, quotes_path);
    write_quotes(quotes_path, NULL);

    Storage storage = {0};
    assert(storage_open(&storage, conquister_path, quotes_path));
    DynamicString body = {0};
    assert(dynamic_string_init(&body, 256U));
    RestRouteContext context = {.storage = &storage};
    RestRouteResponse response = {.status_code = 0, .body = &body};

    assert(!rest_route_dispatch(NULL, "GET", "/health", &response));
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
    assert(strstr(body.data, "no quotes available") != NULL);
    write_quotes(quotes_path, "quote di prova");
    assert(rest_route_dispatch(&context, "GET", "/quote", &response));
    assert(response.status_code == 200);
    assert(strcmp(body.data, "{\"quote\":\"quote di prova\"}\n") == 0);

    dynamic_string_free(&body);
    storage_close(&storage);
    test_paths_remove(conquister_path, quotes_path);
    puts("REST route tests: ok");
    return 0;
}
