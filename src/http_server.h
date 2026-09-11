#ifndef NORELECBOT_HTTP_SERVER_H
#define NORELECBOT_HTTP_SERVER_H

#include "storage.h"

#include <stdbool.h>

struct MHD_Daemon;

typedef struct {
    struct MHD_Daemon *daemon;
    Storage *storage;
} HttpServer;

[[nodiscard]] bool http_server_start(
    HttpServer *server,
    const char *host,
    int port,
    Storage *storage
);
void http_server_stop(HttpServer *server);

#endif
