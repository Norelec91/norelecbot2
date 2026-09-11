#include "config.h"

#include "dynamic_string.h"
#include "logging.h"

#include <ctype.h>
#include <errno.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define DEFAULT_API_PORT 8000
#define DEFAULT_QUOTE_COST 1000

static char *trim(char *text) {
    while (isspace((unsigned char)*text) != 0) {
        ++text;
    }
    char *end = text + strlen(text);
    while (end > text && isspace((unsigned char)end[-1]) != 0) {
        --end;
    }
    *end = '\0';
    return text;
}

static bool text_equals_ignore_case(const char *left, const char *right) {
    while (*left != '\0' && *right != '\0') {
        if (tolower((unsigned char)*left) != tolower((unsigned char)*right)) {
            return false;
        }
        ++left;
        ++right;
    }
    return *left == *right;
}

static bool copy_text(char *destination, size_t capacity, const char *text) {
    int length = snprintf(destination, capacity, "%s", text);
    return length >= 0 && (size_t)length < capacity;
}

static bool parse_integer(const char *text, int64_t *output) {
    char *end = NULL;
    errno = 0;
    long long value = strtoll(text, &end, 10);
    while (isspace((unsigned char)*end) != 0) {
        ++end;
    }
    if (errno != 0 || end == text || *end != '\0') {
        return false;
    }
    *output = (int64_t)value;
    return true;
}

static bool apply_setting(AppConfig *config, const char *name, const char *value) {
    if (strcmp(name, "NORELECBOT_TELEGRAM_TOKEN") == 0) {
        return copy_text(config->bot_token, sizeof(config->bot_token), value);
    }
    if (strcmp(name, "NORELECBOT_CONQUISTER_ENABLED") == 0) {
        config->conquister_enabled = *value != '\0' && strcmp(value, "0") != 0 &&
                                     !text_equals_ignore_case(value, "false");
        return true;
    }
    if (strcmp(name, "NORELECBOT_OWNER_ID") == 0) {
        if (*value == '\0') {
            config->owner_id = 0;
            return true;
        }
        return parse_integer(value, &config->owner_id);
    }
    if (strcmp(name, "NORELECBOT_QUOTE_COST") == 0) {
        int64_t cost = DEFAULT_QUOTE_COST;
        if (*value != '\0' && !parse_integer(value, &cost)) {
            return false;
        }
        if (cost < 0 || cost > INT_MAX) {
            return false;
        }
        config->quote_cost = (int)cost;
        return true;
    }
    if (strcmp(name, "NORELECBOT_API_HOST") == 0) {
        return copy_text(
            config->api_host,
            sizeof(config->api_host),
            *value == '\0' ? "0.0.0.0" : value
        );
    }
    if (strcmp(name, "NORELECBOT_API_PORT") == 0) {
        int64_t port = DEFAULT_API_PORT;
        if (*value != '\0' && !parse_integer(value, &port)) {
            return false;
        }
        if (port < 1 || port > 65535) {
            return false;
        }
        config->api_port = (int)port;
        return true;
    }
    if (strcmp(name, "NORELECBOT_CONQUISTER_FILE") == 0) {
        return copy_text(
            config->conquister_path,
            sizeof(config->conquister_path),
            *value == '\0' ? "conquister.json" : value
        );
    }
    if (strcmp(name, "NORELECBOT_QUOTES_FILE") == 0) {
        return copy_text(
            config->quotes_path,
            sizeof(config->quotes_path),
            *value == '\0' ? "quotes.json" : value
        );
    }
    return true;
}

static bool read_line(FILE *file, DynamicString *line, bool *available) {
    dynamic_string_reset(line);
    int character = fgetc(file);
    while (character != EOF && character != '\n') {
        char byte = (char)character;
        if (!dynamic_string_append_n(line, &byte, 1U)) {
            return false;
        }
        character = fgetc(file);
    }
    if (ferror(file) != 0) {
        return false;
    }
    *available = character != EOF || line->length > 0U;
    return true;
}

static bool apply_dotenv(AppConfig *config, const char *path) {
    FILE *file = fopen(path, "r");
    if (file == NULL) {
        return false;
    }
    DynamicString line = {0};
    if (!dynamic_string_init(&line, 256U)) {
        (void)fclose(file);
        return false;
    }

    bool ok = true;
    bool available = false;
    while (ok) {
        if (!read_line(file, &line, &available)) {
            ok = false;
            break;
        }
        if (!available) {
            break;
        }
        char *entry = trim(line.data);
        if (*entry == '\0' || *entry == '#') {
            continue;
        }
        char *equals = strchr(entry, '=');
        if (equals == NULL) {
            continue;
        }
        *equals = '\0';
        char *name = trim(entry);
        char *value = trim(equals + 1);
        size_t length = strlen(value);
        if (length >= 2U && ((value[0] == '"' && value[length - 1U] == '"') ||
                            (value[0] == '\'' && value[length - 1U] == '\''))) {
            value[length - 1U] = '\0';
            ++value;
        }
        ok = *name == '\0' || apply_setting(config, name, value);
    }
    if (ferror(file) != 0) {
        ok = false;
    }
    dynamic_string_free(&line);
    if (fclose(file) != 0) {
        ok = false;
    }
    return ok;
}

static bool apply_environment(AppConfig *config) {
    static const char *const names[] = {
        "NORELECBOT_TELEGRAM_TOKEN",
        "NORELECBOT_CONQUISTER_ENABLED",
        "NORELECBOT_OWNER_ID",
        "NORELECBOT_QUOTE_COST",
        "NORELECBOT_API_HOST",
        "NORELECBOT_API_PORT",
        "NORELECBOT_CONQUISTER_FILE",
        "NORELECBOT_QUOTES_FILE",
    };
    size_t count = sizeof(names) / sizeof(names[0]);
    for (size_t index = 0U; index < count; ++index) {
        const char *value = getenv(names[index]);
        if (value != NULL && !apply_setting(config, names[index], value)) {
            return false;
        }
    }
    return true;
}

bool config_load(AppConfig *config, const char *dotenv_path) {
    if (config == NULL || dotenv_path == NULL) {
        return false;
    }
    *config = (AppConfig){
        .quote_cost = DEFAULT_QUOTE_COST,
        .api_port = DEFAULT_API_PORT,
    };
    if (!copy_text(config->api_host, sizeof(config->api_host), "0.0.0.0") ||
        !copy_text(config->conquister_path, sizeof(config->conquister_path), "conquister.json") ||
        !copy_text(config->quotes_path, sizeof(config->quotes_path), "quotes.json")) {
        log_error("Could not initialize configuration defaults");
        return false;
    }
    if (!apply_dotenv(config, dotenv_path)) {
        log_error("Required configuration file '%s' is missing, unreadable, or invalid", dotenv_path);
        return false;
    }
    if (!apply_environment(config)) {
        log_error("Invalid environment configuration");
        return false;
    }
    return true;
}
