#include "telegram.hpp"

#include "logging.hpp"
#include "storage.hpp"
#include "commands.hpp"
#include "game.hpp"
#include "irc.hpp"
#include "pulse.hpp"
#include "sentinel.hpp"
#include "text.hpp"

#include <httplib.h>

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <format>
#include <map>
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
        .admin = std::ranges::find(config.admin_ids, sender_id) != config.admin_ids.end(),
    };
    const std::string &said = text->get_ref<const std::string &>();
    const std::int64_t now = std::chrono::duration_cast<std::chrono::seconds>(
        std::chrono::system_clock::now().time_since_epoch()).count();
    if (config.conquister_chat_id != 0 && chat == config.conquister_chat_id) {
        sentinel().heard(sentinel_identity(sender_id, {}), {}, command_is_for_bot(said), now);
        if (!command_is_for_bot(said)) {
            pulse().message("tg", said, now);
        }
    }
    /* What the sentinel saw, only to an owner and only in private. */
    if (text::trim(said) == "/sospetti" && chat == sender_id && context.owner) {
        const std::map<std::string, std::string> names = player_names(storage);
        telegram_say(config, chat, sentinel().report(now, [&names](const std::string &key) {
            const auto found = names.find(key);
            return found != names.end() ? found->second : key;
        }));
        return;
    }
    const std::optional<std::string> reply = command_dispatch(context, said);
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

void telegram_photo(const AppConfig &config, std::int64_t chat_id, const Picture &picture, const std::string &text) {
    /* Telegram cuts a caption at 1024 characters: a longer text goes on its own, after the picture. */
    constexpr std::size_t caption_bytes = 900;
    const std::string caption = text.size() <= caption_bytes ? text : std::string{};
    static std::mutex known_guard;
    static std::map<std::string, std::string, std::less<>> known_ids;
    std::string known;
    {
        const std::scoped_lock lock{known_guard};
        if (const auto found = known_ids.find(picture.name); found != known_ids.end()) {
            known = found->second;
        }
    }
    bool sent = false;
    if (!known.empty()) {
        sent = telegram_api(config, "sendPhoto", {{"chat_id", std::to_string(chat_id)}, {"photo", known},
                                                  {"caption", caption}}, poll_timeout_seconds).has_value();
    }
    if (!sent) {
        httplib::Client client{"https://api.telegram.org"};
        const std::chrono::seconds timeout{poll_timeout_seconds};
        client.set_connection_timeout(timeout);
        client.set_read_timeout(timeout);
        client.set_write_timeout(timeout);
        const httplib::MultipartFormDataItems items{
            {"chat_id", std::to_string(chat_id), "", ""},
            {"caption", caption, "", ""},
            {"photo", std::string{picture.bytes}, std::string{picture.file_name}, std::string{picture.mime}},
        };
        const httplib::Result result = client.Post(std::format("/bot{}/sendPhoto", config.bot_token), items);
        if (!result || result->status < 200 || result->status >= 300) {
            log_warning("Telegram sendPhoto {} failed: {}", picture.name,
                        result ? std::to_string(result->status) : httplib::to_string(result.error()));
        } else {
            sent = true;
            try {
                /* The largest size is the last: its id sends the same picture again without uploading it. */
                const Json reply = Json::parse(result->body);
                const Json &sizes = reply.at("result").at("photo");
                if (sizes.is_array() && !sizes.empty()) {
                    const std::scoped_lock lock{known_guard};
                    known_ids.insert_or_assign(std::string{picture.name},
                                               sizes.back().at("file_id").get<std::string>());
                }
            } catch (const std::exception &error) {
                log_warning("Telegram sendPhoto {} returned no id: {}", picture.name, error.what());
            }
        }
    }
    /* Whatever happened to the picture, the words get there. */
    if (!sent || caption.empty()) {
        telegram_say(config, chat_id, text);
    }
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
