#include "storage.hpp"

#include "logging.hpp"
#include "text.hpp"

#include <algorithm>
#include <cerrno>
#include <filesystem>
#include <format>
#include <fstream>
#include <iterator>
#include <system_error>
#include <utility>

namespace norelecbot {
namespace {

bool ensure_object_member(Json &parent, const char *name) {
    if (const Json *value = find_member(parent, name)) {
        return value->is_object();
    }
    parent[name] = Json::object();
    return true;
}

bool normalize_conquister(Json &state) {
    if (!state.is_object()) {
        return false;
    }
    const Json *current = find_member(state, "current");
    if (current == nullptr) {
        state["current"] = nullptr;
    } else if (!current->is_null() && !current->is_object()) {
        return false;
    }
    return ensure_object_member(state, "scores") && ensure_object_member(state, "quotes_added");
}

bool file_missing(const std::string &path, std::string_view description) {
    std::error_code error;
    const std::filesystem::file_status status = std::filesystem::status(path, error);
    if (status.type() == std::filesystem::file_type::not_found) {
        return true;
    }
    if (error) {
        log_error("Could not access {} {}: {}", description, path, error.message());
    }
    return false;
}

std::optional<Json> parse_file(const std::string &path, std::string &error) {
    std::ifstream file{path, std::ios::binary};
    if (!file) {
        error = std::format("unable to open {}", path);
        return std::nullopt;
    }
    try {
        return Json::parse(file);
    } catch (const Json::parse_error &failure) {
        error = failure.what();
        return std::nullopt;
    }
}

std::optional<Json> load_state(const std::string &path) {
    if (file_missing(path, "Conquister state")) {
        return Json{{"current", nullptr}, {"scores", Json::object()}, {"quotes_added", Json::object()}};
    }
    std::string error = "unexpected structure";
    std::optional<Json> state = parse_file(path, error);
    if (!state || !normalize_conquister(*state)) {
        log_error("Conquister state {} is not valid: {}", path, error);
        return std::nullopt;
    }
    return state;
}

std::optional<Json> load_quotes(const std::string &path) {
    if (file_missing(path, "quote collection")) {
        return Json::array();
    }
    std::string error = "unexpected structure";
    std::optional<Json> quotes = parse_file(path, error);
    if (!quotes || !quotes->is_array()) {
        log_error("Quote collection {} is not a JSON array: {}", path, error);
        return std::nullopt;
    }
    const bool valid = std::all_of(quotes->begin(), quotes->end(), [](const Json &entry) {
        return entry.is_string() && !entry.get_ref<const std::string &>().empty();
    });
    if (!valid) {
        log_error("Quote collection {} contains an invalid entry", path);
        return std::nullopt;
    }
    return quotes;
}

bool save_json(const std::string &path, const Json &value) {
    const std::string text = value.dump(2);
    const std::string temporary = path + ".tmp";
    std::error_code ignored;
    std::ofstream file{temporary, std::ios::binary | std::ios::trunc};
    file << text;
    file.close();
    if (!file) {
        const int saved_errno = errno;
        std::filesystem::remove(temporary, ignored);
        log_error(
            "Could not write JSON file {}: {}",
            temporary,
            std::generic_category().message(saved_errno)
        );
        return false;
    }
    std::error_code error;
    std::filesystem::rename(temporary, path, error);
    if (error) {
        std::filesystem::remove(temporary, ignored);
        log_error("Could not replace JSON file {}", path);
        return false;
    }
    return true;
}

std::int64_t integer_or(const Json *value, std::int64_t fallback) {
    return value != nullptr && value->is_number_integer() ? value->get<std::int64_t>() : fallback;
}

std::optional<std::string> find_key_ignore_case(const Json *object, std::string_view name) {
    if (object == nullptr || !object->is_object()) {
        return std::nullopt;
    }
    const auto items = object->items();
    const auto found = std::ranges::find_if(items, [name](const auto &entry) {
        return text::equals_ignore_case(entry.key(), name);
    });
    if (found == items.end()) {
        return std::nullopt;
    }
    return found.key();
}

}

Storage::Storage(std::string conquister_path, std::string quotes_path)
    : conquister_path_(std::move(conquister_path)),
      quotes_path_(std::move(quotes_path)),
      random_(std::random_device{}()) {
    {
        [[maybe_unused]] const StorageTransaction validation{*this, StorageDocuments::all};
    }
    log_info("JSON storage ready (conquister={}, quotes={})", conquister_path_, quotes_path_);
}

StorageTransaction::StorageTransaction(Storage &storage, StorageDocuments documents)
    : storage_(storage), lock_(storage.mutex_) {
    bool loaded = true;
    if (documents != StorageDocuments::quotes) {
        std::optional<Json> state = load_state(storage.conquister_path_);
        loaded = state.has_value();
        if (state) {
            state_ = std::move(*state);
        }
    }
    if (documents != StorageDocuments::state) {
        std::optional<Json> quotes = load_quotes(storage.quotes_path_);
        loaded = loaded && quotes.has_value();
        if (quotes) {
            quotes_ = std::move(*quotes);
        }
    }
    if (!loaded) {
        throw StorageError("JSON storage could not be loaded");
    }
}

void StorageTransaction::commit() {
    if (updated_quotes_ && !save_json(storage_.quotes_path_, *updated_quotes_)) {
        throw StorageError("Quote collection not saved");
    }
    if (state_changed_ && !save_json(storage_.conquister_path_, state_)) {
        if (updated_quotes_ && !save_json(storage_.quotes_path_, quotes_)) {
            log_error("Could not roll back quote collection after Conquister save failure");
        }
        throw StorageError("Conquister state not saved");
    }
}

std::string_view StorageTransaction::holder() const {
    const Json *username = find_member(find_member(state_, "current"), "username");
    if (username == nullptr || !username->is_string()) {
        return {};
    }
    return username->get_ref<const std::string &>();
}

std::int64_t StorageTransaction::holder_since(std::int64_t fallback) const {
    return integer_or(find_member(find_member(state_, "current"), "since"), fallback);
}

void StorageTransaction::set_holder(std::int64_t user_id, const std::string &username, std::int64_t now) {
    state_["current"] = Json{{"user_id", user_id}, {"username", username}, {"since", now}};
    state_changed_ = true;
}

std::int64_t StorageTransaction::score(const std::string &username) const {
    return integer_or(find_member(find_member(state_, "scores"), username.c_str()), 0);
}

void StorageTransaction::set_score(const std::string &username, std::int64_t value) {
    state_["scores"][username] = value;
    state_changed_ = true;
}

std::int64_t StorageTransaction::quotes_added(const std::string &username) const {
    return integer_or(find_member(find_member(state_, "quotes_added"), username.c_str()), 0);
}

void StorageTransaction::set_quotes_added(const std::string &username, std::int64_t count) {
    state_["quotes_added"][username] = count;
    state_changed_ = true;
}

std::optional<std::string> StorageTransaction::find_score(std::string_view username) const {
    return find_key_ignore_case(find_member(state_, "scores"), username);
}

std::optional<std::string> StorageTransaction::find_quotes_added(std::string_view username) const {
    return find_key_ignore_case(find_member(state_, "quotes_added"), username);
}

std::vector<ScoreEntry> StorageTransaction::scores() const {
    std::vector<ScoreEntry> entries;
    if (const Json *table = find_member(state_, "scores"); table != nullptr && table->is_object()) {
        entries.reserve(table->size());
        const auto items = table->items();
        std::ranges::transform(items, std::back_inserter(entries), [](const auto &entry) {
            return ScoreEntry{entry.key(), integer_or(&entry.value(), 0)};
        });
    }
    return entries;
}

const Json &StorageTransaction::quote_list() const {
    return updated_quotes_ ? *updated_quotes_ : quotes_;
}

Json &StorageTransaction::writable_quotes() {
    if (!updated_quotes_) {
        updated_quotes_ = quotes_;
    }
    return *updated_quotes_;
}

std::size_t StorageTransaction::quotes_count() const {
    return quote_list().size();
}

std::string_view StorageTransaction::quote(std::size_t index) const {
    const Json &quotes = quote_list();
    if (index >= quotes.size() || !quotes[index].is_string()) {
        return {};
    }
    return quotes[index].get_ref<const std::string &>();
}

void StorageTransaction::append_quote(const std::string &text) {
    writable_quotes().push_back(text);
}

void StorageTransaction::remove_quote(std::size_t index) {
    Json &quotes = writable_quotes();
    if (index >= quotes.size()) {
        throw StorageError("Quote not removed");
    }
    quotes.erase(index);
}

std::size_t StorageTransaction::random_index(std::size_t count) {
    std::uniform_int_distribution<std::size_t> distribution{0, count - 1};
    return distribution(storage_.random_);
}

}
