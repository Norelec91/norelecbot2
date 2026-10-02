#include "storage.hpp"

#include "logging.hpp"

#include <algorithm>
#include <cerrno>
#include <chrono>
#include <exception>
#include <filesystem>
#include <format>
#include <fstream>
#include <iterator>
#include <system_error>

namespace norelecbot {
namespace {

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

std::int64_t integer(const Json &value) {
    if (!value.is_number_integer()) {
        throw std::invalid_argument(std::format("expected an integer, found {}", value.type_name()));
    }
    return value.get<std::int64_t>();
}

Counters parse_counters(const Json &state, const char *name) {
    Counters counters;
    const auto table = state.find(name);
    if (table == state.end()) {
        return counters;
    }
    if (!table->is_object()) {
        throw std::invalid_argument(std::format("{} is not an object", name));
    }
    for (const auto &entry : table->items()) {
        counters.emplace(entry.key(), integer(entry.value()));
    }
    return counters;
}

/* Retired timed balloons become ordinary balloons, preserving paid items from old saves. */
Counters parse_balloons(const Json &state) {
    Counters balloons = parse_counters(state, "balloons");
    const std::int64_t now = std::chrono::duration_cast<std::chrono::seconds>(
        std::chrono::system_clock::now().time_since_epoch()).count();
    for (const auto &[player, expires] : parse_counters(state, "shields")) {
        if (expires > now && balloons.find(player) == balloons.end()) {
            balloons[player] = 0;
        }
    }
    return balloons;
}

Authors parse_authors(const Json &state, const char *name) {
    Authors authors;
    const auto section = state.find(name);
    if (section == state.end() || !section->is_object()) {
        return authors;
    }
    for (const auto &entry : section->items()) {
        if (entry.value().is_string()) {
            authors.emplace(entry.key(), entry.value().get<std::string>());
        }
    }
    return authors;
}

Slots parse_slots(const Json &state, const char *name) {
    Slots slots;
    const auto section = state.find(name);
    if (section == state.end() || !section->is_object()) {
        return slots;
    }
    for (const auto &entry : section->items()) {
        if (!entry.value().is_array()) {
            continue;
        }
        std::vector<std::int64_t> numbers;
        for (const Json &slot : entry.value()) {
            numbers.push_back(integer(slot));
        }
        slots.emplace(entry.key(), std::move(numbers));
    }
    return slots;
}

std::vector<Raid> parse_raids(const Json &state) {
    std::vector<Raid> raids;
    const auto section = state.find("raids");
    if (section == state.end() || !section->is_array()) {
        return raids;
    }
    for (const Json &entry : *section) {
        if (!entry.is_object()) {
            continue;
        }
        raids.push_back(Raid{
            .raider = entry.at("raider").get<std::string>(),
            .target = entry.at("target").get<std::string>(),
            .arrive = integer(entry.at("arrive")),
            .back = integer(entry.at("back")),
            .arrived = entry.at("arrived").get<bool>(),
            .loot = integer(entry.at("loot")),
            .gift = entry.contains("gift") ? integer(entry.at("gift")) : 0,
            .gift_emoji = entry.contains("gift_emoji") ? entry.at("gift_emoji").get<std::string>() : std::string{},
        });
    }
    return raids;
}

std::vector<Pregnancy> parse_pregnancies(const Json &state) {
    std::vector<Pregnancy> pregnancies;
    const auto section = state.find("pregnancies");
    if (section == state.end() || !section->is_array()) {
        return pregnancies;
    }
    for (const Json &entry : *section) {
        if (!entry.is_object()) {
            continue;
        }
        pregnancies.push_back(Pregnancy{
            .mother = entry.at("mother").get<std::string>(),
            .father = entry.at("father").get<std::string>(),
            .due = integer(entry.at("due")),
        });
    }
    return pregnancies;
}

std::vector<Child> parse_children(const Json &state) {
    std::vector<Child> children;
    const auto section = state.find("children");
    if (section == state.end() || !section->is_array()) {
        return children;
    }
    for (const Json &entry : *section) {
        if (!entry.is_object()) {
            continue;
        }
        children.push_back(Child{
            .owner = entry.at("owner").get<std::string>(),
            .slot = integer(entry.at("slot")),
            .male = entry.at("male").get<bool>(),
            .born = integer(entry.at("born")),
            .paid = entry.contains("paid") ? integer(entry.at("paid")) : 0,
        });
    }
    return children;
}

/* Missing sections count as empty, like in the original C version. */
ConquisterState parse_state(const Json &json) {
    if (!json.is_object()) {
        throw std::invalid_argument("unexpected structure");
    }
    std::optional<Holder> holder;
    if (const auto current = json.find("current"); current != json.end() && !current->is_null()) {
        holder = Holder{
            .user_id = integer(current->at("user_id")),
            .username = current->at("username").get<std::string>(),
            .since = integer(current->at("since")),
            /* Older saves kept a whole multiplier: x3 is 300%. */
            .lightning_percent = current->contains("lightning_percent") ? integer(current->at("lightning_percent"))
                : current->contains("multiplier") && integer(current->at("multiplier")) > 1
                    ? integer(current->at("multiplier")) * 100 : 0,
            .bolts = current->contains("bolts") ? integer(current->at("bolts")) : 0,
            .banked = current->contains("banked") ? integer(current->at("banked")) : 0,
            .counted_from = current->contains("counted_from") ? integer(current->at("counted_from")) : 0,
            .lobsters = {},
        };
        if (const auto lobsters = current->find("lobsters"); lobsters != current->end()) {
            if (!lobsters->is_object()) {
                throw std::invalid_argument("lobsters is not an object");
            }
            for (const auto &entry : lobsters->items()) {
                holder->lobsters[std::stoul(entry.key())] = entry.value().get<std::string>();
            }
        }
    }
    ConquisterState state{
        std::move(holder),
        parse_counters(json, "scores"),
        parse_counters(json, "quotes_added"),
        parse_balloons(json),
        parse_counters(json, "cooldowns"),
        parse_counters(json, "ids"),
        parse_counters(json, "telegram_ids"),
        parse_counters(json, "irc_names"),
        parse_authors(json, "accounts"),
        parse_authors(json, "display_names"),
        parse_authors(json, "telegram_names"),
        parse_authors(json, "irc_nicks"),
        parse_authors(json, "link_requests"),
        parse_raids(json),
        parse_authors(json, "quote_authors"),
        parse_authors(json, "furniture"),
        parse_counters(json, "debugging"),
        parse_counters(json, "smeared"),
        parse_slots(json, "stayed"),
        parse_counters(json, "welcomed"),
        parse_counters(json, "balloon_ported"),
        parse_counters(json, "flung"),
        parse_pregnancies(json),
        parse_children(json),
        json.contains("eggs_at") ? integer(json.at("eggs_at")) : 0,
    };
    return state;
}

Json state_to_json(const ConquisterState &state) {
    Json raids = Json::array();
    std::ranges::transform(state.raids, std::back_inserter(raids), [](const Raid &raid) {
        return Json{
            {"raider", raid.raider},
            {"target", raid.target},
            {"arrive", raid.arrive},
            {"back", raid.back},
            {"arrived", raid.arrived},
            {"loot", raid.loot},
            {"gift", raid.gift},
            {"gift_emoji", raid.gift_emoji},
        };
    });
    Json pregnancies = Json::array();
    std::ranges::transform(state.pregnancies, std::back_inserter(pregnancies), [](const Pregnancy &pregnancy) {
        return Json{{"mother", pregnancy.mother}, {"father", pregnancy.father}, {"due", pregnancy.due}};
    });
    Json children = Json::array();
    std::ranges::transform(state.children, std::back_inserter(children), [](const Child &child) {
        return Json{{"owner", child.owner}, {"slot", child.slot}, {"male", child.male}, {"born", child.born},
                    {"paid", child.paid}};
    });
    Json current = nullptr;
    if (state.current) {
        current = Json{
            {"user_id", state.current->user_id},
            {"username", state.current->username},
            {"since", state.current->since},
            {"lightning_percent", state.current->lightning_percent},
            {"bolts", state.current->bolts},
            {"banked", state.current->banked},
            {"counted_from", state.current->counted_from},
        };
        if (!state.current->lobsters.empty()) {
            Json lobsters = Json::object();
            for (const auto &[slot, emoji] : state.current->lobsters) {
                lobsters[std::to_string(slot)] = emoji;
            }
            current["lobsters"] = std::move(lobsters);
        }
    }
    return Json{
        {"current", std::move(current)},
        {"scores", state.scores},
        {"quotes_added", state.quotes_added},
        {"balloons", state.balloons},
        {"cooldowns", state.cooldowns},
        {"ids", state.ids},
        {"telegram_ids", state.telegram_ids},
        {"irc_names", state.irc_names},
        {"accounts", state.accounts},
        {"display_names", state.display_names},
        {"telegram_names", state.telegram_names},
        {"irc_nicks", state.irc_nicks},
        {"link_requests", state.link_requests},
        {"raids", std::move(raids)},
        {"quote_authors", state.quote_authors},
        {"furniture", state.furniture},
        {"debugging", state.debugging},
        {"smeared", state.smeared},
        {"stayed", state.stayed},
        {"welcomed", state.welcomed},
        {"balloon_ported", state.balloon_ported},
        {"flung", state.flung},
        {"pregnancies", std::move(pregnancies)},
        {"children", std::move(children)},
        {"eggs_at", state.eggs_at},
    };
}

std::optional<ConquisterState> load_state(const std::string &path, bool *has_legacy_shields = nullptr) {
    if (file_missing(path, "Conquister state")) {
        return ConquisterState{};
    }
    std::string error;
    if (const std::optional<Json> json = parse_file(path, error)) {
        try {
            ConquisterState state = parse_state(*json);
            if (has_legacy_shields != nullptr) {
                /* Timed balloons, bought raid shields, raid resistance and bought boosts are gone: rewrite
                   without them. */
                *has_legacy_shields = json->contains("shields") || json->contains("raid_shields") ||
                    json->contains("raid_resistance_levels") || json->contains("raid_resistance_since") ||
                    json->contains("boosts");
            }
            return state;
        } catch (const std::exception &failure) {
            error = failure.what();
        }
    }
    log_error("Conquister state {} is not valid: {}", path, error);
    return std::nullopt;
}

std::optional<Quotes> load_quotes(const std::string &path) {
    if (file_missing(path, "quote collection")) {
        return Quotes{};
    }
    std::string error = "unexpected structure";
    const std::optional<Json> json = parse_file(path, error);
    if (!json || !json->is_array()) {
        log_error("Quote collection {} is not a JSON array: {}", path, error);
        return std::nullopt;
    }
    const bool valid = std::all_of(json->begin(), json->end(), [](const Json &entry) {
        return entry.is_string() && !entry.get_ref<const std::string &>().empty();
    });
    if (!valid) {
        log_error("Quote collection {} contains an invalid entry", path);
        return std::nullopt;
    }
    return json->get<Quotes>();
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

}

Storage::Storage(std::string conquister_path, std::string quotes_path)
    : conquister_path_(std::move(conquister_path)),
      quotes_path_(std::move(quotes_path)),
      random_(std::random_device{}()) {
    bool has_legacy_shields = false;
    const std::optional<ConquisterState> state = load_state(conquister_path_, &has_legacy_shields);
    const bool quotes_valid = load_quotes(quotes_path_).has_value();
    if (!state || !quotes_valid) {
        throw StorageError("JSON storage could not be loaded");
    }
    if (has_legacy_shields && !save_json(conquister_path_, state_to_json(*state))) {
        throw StorageError("Game state could not be migrated");
    }
    log_info("JSON storage ready (conquister={}, quotes={})", conquister_path_, quotes_path_);
}

StorageSession::StorageSession(Storage &storage) : storage_(storage) {}

ConquisterState &StorageSession::state() {
    if (!state_) {
        const std::optional<ConquisterState> loaded = load_state(storage_.conquister_path_);
        if (!loaded) {
            throw StorageError("Conquister state could not be loaded");
        }
        state_.emplace(*loaded, *loaded);
    }
    return state_->current;
}

Quotes &StorageSession::quotes() {
    if (!quotes_) {
        const std::optional<Quotes> loaded = load_quotes(storage_.quotes_path_);
        if (!loaded) {
            throw StorageError("Quote collection could not be loaded");
        }
        quotes_.emplace(*loaded, *loaded);
    }
    return quotes_->current;
}

std::size_t StorageSession::random_index(std::size_t count) {
    std::uniform_int_distribution<std::size_t> distribution{0, count - 1};
    return distribution(storage_.random_);
}

void StorageSession::backup(std::string_view tag) const {
    const std::string &path = storage_.conquister_path_;
    if (file_missing(path, "Conquister state")) {
        return;
    }
    std::error_code error;
    std::filesystem::copy_file(path, std::format("{}.{}", path, tag),
                               std::filesystem::copy_options::overwrite_existing, error);
    if (error) {
        log_error("Could not back up {}: {}", path, error.message());
        throw StorageError("Conquister state backup failed");
    }
}

void StorageSession::save() const {
    bool quotes_saved = false;
    if (quotes_ && quotes_->current != quotes_->original) {
        if (!save_json(storage_.quotes_path_, Json(quotes_->current))) {
            throw StorageError("Quote collection not saved");
        }
        quotes_saved = true;
    }
    if (state_ && state_->current != state_->original &&
        !save_json(storage_.conquister_path_, state_to_json(state_->current))) {
        if (quotes_saved && quotes_ && !save_json(storage_.quotes_path_, Json(quotes_->original))) {
            log_error("Could not roll back quote collection after Conquister save failure");
        }
        throw StorageError("Conquister state not saved");
    }
}

}
