#include "arena.h"
#include "dynamic_string.h"
#include "rest_routes.h"

#include <jansson.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size);

static Storage storage;

static void fail(const char *reason, const char *method, const char *path, const char *body) {
    fprintf(stderr, "REST invariant violated: %s (%s %s) -> %s\n", reason, method, path, body);
    abort();
}

static bool open_storage(void) {
    const char *conquister_path = "fuzz-conquister.json";
    const char *quotes_path = "fuzz-quotes.json";
    json_t *state = json_pack(
        "{s:{s:I, s:s, s:I}, s:{s:I, s:I, s:I}, s:{s:I}}",
        "current", "user_id", (json_int_t)1, "username", "Norelec", "since", (json_int_t)100,
        "scores", "Lord_Possum", (json_int_t)7074023, "Norelec", (json_int_t)206989,
        "citt\xC3\xA0", (json_int_t)5,
        "quotes_added", "Norelec", (json_int_t)1
    );
    json_t *quotes = json_pack("[s, s]", "Pillola azzurra", "con \"virgolette\" / e slash");
    bool ok = state != nullptr && quotes != nullptr &&
              json_dump_file(state, conquister_path, 0) == 0 &&
              json_dump_file(quotes, quotes_path, 0) == 0 &&
              storage_open(&storage, conquister_path, quotes_path);
    json_decref(state);
    json_decref(quotes);
    return ok;
}

/* First byte picks the method, the rest is the URL path. */
int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size) {
    static bool ready = false;
    if (!ready) {
        if (!open_storage()) {
            abort();
        }
        ready = true;
    }
    if (size == 0U) {
        return 0;
    }
    static const char *const methods[] = {"GET", "POST", "HEAD", "PUT"};
    const char *method = methods[data[0] % 4U];
    char *path = malloc(size);
    Arena arena = {};
    DynamicString body = {};
    if (path == nullptr || !dynamic_string_init(&body, &arena, 64U)) {
        arena_free(&arena);
        free(path);
        return 0;
    }
    memcpy(path, data + 1, size - 1U);
    path[size - 1U] = '\0';

    RestRouteContext context = {.storage = &storage, .arena = &arena};
    RestRouteResponse response = {.status_code = 0, .body = &body};
    if (!rest_route_dispatch(&context, method, path, &response)) {
        fail("dispatch failed", method, path, body.data);
    }
    if (response.status_code != 200U && response.status_code != 404U) {
        fail("unexpected status", method, path, body.data);
    }
    if (body.length == 0U || body.data[body.length - 1U] != '\n') {
        fail("body does not end with a newline", method, path, body.data);
    }
    json_error_t error;
    json_t *parsed = json_loads(body.data, 0, &error);
    if (parsed == nullptr) {
        fail("body is not valid JSON", method, path, body.data);
    }
    json_decref(parsed);

    arena_free(&arena);
    free(path);
    return 0;
}
