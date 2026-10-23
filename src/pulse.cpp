#include "pulse.hpp"

#include "logging.hpp"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <array>
#include <cctype>
#include <exception>
#include <filesystem>
#include <fstream>
#include <system_error>
#include <utility>

namespace norelecbot {
namespace {

using Json = nlohmann::json;

constexpr std::int64_t bucket_seconds = 5 * 60;
constexpr std::int64_t remembered_seconds = 90 * 24 * 60 * 60;

std::string lowered(std::string_view text) {
    std::string low{text};
    std::ranges::transform(low, low.begin(), [](char letter) {
        return static_cast<char>(std::tolower(static_cast<unsigned char>(letter)));
    });
    return low;
}

/* The parts of the game a message may name, each with the words that name it. */
struct Topic {
    std::string_view name;
    std::array<std::string_view, 4> words;
};
constexpr std::array<Topic, 12> named{{
    {"raid", {"raid", "rubat", "rubar", "razzi"}},
    {"conquister", {"conquister", "posto", "entrat", "entrar"}},
    {"balloon", {"🎈", "palloncin", "pallonc", "bucat"}},
    {"ice", {"🧊", "ghiacci", "congel", "frozen"}},
    {"bomb", {"💣", "bomba", "esplos", "☢"}},
    {"poo", {"💩", "smerd", "cacca", "merda"}},
    {"shop", {"/buy", "compr", "negozio", "costa"}},
    {"palle", {"palle", "palla", "punti", "classifica"}},
    {"children", {"💦", "figli", "bambin", "nato"}},
    {"zodiac", {"zodiac", "segno", "oroscop", "pianet"}},
    {"bot", {"script", "automat", "bot ", "barare"}},
    {"rules", {"regol", "aggiorn", "novità", "cambiat"}},
}};

}

bool laughs(std::string_view text) {
    const std::string low = lowered(text);
    for (const std::string_view laugh : {"ahah", "haha", "ahha", "jaja", "lol", "xd", "😂", "🤣", "😆", "😹", "💀"}) {
        if (low.find(laugh) != std::string::npos) {
            return true;
        }
    }
    return false;
}

std::vector<std::string> topics(std::string_view text) {
    const std::string low = lowered(text) + " ";
    std::vector<std::string> found;
    for (const Topic &topic : named) {
        if (std::ranges::any_of(topic.words, [&low](std::string_view word) {
                return !word.empty() && low.find(word) != std::string::npos;
            })) {
            found.emplace_back(topic.name);
        }
    }
    return found;
}

void Pulse::count(const std::string &what, std::int64_t now) {
    const std::int64_t bucket = now / bucket_seconds * bucket_seconds;
    ++buckets_[bucket][what];
    /* Whatever is older than it remembers goes, a stretch at a time. */
    while (!buckets_.empty() && buckets_.begin()->first < now - remembered_seconds) {
        buckets_.erase(buckets_.begin());
    }
}

void Pulse::message(std::string_view platform, std::string_view text, std::int64_t now) {
    const std::scoped_lock lock{mutex_};
    const std::string side{platform};
    count("messages:" + side, now);
    if (laughs(text)) {
        count("laughs:" + side, now);
    }
    for (const std::string &topic : topics(text)) {
        count("topic:" + topic, now);
    }
}

void Pulse::event(std::string_view kind, std::int64_t now) {
    const std::scoped_lock lock{mutex_};
    count("event:" + std::string{kind}, now);
}

std::map<std::string, std::int64_t> Pulse::at(std::int64_t now) const {
    const std::scoped_lock lock{mutex_};
    const auto found = buckets_.find(now / bucket_seconds * bucket_seconds);
    return found != buckets_.end() ? found->second : std::map<std::string, std::int64_t>{};
}

void Pulse::load(const std::string &path) {
    std::ifstream file{path, std::ios::binary};
    if (!file) {
        return;
    }
    try {
        const Json saved = Json::parse(file);
        const std::scoped_lock lock{mutex_};
        for (const auto &[bucket, counts] : saved.items()) {
            buckets_[std::stoll(bucket)] = counts.get<std::map<std::string, std::int64_t>>();
        }
    } catch (const std::exception &error) {
        log_warning("The pulse file {} could not be read: {}", path, error.what());
    }
}

void Pulse::save(const std::string &path) const {
    if (path.empty()) {
        return;
    }
    Json saved = Json::object();
    {
        const std::scoped_lock lock{mutex_};
        for (const auto &[bucket, counts] : buckets_) {
            saved[std::to_string(bucket)] = counts;
        }
    }
    const std::string temporary = path + ".tmp";
    {
        std::ofstream file{temporary, std::ios::binary | std::ios::trunc};
        if (!file) {
            log_warning("The pulse file {} could not be written", temporary);
            return;
        }
        file << saved.dump();
    }
    std::error_code error;
    std::filesystem::rename(temporary, path, error);
    if (error) {
        log_warning("The pulse file {} could not be replaced: {}", path, error.message());
    }
}

Pulse &pulse() {
    static Pulse beating;
    return beating;
}

}
