#include "config.h"

#include "dynamic_string.h"
#include "logging.h"
#include "text.h"

#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static constexpr int DEFAULT_API_PORT = 8000;
static constexpr int DEFAULT_QUOTE_COST = 1000;

static bool parse_number(
    const char *value,
    int64_t fallback,
    int64_t minimum,
    int64_t maximum,
    int64_t *number
) {
    int64_t parsed = fallback;
    if ((*value != '\0' && !text_parse_int64(value, &parsed)) || parsed < minimum ||
        parsed > maximum) {
        return false;
    }
    *number = parsed;
    return true;
}

static bool set_token(AppConfig *config, const char *value) {
    return text_copy(config->bot_token, sizeof(config->bot_token), value);
}

static bool set_conquister_enabled(AppConfig *config, const char *value) {
    config->conquister_enabled = *value != '\0' && strcmp(value, "0") != 0 &&
                                 !text_equals_ignore_case(value, "false");
    return true;
}

static bool set_owner_id(AppConfig *config, const char *value) {
    return parse_number(value, 0, INT64_MIN, INT64_MAX, &config->owner_id);
}

static bool set_quote_cost(AppConfig *config, const char *value) {
    int64_t cost = 0;
    if (!parse_number(value, DEFAULT_QUOTE_COST, 0, INT_MAX, &cost)) {
        return false;
    }
    config->quote_cost = (int)cost;
    return true;
}

static bool set_api_host(AppConfig *config, const char *value) {
    return text_copy(config->api_host, sizeof(config->api_host),
                     *value == '\0' ? "0.0.0.0" : value);
}

static bool set_api_port(AppConfig *config, const char *value) {
    int64_t port = 0;
    if (!parse_number(value, DEFAULT_API_PORT, 1, 65535, &port)) {
        return false;
    }
    config->api_port = (int)port;
    return true;
}

static bool set_conquister_file(AppConfig *config, const char *value) {
    return text_copy(config->conquister_path, sizeof(config->conquister_path),
                     *value == '\0' ? "conquister.json" : value);
}

static bool set_quotes_file(AppConfig *config, const char *value) {
    return text_copy(config->quotes_path, sizeof(config->quotes_path),
                     *value == '\0' ? "quotes.json" : value);
}

/* Environment variables are applied in this order. */
static const struct {
    const char *name;
    bool (*apply)(AppConfig *config, const char *value);
} SETTINGS[] = {
    {"NORELECBOT_TELEGRAM_TOKEN", set_token},
    {"NORELECBOT_CONQUISTER_ENABLED", set_conquister_enabled},
    {"NORELECBOT_OWNER_ID", set_owner_id},
    {"NORELECBOT_QUOTE_COST", set_quote_cost},
    {"NORELECBOT_API_HOST", set_api_host},
    {"NORELECBOT_API_PORT", set_api_port},
    {"NORELECBOT_CONQUISTER_FILE", set_conquister_file},
    {"NORELECBOT_QUOTES_FILE", set_quotes_file},
};

static constexpr size_t SETTING_COUNT = sizeof(SETTINGS) / sizeof(SETTINGS[0]);

static bool apply_setting(AppConfig *config, const char *name, const char *value) {
    for (size_t index = 0U; index < SETTING_COUNT; ++index) {
        if (strcmp(name, SETTINGS[index].name) == 0) {
            return SETTINGS[index].apply(config, value);
        }
    }
    return true;
}

static bool read_line(FILE *file, DynamicString *line, bool *available) {
    dynamic_string_reset(line);
    *available = false;
    if (feof(file) != 0) {
        return true;
    }
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
    if (file == nullptr) {
        return false;
    }
    Arena arena = {};
    DynamicString line = {};
    if (!dynamic_string_init(&line, &arena, 256U)) {
        arena_free(&arena);
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
        char *entry = text_trim(line.data);
        if (*entry == '\0' || *entry == '#') {
            continue;
        }
        char *equals = strchr(entry, '=');
        if (equals == nullptr) {
            continue;
        }
        *equals = '\0';
        const char *name = text_trim(entry);
        char *value = text_trim(equals + 1);
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
    arena_free(&arena);
    if (fclose(file) != 0) {
        ok = false;
    }
    return ok;
}

static bool apply_environment(AppConfig *config) {
    for (size_t index = 0U; index < SETTING_COUNT; ++index) {
        const char *value = getenv(SETTINGS[index].name);
        if (value != nullptr && !SETTINGS[index].apply(config, value)) {
            return false;
        }
    }
    return true;
}

bool config_load(AppConfig *config, const char *dotenv_path) {
    if (config == nullptr || dotenv_path == nullptr) {
        return false;
    }
    *config = (AppConfig){
        .quote_cost = DEFAULT_QUOTE_COST,
        .api_port = DEFAULT_API_PORT,
    };
    if (!text_copy(config->api_host, sizeof(config->api_host), "0.0.0.0") ||
        !text_copy(config->conquister_path, sizeof(config->conquister_path), "conquister.json") ||
        !text_copy(config->quotes_path, sizeof(config->quotes_path), "quotes.json")) {
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
