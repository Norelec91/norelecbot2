#include "telegram.hpp"

#include "logging.hpp"
#include "storage.hpp"
#include "commands.hpp"
#include "irc.hpp"

#include <httplib.h>

#include <algorithm>
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

std::optional<Json> get_updates(const AppConfig &config, std::int64_t offset, int poll_timeout) {
    httplib::Params fields{{"timeout", std::to_string(poll_timeout)}, {"allowed_updates", "[\"message\"]"}};
    if (offset >= 0) {
        fields.emplace("offset", std::to_string(offset));
    }
    return telegram_api(config, "getUpdates", fields, poll_timeout + 4);
}

const Json *find_member(const Json *object, const char *key) {
    if (object == nullptr) {
        return nullptr;
    }
    const auto found = object->find(key);
    return found != object->end() ? &*found : nullptr;
}

const Json *integer_member(const Json *object, const char *key) {
    const Json *value = find_member(object, key);
    return value != nullptr && value->is_number_integer() ? value : nullptr;
}

void process_message(Storage &storage, const AppConfig &config, const Json &message) {
    const Json *text = find_member(&message, "text");
    const Json *sender = find_member(&message, "from");
    const Json *chat_id = integer_member(find_member(&message, "chat"), "id");
    const Json *user_id = integer_member(sender, "id");
    if (text == nullptr || !text->is_string() || chat_id == nullptr || user_id == nullptr) {
        return;
    }
    const Json *username = find_member(sender, "username");
    const std::int64_t chat = chat_id->get<std::int64_t>();
    const std::int64_t sender_id = user_id->get<std::int64_t>();
    const CommandContext context{
        .storage = storage,
        .config = config,
        .user_id = sender_id,
        .username = username != nullptr && username->is_string()
            ? std::string_view{username->get_ref<const std::string &>()}
            : std::string_view{},
        .claims_allowed = config.conquister_chat_id == 0 || chat == config.conquister_chat_id,
        .owner = std::ranges::find(config.owner_ids, sender_id) != config.owner_ids.end(),
    };
    const std::optional<std::string> reply = command_dispatch(context, text->get_ref<const std::string &>());
    if (!reply) {
        return;
    }
    telegram_say(config, chat, *reply);
    /* Telegram does not hand a bot's messages to another bot, so the bridge cannot carry this one. */
    if (config.irc_enabled && chat == config.conquister_chat_id) {
        irc_say(*reply);
    }
}

const Json *updates_array(const std::optional<Json> &root) {
    if (!root) {
        return nullptr;
    }
    const Json *ok = find_member(&*root, "ok");
    const Json *result = find_member(&*root, "result");
    const bool succeeded = ok != nullptr && ok->is_boolean() && ok->get<bool>();
    return succeeded && result != nullptr && result->is_array() ? result : nullptr;
}

void advance_offset(const Json &update, std::int64_t &offset) {
    if (const Json *update_id = integer_member(&update, "update_id")) {
        offset = update_id->get<std::int64_t>() + 1;
    }
}

}

void telegram_say(const AppConfig &config, std::int64_t chat_id, const std::string &text) {
    static_cast<void>(telegram_api(
        config,
        "sendMessage",
        {{"chat_id", std::to_string(chat_id)}, {"text", text}, {"disable_web_page_preview", "true"}},
        poll_timeout_seconds
    ));
}

void telegram_run(Storage &storage, const AppConfig &config, const std::atomic<bool> &stop) {
    log_info("Telegram poller started (trigger={})", conquister_trigger);
    std::int64_t offset = -1;
    const std::optional<Json> backlog = get_updates(config, -1, 0);
    if (const Json *updates = updates_array(backlog); updates != nullptr && !updates->empty()) {
        advance_offset(updates->back(), offset);
    }

    while (!stop.load(std::memory_order_relaxed)) {
        const std::optional<Json> root = get_updates(config, offset, poll_timeout_seconds);
        const Json *updates = updates_array(root);
        if (updates == nullptr) {
            std::this_thread::sleep_for(std::chrono::seconds{1});
            continue;
        }
        for (const Json &update : *updates) {
            advance_offset(update, offset);
            if (const Json *message = find_member(&update, "message"); message != nullptr && message->is_object()) {
                process_message(storage, config, *message);
            }
        }
    }
    log_info("Telegram poller stopped");
}

}
