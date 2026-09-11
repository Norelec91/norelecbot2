#ifndef NORELECBOT_REST_ROUTES_H
#define NORELECBOT_REST_ROUTES_H

#include "dynamic_string.h"
#include "storage.h"

#include <stdbool.h>

typedef struct {
    Storage *storage;
} RestRouteContext;

typedef struct {
    unsigned int status_code;
    DynamicString *body;
} RestRouteResponse;

/* Produces a complete HTTP response, including 404; false means an internal failure. */
bool rest_route_dispatch(
    const RestRouteContext *context,
    const char *method,
    const char *path,
    RestRouteResponse *response
);

#endif
