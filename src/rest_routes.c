#include "rest_routes.h"

#include "conquister_service.h"
#include "quote_service.h"

#include <jansson.h>
#include <string.h>

typedef bool (*RestRouteHandler)(
    const RestRouteContext *context,
    const char *argument,
    RestRouteResponse *response
);

/* A path ending in '/' matches as a prefix and passes the rest of the URL as argument. */
typedef struct {
    const char *method;
    const char *path;
    RestRouteHandler handler;
} RestRouteDefinition;

static bool set_response(RestRouteResponse *response, unsigned int status_code, const char *body) {
    response->status_code = status_code;
    return dynamic_string_append(response->body, body);
}

static int append_chunk(const char *buffer, size_t size, void *body) {
    return dynamic_string_append_n(body, buffer, size) ? 0 : -1;
}

/* Takes ownership of root; a NULL root (failed json_pack) reports an internal error. */
static bool append_json(DynamicString *body, json_t *root) {
    bool ok = root != NULL && json_dump_callback(root, append_chunk, body, JSON_COMPACT) == 0 &&
              dynamic_string_append(body, "\n");
    json_decref(root);
    return ok;
}

static bool json_user_body(DynamicString *body, const ConquisterUser *user) {
    json_t *root = json_pack(
        "{s:s, s:I, s:o, s:I, s:b}",
        "username", user->username,
        "score", (json_int_t)user->score,
        "rank", user->rank > 0U ? json_integer((json_int_t)user->rank) : json_null(),
        "quotes_added", (json_int_t)user->quotes_added,
        "in_conquister", user->in_conquister
    );
    if (root != NULL && user->in_conquister &&
        json_object_set_new(root, "since", json_integer((json_int_t)user->since)) != 0) {
        json_decref(root);
        root = NULL;
    }
    return append_json(body, root);
}

static bool json_leaderboard_body(DynamicString *body, const Leaderboard *leaderboard) {
    json_t *entries = json_array();
    for (size_t index = 0U; entries != NULL && index < leaderboard->count; ++index) {
        const LeaderboardEntry *entry = &leaderboard->entries[index];
        json_t *item = json_pack(
            "{s:I, s:s, s:I, s:I}",
            "rank", (json_int_t)index + 1,
            "username", entry->username,
            "score", (json_int_t)entry->score,
            "quotes_added", (json_int_t)entry->quotes_added
        );
        if (json_array_append_new(entries, item) != 0) {
            json_decref(entries);
            entries = NULL;
        }
    }
    json_t *current = leaderboard->current_username != NULL
        ? json_pack(
              "{s:s, s:I}",
              "username", leaderboard->current_username,
              "since", (json_int_t)leaderboard->current_since
          )
        : json_null();
    return append_json(body, json_pack("{s:o, s:o}", "entries", entries, "current", current));
}

static bool handle_health(
    const RestRouteContext *context,
    const char *argument,
    RestRouteResponse *response
) {
    (void)context;
    (void)argument;
    return set_response(response, 200, "{\"status\":\"ok\"}\n");
}

static bool handle_quote(
    const RestRouteContext *context,
    const char *argument,
    RestRouteResponse *response
) {
    (void)argument;
    char *quote = NULL;
    if (!quote_random(context->storage, context->arena, &quote)) {
        return false;
    }
    if (quote == NULL) {
        return set_response(response, 404, "{\"error\":\"no quotes available\"}\n");
    }
    response->status_code = 200;
    return append_json(response->body, json_pack("{s:s}", "quote", quote));
}

static bool handle_leaderboard(
    const RestRouteContext *context,
    const char *argument,
    RestRouteResponse *response
) {
    (void)argument;
    Leaderboard leaderboard;
    if (!conquister_leaderboard(context->storage, context->arena, 0U, &leaderboard)) {
        return false;
    }
    response->status_code = 200;
    return json_leaderboard_body(response->body, &leaderboard);
}

static bool handle_user(
    const RestRouteContext *context,
    const char *argument,
    RestRouteResponse *response
) {
    const char *username = *argument == '@' ? argument + 1 : argument;
    if (*username == '\0' || strchr(username, '/') != NULL) {
        return set_response(response, 404, "{\"error\":\"not found\"}\n");
    }
    ConquisterUser user;
    if (!conquister_user(context->storage, username, &user)) {
        return false;
    }
    if (!user.found) {
        return set_response(response, 404, "{\"error\":\"user not found\"}\n");
    }
    response->status_code = 200;
    return json_user_body(response->body, &user);
}

static const RestRouteDefinition REST_ROUTES[] = {
    {"GET", "/health", handle_health},
    {"GET", "/quote", handle_quote},
    {"GET", "/leaderboard", handle_leaderboard},
    {"GET", "/user/", handle_user},
};

bool rest_route_dispatch(
    const RestRouteContext *context,
    const char *method,
    const char *path,
    RestRouteResponse *response
) {
    if (context == NULL || context->storage == NULL || context->arena == NULL || method == NULL ||
        path == NULL || response == NULL || response->body == NULL) {
        return false;
    }
    dynamic_string_reset(response->body);
    response->status_code = 500;

    size_t route_count = sizeof(REST_ROUTES) / sizeof(REST_ROUTES[0]);
    for (size_t index = 0U; index < route_count; ++index) {
        const RestRouteDefinition *route = &REST_ROUTES[index];
        if (strcmp(method, route->method) != 0) {
            continue;
        }
        size_t length = strlen(route->path);
        bool prefix = route->path[length - 1U] == '/';
        if (prefix && strncmp(path, route->path, length) == 0) {
            return route->handler(context, path + length, response);
        }
        if (!prefix && strcmp(path, route->path) == 0) {
            return route->handler(context, "", response);
        }
    }
    return set_response(response, 404, "{\"error\":\"not found\"}\n");
}
