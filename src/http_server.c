#if !defined(_WIN32) && !defined(_POSIX_C_SOURCE)
#define _POSIX_C_SOURCE 200112L
#endif

#include "http_server.h"

#include "dynamic_string.h"
#include "logging.h"
#include "rest_routes.h"

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <winsock2.h>
#include <ws2tcpip.h>
#else
#include <netdb.h>
#include <sys/socket.h>
#endif

#include <microhttpd.h>

#include <stdint.h>
#include <stdio.h>
#include <string.h>

static bool network_initialize(void) {
#ifdef _WIN32
    WSADATA data;
    return WSAStartup(MAKEWORD(2, 2), &data) == 0;
#else
    return true;
#endif
}

static void network_cleanup(void) {
#ifdef _WIN32
    (void)WSACleanup();
#endif
}

static enum MHD_Result queue_json_response(
    struct MHD_Connection *connection,
    unsigned int status_code,
    const DynamicString *body
) {
    struct MHD_Response *response = MHD_create_response_from_buffer(
        body->length,
        body->data,
        MHD_RESPMEM_MUST_COPY
    );
    if (response == nullptr) {
        return MHD_NO;
    }
    enum MHD_Result headers_ok = MHD_add_response_header(
        response,
        MHD_HTTP_HEADER_CONTENT_TYPE,
        "application/json; charset=utf-8"
    );
    if (headers_ok == MHD_YES) {
        headers_ok = MHD_add_response_header(
            response,
            MHD_HTTP_HEADER_CACHE_CONTROL,
            "no-store"
        );
    }
    enum MHD_Result result = headers_ok == MHD_YES
        ? MHD_queue_response(connection, status_code, response)
        : MHD_NO;
    MHD_destroy_response(response);
    return result;
}

static enum MHD_Result handle_request(
    void *context,
    struct MHD_Connection *connection,
    const char *url,
    const char *method,
    [[maybe_unused]] const char *version,
    [[maybe_unused]] const char *upload_data,
    [[maybe_unused]] size_t *upload_data_size,
    [[maybe_unused]] void **request_context
) {

    const HttpServer *server = context;
    DynamicString body = {};
    if (!dynamic_string_init(&body, 256U)) {
        return MHD_NO;
    }
    Arena arena = {};
    RestRouteContext route_context = {.storage = server->storage, .arena = &arena};
    RestRouteResponse route_response = {.status_code = 500, .body = &body};
    bool response_ready = rest_route_dispatch(
        &route_context,
        method,
        url,
        &route_response
    );
    arena_free(&arena);
    if (!response_ready) {
        route_response.status_code = 500;
        dynamic_string_reset(&body);
        if (!dynamic_string_append(&body, "{\"error\":\"storage error\"}\n")) {
            dynamic_string_free(&body);
            return MHD_NO;
        }
    }

    enum MHD_Result result = queue_json_response(
        connection,
        route_response.status_code,
        &body
    );
    dynamic_string_free(&body);
    return result;
}

static bool resolve_address(
    const char *host,
    int port,
    struct sockaddr_storage *address,
    unsigned int *daemon_flags
) {
    char service[16];
    (void)snprintf(service, sizeof(service), "%d", port);
    struct addrinfo hints = {};
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_flags = AI_PASSIVE | AI_NUMERICSERV;

    struct addrinfo *addresses = nullptr;
    int status = getaddrinfo(host, service, &hints, &addresses);
    if (status != 0) {
        log_error("API server address resolution failed with code %d", status);
        return false;
    }

    const struct addrinfo *selected = addresses;
    while (selected != nullptr && selected->ai_addrlen > sizeof(*address)) {
        selected = selected->ai_next;
    }
    if (selected == nullptr) {
        freeaddrinfo(addresses);
        log_error("API server found no usable address for %s", host);
        return false;
    }

    memset(address, 0, sizeof(*address));
    memcpy(address, selected->ai_addr, selected->ai_addrlen);
    if (selected->ai_family == AF_INET6) {
        *daemon_flags |= MHD_USE_IPv6;
    }
    freeaddrinfo(addresses);
    return true;
}

bool http_server_start(
    HttpServer *server,
    const char *host,
    int port,
    Storage *storage
) {
    *server = (HttpServer){};
    server->storage = storage;
    if (!network_initialize()) {
        log_error("Could not initialize networking");
        return false;
    }

    struct sockaddr_storage address;
    unsigned int flags = MHD_USE_INTERNAL_POLLING_THREAD | MHD_USE_ERROR_LOG;
    if (!resolve_address(host, port, &address, &flags)) {
        network_cleanup();
        return false;
    }

    server->daemon = MHD_start_daemon(
        flags,
        (uint16_t)port,
        nullptr,
        nullptr,
        handle_request,
        server,
        MHD_OPTION_SOCK_ADDR,
        (const struct sockaddr *)&address,
        MHD_OPTION_CONNECTION_TIMEOUT,
        2U,
        MHD_OPTION_END
    );
    if (server->daemon == nullptr) {
        network_cleanup();
        log_error("API server could not listen on %s:%d", host, port);
        return false;
    }

    log_info("API server listening on %s:%d", host, port);
    return true;
}

void http_server_stop(HttpServer *server) {
    if (server->daemon == nullptr) {
        return;
    }
    MHD_stop_daemon(server->daemon);
    server->daemon = nullptr;
    network_cleanup();
}
