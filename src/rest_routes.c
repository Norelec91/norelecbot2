#include "rest_routes.h"

#include "conquister_service.h"
#include "quote_service.h"

#include <json-c/json.h>
#include <stdint.h>
#include <stdlib.h>
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

static bool append_json(DynamicString *body, json_object *root) {
    const char *encoded = json_object_to_json_string_ext(root, JSON_C_TO_STRING_PLAIN);
    return encoded != NULL && dynamic_string_append(body, encoded) &&
           dynamic_string_append(body, "\n");
}

static bool add_member(json_object *root, const char *name, json_object *value) {
    if (value == NULL) {
        return false;
    }
    json_object_object_add(root, name, value);
    return true;
}

static bool json_quote_body(DynamicString *body, const char *quote) {
    json_object *root = json_object_new_object();
    if (root == NULL) {
        return false;
    }
    bool ok = add_member(root, "quote", json_object_new_string(quote)) && append_json(body, root);
    json_object_put(root);
    return ok;
}

static bool json_user_body(DynamicString *body, const ConquisterUser *user) {
    json_object *root = json_object_new_object();
    if (root == NULL) {
        return false;
    }
    bool ok = add_member(root, "username", json_object_new_string(user->username)) &&
              add_member(root, "score", json_object_new_int64(user->score));
    if (ok && user->rank > 0U) {
        ok = add_member(root, "rank", json_object_new_int64((int64_t)user->rank));
    } else if (ok) {
        json_object_object_add(root, "rank", NULL);
    }
    ok = ok && add_member(root, "quotes_added", json_object_new_int64(user->quotes_added)) &&
         add_member(root, "in_conquister", json_object_new_boolean(user->in_conquister ? 1 : 0));
    if (ok && user->in_conquister) {
        ok = add_member(root, "since", json_object_new_int64(user->since));
    }
    ok = ok && append_json(body, root);
    json_object_put(root);
    return ok;
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
    if (!quote_random(context->storage, &quote)) {
        return false;
    }
    if (quote == NULL) {
        return set_response(response, 404, "{\"error\":\"no quotes available\"}\n");
    }
    response->status_code = 200;
    bool ok = json_quote_body(response->body, quote);
    free(quote);
    return ok;
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
    {"GET", "/user/", handle_user},
};

bool rest_route_dispatch(
    const RestRouteContext *context,
    const char *method,
    const char *path,
    RestRouteResponse *response
) {
    if (context == NULL || context->storage == NULL || method == NULL || path == NULL ||
        response == NULL || response->body == NULL) {
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
