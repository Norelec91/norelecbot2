#include "telegram.h"

#include "dynamic_string.h"
#include "logging.h"
#include "platform.h"
#include "telegram_commands.h"

#include <curl/curl.h>
#include <json-c/json.h>
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

static json_object *telegram_api(
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
    json_object *root = NULL;
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
        root = json_tokener_parse(response.data);
        if (root == NULL) {
            log_warning("Telegram %s returned invalid JSON", method);
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
    json_object *response = telegram_api(
        config->bot_token,
        "sendMessage",
        names,
        values,
        3U,
        POLL_TIMEOUT_SECONDS
    );
    json_object_put(response);
}

static json_object *get_updates(const AppConfig *config, int64_t offset, long poll_timeout) {
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

static json_object *object_member(json_object *object, const char *name) {
    json_object *value = NULL;
    return object != NULL && json_object_object_get_ex(object, name, &value)
        ? value
        : NULL;
}

static void process_message(Storage *storage, const AppConfig *config, json_object *message) {
    json_object *text_object = object_member(message, "text");
    if (text_object == NULL || !json_object_is_type(text_object, json_type_string)) {
        return;
    }
    const char *text = json_object_get_string(text_object);
    json_object *chat = object_member(message, "chat");
    json_object *sender = object_member(message, "from");
    json_object *chat_id_object = object_member(chat, "id");
    json_object *user_id_object = object_member(sender, "id");
    json_object *username_object = object_member(sender, "username");
    if (chat_id_object == NULL || user_id_object == NULL) {
        return;
    }
    int64_t chat_id = json_object_get_int64(chat_id_object);
    int64_t user_id = json_object_get_int64(user_id_object);
    const char *username = username_object != NULL
        ? json_object_get_string(username_object)
        : NULL;
    DynamicString reply = {0};
    if (!dynamic_string_init(&reply, 1024U)) {
        return;
    }
    TelegramCommandContext context = {
        .storage = storage,
        .config = config,
        .user_id = user_id,
        .username = username,
    };
    TelegramCommandResult result = telegram_command_dispatch(&context, text, &reply);
    if (result == TELEGRAM_COMMAND_REPLIED) {
        send_message(config, chat_id, reply.data);
    } else if (result == TELEGRAM_COMMAND_ERROR) {
        send_message(config, chat_id, "Errore interno: riprova tra poco.");
    }
    dynamic_string_free(&reply);
}

static json_object *updates_array(json_object *root) {
    json_object *ok = object_member(root, "ok");
    json_object *result = object_member(root, "result");
    return ok != NULL && json_object_get_boolean(ok) != 0 &&
           result != NULL && json_object_is_type(result, json_type_array)
        ? result
        : NULL;
}

int telegram_run(Storage *storage, const AppConfig *config, volatile sig_atomic_t *stop) {
    log_info("Telegram poller started (trigger=%s)", TELEGRAM_CONQUISTER_TRIGGER);
    int64_t offset = -1;
    json_object *backlog = get_updates(config, -1, 0L);
    json_object *array = updates_array(backlog);
    if (array != NULL && json_object_array_length(array) > 0U) {
        json_object *last = json_object_array_get_idx(array, json_object_array_length(array) - 1U);
        json_object *last_id = object_member(last, "update_id");
        if (last_id != NULL) {
            offset = json_object_get_int64(last_id) + 1;
        }
    }
    json_object_put(backlog);

    while (*stop == 0) {
        json_object *root = get_updates(config, offset, POLL_TIMEOUT_SECONDS);
        array = updates_array(root);
        if (array == NULL) {
            json_object_put(root);
            platform_sleep_milliseconds(1000UL);
            continue;
        }
        size_t count = json_object_array_length(array);
        for (size_t index = 0U; index < count; ++index) {
            json_object *update = json_object_array_get_idx(array, index);
            json_object *update_id = object_member(update, "update_id");
            if (update_id != NULL) {
                offset = json_object_get_int64(update_id) + 1;
            }
            json_object *message = object_member(update, "message");
            if (message != NULL) {
                process_message(storage, config, message);
            }
        }
        json_object_put(root);
    }
    log_info("Telegram poller stopped");
    return 0;
}
