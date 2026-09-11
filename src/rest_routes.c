#include "rest_routes.h"

#include "quote_service.h"

#include <json-c/json.h>
#include <stdlib.h>
#include <string.h>

typedef bool (*RestRouteHandler)(
    const RestRouteContext *context,
    RestRouteResponse *response
);

typedef struct {
    const char *method;
    const char *path;
    RestRouteHandler handler;
} RestRouteDefinition;

static bool set_response(RestRouteResponse *response, unsigned int status_code, const char *body) {
    response->status_code = status_code;
    return dynamic_string_append(response->body, body);
}

static bool json_quote_body(DynamicString *body, const char *quote) {
    json_object *root = json_object_new_object();
    json_object *value = json_object_new_string(quote);
    if (root == NULL || value == NULL) {
        if (value != NULL) {
            json_object_put(value);
        }
        if (root != NULL) {
            json_object_put(root);
        }
        return false;
    }
    json_object_object_add(root, "quote", value);
    const char *encoded = json_object_to_json_string_ext(root, JSON_C_TO_STRING_PLAIN);
    bool ok = encoded != NULL && dynamic_string_append(body, encoded) &&
              dynamic_string_append(body, "\n");
    json_object_put(root);
    return ok;
}

static bool handle_health(
    const RestRouteContext *context,
    RestRouteResponse *response
) {
    (void)context;
    return set_response(response, 200, "{\"status\":\"ok\"}\n");
}

static bool handle_quote(
    const RestRouteContext *context,
    RestRouteResponse *response
) {
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

static const RestRouteDefinition REST_ROUTES[] = {
    {"GET", "/health", handle_health},
    {"GET", "/quote", handle_quote},
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
        if (strcmp(method, REST_ROUTES[index].method) == 0 &&
            strcmp(path, REST_ROUTES[index].path) == 0) {
            return REST_ROUTES[index].handler(context, response);
        }
    }
    return set_response(response, 404, "{\"error\":\"not found\"}\n");
}
