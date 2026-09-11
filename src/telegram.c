#include "telegram.h"

#include "dynamic_string.h"
#include "logging.h"
#include "platform.h"
#include "telegram_commands.h"

#include <curl/curl.h>
#include <jansson.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define POLL_TIMEOUT_SECONDS 6L

static size_t receive_data(char *data, size_t size, size_t count, void *context) {
    if (size != 0U && count > SIZE_MAX / size) {
        return 0U;
    }
    size_t bytes = size * count;
    DynamicString *response = context;
    return dynamic_string_append_n(response, data, bytes) ? bytes : 0U;
}

static bool form_field(DynamicString *form, CURL *curl, const char *name, const char *value) {
    char *encoded = curl_easy_escape(curl, value, 0);
    if (encoded == NULL) {
        return false;
    }
    bool ok = (form->length == 0U || dynamic_string_append(form, "&")) &&
              dynamic_string_appendf(form, "%s=%s", name, encoded);
    curl_free(encoded);
    return ok;
}

static json_t *telegram_api(
    const char *token,
    const char *method,
    const char *const names[],
    const char *const values[],
    size_t field_count,
    long timeout
) {
    CURL *curl = curl_easy_init();
    if (curl == NULL) {
        log_error("Could not initialize libcurl");
        return NULL;
    }

    DynamicString url = {0};
    DynamicString form = {0};
    DynamicString response = {0};
    bool ready = dynamic_string_init(&url, 384U) && dynamic_string_init(&form, 256U) &&
                 dynamic_string_init(&response, 1024U) &&
                 dynamic_string_appendf(&url, "https://api.telegram.org/bot%s/%s", token, method);
    for (size_t index = 0U; ready && index < field_count; ++index) {
        ready = form_field(&form, curl, names[index], values[index]);
    }
    json_t *root = NULL;
    if (!ready) {
        log_error("Out of memory while preparing Telegram request");
        goto cleanup;
    }

    (void)curl_easy_setopt(curl, CURLOPT_URL, url.data);
    (void)curl_easy_setopt(curl, CURLOPT_POSTFIELDS, form.data);
    (void)curl_easy_setopt(curl, CURLOPT_POSTFIELDSIZE_LARGE, (curl_off_t)form.length);
    (void)curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, receive_data);
    (void)curl_easy_setopt(curl, CURLOPT_WRITEDATA, &response);
    (void)curl_easy_setopt(curl, CURLOPT_TIMEOUT, timeout);
    (void)curl_easy_setopt(curl, CURLOPT_NOSIGNAL, 1L);
    CURLcode status = curl_easy_perform(curl);
    long http_status = 0L;
    (void)curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &http_status);
    if (status != CURLE_OK) {
        log_warning("Telegram %s failed: %s", method, curl_easy_strerror(status));
    } else if (http_status < 200L || http_status >= 300L) {
        log_warning("Telegram %s returned HTTP %ld: %.200s", method, http_status, response.data);
    } else {
        json_error_t error;
        root = json_loads(response.data, 0, &error);
        if (root == NULL) {
            log_warning("Telegram %s returned invalid JSON: %s", method, error.text);
        }
    }

cleanup:
    dynamic_string_free(&url);
    dynamic_string_free(&form);
    dynamic_string_free(&response);
    curl_easy_cleanup(curl);
    return root;
}

static void send_message(const AppConfig *config, int64_t chat_id, const char *text) {
    char chat_id_text[32];
    (void)snprintf(chat_id_text, sizeof(chat_id_text), "%lld", (long long)chat_id);
    const char *names[] = {"chat_id", "text", "disable_web_page_preview"};
    const char *values[] = {chat_id_text, text, "true"};
    json_decref(telegram_api(
        config->bot_token,
        "sendMessage",
        names,
        values,
        3U,
        POLL_TIMEOUT_SECONDS
    ));
}

static json_t *get_updates(const AppConfig *config, int64_t offset, long poll_timeout) {
    char timeout[16];
    char offset_text[32];
    (void)snprintf(timeout, sizeof(timeout), "%ld", poll_timeout);
    (void)snprintf(offset_text, sizeof(offset_text), "%lld", (long long)offset);
    const char *names[] = {"timeout", "allowed_updates", "offset"};
    const char *values[] = {timeout, "[\"message\"]", offset_text};
    return telegram_api(
        config->bot_token,
        "getUpdates",
        names,
        values,
        offset >= 0 ? 3U : 2U,
        poll_timeout + 4L
    );
}

static void process_message(Storage *storage, const AppConfig *config, json_t *message) {
    const char *text = json_string_value(json_object_get(message, "text"));
    json_t *sender = json_object_get(message, "from");
    json_t *chat_id_value = json_object_get(json_object_get(message, "chat"), "id");
    json_t *user_id_value = json_object_get(sender, "id");
    if (text == NULL || !json_is_integer(chat_id_value) || !json_is_integer(user_id_value)) {
        return;
    }
    int64_t chat_id = (int64_t)json_integer_value(chat_id_value);
    DynamicString reply = {0};
    if (!dynamic_string_init(&reply, 1024U)) {
        return;
    }
    Arena arena = {0};
    TelegramCommandContext context = {
        .storage = storage,
        .arena = &arena,
        .config = config,
        .user_id = (int64_t)json_integer_value(user_id_value),
        .username = json_string_value(json_object_get(sender, "username")),
    };
    TelegramCommandResult result = telegram_command_dispatch(&context, text, &reply);
    arena_free(&arena);
    if (result == TELEGRAM_COMMAND_REPLIED) {
        send_message(config, chat_id, reply.data);
    } else if (result == TELEGRAM_COMMAND_ERROR) {
        send_message(config, chat_id, "Errore interno: riprova tra poco.");
    }
    dynamic_string_free(&reply);
}

static json_t *updates_array(json_t *root) {
    json_t *result = json_object_get(root, "result");
    return json_is_true(json_object_get(root, "ok")) && json_is_array(result) ? result : NULL;
}

static void advance_offset(json_t *update, int64_t *offset) {
    json_t *update_id = json_object_get(update, "update_id");
    if (json_is_integer(update_id)) {
        *offset = (int64_t)json_integer_value(update_id) + 1;
    }
}

int telegram_run(Storage *storage, const AppConfig *config, const volatile sig_atomic_t *stop) {
    log_info("Telegram poller started (trigger=%s)", TELEGRAM_CONQUISTER_TRIGGER);
    int64_t offset = -1;
    json_t *backlog = get_updates(config, -1, 0L);
    json_t *array = updates_array(backlog);
    size_t backlog_count = json_array_size(array);
    if (backlog_count > 0U) {
        advance_offset(json_array_get(array, backlog_count - 1U), &offset);
    }
    json_decref(backlog);

    while (*stop == 0) {
        json_t *root = get_updates(config, offset, POLL_TIMEOUT_SECONDS);
        array = updates_array(root);
        if (array == NULL) {
            json_decref(root);
            platform_sleep_milliseconds(1000UL);
            continue;
        }
        size_t index;
        json_t *update;
        json_array_foreach(array, index, update) {
            advance_offset(update, &offset);
            json_t *message = json_object_get(update, "message");
            if (json_is_object(message)) {
                process_message(storage, config, message);
            }
        }
        json_decref(root);
    }
    log_info("Telegram poller stopped");
    return 0;
}
