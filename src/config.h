#ifndef NORELECBOT_CONFIG_H
#define NORELECBOT_CONFIG_H

#include <stdbool.h>
#include <stdint.h>

typedef struct {
    char bot_token[256];
    bool conquister_enabled;
    int64_t owner_id;
    int quote_cost;
    char api_host[64];
    int api_port;
    char conquister_path[1024];
    char quotes_path[1024];
} AppConfig;

[[nodiscard]] bool config_load(AppConfig *config, const char *dotenv_path);

#endif
