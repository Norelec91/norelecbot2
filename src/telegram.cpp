#include "telegram.hpp"

#include "json.hpp"
#include "logging.hpp"
#include "telegram_commands.hpp"

#include <httplib.h>

#include <chrono>
#include <cstdint>
#include <format>
#include <optional>
#include <string>
#include <thread>

namespace norelecbot {
namespace {

constexpr int poll_timeout_seconds = 6;

std::optional<Json> telegram_api(
    const AppConfig &config,
    std::string_view method,
    const httplib::Params &fields,
    int timeout_seconds
) {
    httplib::Client client{"https://api.telegram.org"};
    const std::chrono::seconds timeout{timeout_seconds};
    client.set_connection_timeout(timeout);
    client.set_read_timeout(timeout);
    client.set_write_timeout(timeout);
    const httplib::Result result = client.Post(std::format("/bot{}/{}", config.bot_token, method), fields);
    if (!result) {
        log_warning("Telegram {} failed: {}", method, httplib::to_string(result.error()));
        return std::nullopt;
    }
    if (result->status < 200 || result->status >= 300) {
        log_warning(
            "Telegram {} returned HTTP {}: {}",
            method,
            result->status,
            std::string_view{result->body}.substr(0, 200)
        );
        return std::nullopt;
    }
    try {
        return Json::parse(result->body);
    } catch (const Json::parse_error &error) {
        log_warning("Telegram {} returned invalid JSON: {}", method, error.what());
        return std::nullopt;
    }
}

void send_message(const AppConfig &config, std::int64_t chat_id, const std::string &text) {
    telegram_api(
        config,
        "sendMessage",
        {{"chat_id", std::to_string(chat_id)}, {"text", text}, {"disable_web_page_preview", "true"}},
        poll_timeout_seconds
    );
}

std::optional<Json> get_updates(const AppConfig &config, std::int64_t offset, int poll_timeout) {
    httplib::Params fields{{"timeout", std::to_string(poll_timeout)}, {"allowed_updates", "[\"message\"]"}};
    if (offset >= 0) {
        fields.emplace("offset", std::to_string(offset));
    }
    return telegram_api(config, "getUpdates", fields, poll_timeout + 4);
}

const Json *integer_member(const Json *object, const char *key) {
    const Json *value = find_member(object, key);
    return value != nullptr && value->is_number_integer() ? value : nullptr;
}

void process_message(Storage &storage, const AppConfig &config, const Json &message) {
    const Json *text = find_member(message, "text");
    const Json *sender = find_member(message, "from");
    const Json *chat_id = integer_member(find_member(message, "chat"), "id");
    const Json *user_id = integer_member(sender, "id");
    if (text == nullptr || !text->is_string() || chat_id == nullptr || user_id == nullptr) {
        return;
    }
    const Json *username = find_member(sender, "username");
    const CommandContext context{
        .storage = storage,
        .config = config,
        .chat_id = chat_id->get<std::int64_t>(),
        .user_id = user_id->get<std::int64_t>(),
        .username = username != nullptr && username->is_string()
            ? std::string_view{username->get_ref<const std::string &>()}
            : std::string_view{},
    };
    std::string reply;
    const CommandResult result = telegram_command_dispatch(context, text->get_ref<const std::string &>(), reply);
    if (result == CommandResult::replied) {
        send_message(config, context.chat_id, reply);
    } else if (result == CommandResult::error) {
        send_message(config, context.chat_id, "Errore interno: riprova tra poco.");
    }
}

const Json *updates_array(const std::optional<Json> &root) {
    if (!root) {
        return nullptr;
    }
    const Json *ok = find_member(*root, "ok");
    const Json *result = find_member(*root, "result");
    const bool succeeded = ok != nullptr && ok->is_boolean() && ok->get<bool>();
    return succeeded && result != nullptr && result->is_array() ? result : nullptr;
}

void advance_offset(const Json &update, std::int64_t &offset) {
    if (const Json *update_id = integer_member(&update, "update_id")) {
        offset = update_id->get<std::int64_t>() + 1;
    }
}

}

void telegram_run(Storage &storage, const AppConfig &config, const volatile std::sig_atomic_t &stop) {
    log_info("Telegram poller started (trigger={})", conquister_trigger);
    std::int64_t offset = -1;
    const std::optional<Json> backlog = get_updates(config, -1, 0);
    if (const Json *updates = updates_array(backlog); updates != nullptr && !updates->empty()) {
        advance_offset(updates->back(), offset);
    }

    while (stop == 0) {
        const std::optional<Json> root = get_updates(config, offset, poll_timeout_seconds);
        const Json *updates = updates_array(root);
        if (updates == nullptr) {
            std::this_thread::sleep_for(std::chrono::seconds{1});
            continue;
        }
        for (const Json &update : *updates) {
            advance_offset(update, offset);
            if (const Json *message = find_member(update, "message"); message != nullptr && message->is_object()) {
                process_message(storage, config, *message);
            }
        }
    }
    log_info("Telegram poller stopped");
}

}
