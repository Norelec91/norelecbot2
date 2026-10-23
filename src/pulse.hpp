#ifndef NORELECBOT_PULSE_HPP
#define NORELECBOT_PULSE_HPP

#include <cstdint>
#include <map>
#include <mutex>
#include <string>
#include <string_view>
#include <vector>

namespace norelecbot {

/* The pulse of the group: how many messages, how much laughter and which parts of the game come up in the
   chats, beside what the game does, five minutes at a time. Never a word or a name: what is said is only
   counted, and forgotten at once. It remembers ninety days. */
class Pulse {
public:
    /* Somebody wrote in the game's chat on a platform, "tg" or "irc". */
    void message(std::string_view platform, std::string_view text, std::int64_t now);
    /* Something happened in the game. */
    void event(std::string_view kind, std::int64_t now);

    /* The counts of a five-minute stretch, by what was counted. */
    [[nodiscard]] std::map<std::string, std::int64_t> at(std::int64_t now) const;

    void load(const std::string &path);
    void save(const std::string &path) const;

private:
    void count(const std::string &what, std::int64_t now);

    mutable std::mutex mutex_;
    std::map<std::int64_t, std::map<std::string, std::int64_t>> buckets_;
};

/* The one the bot keeps. */
[[nodiscard]] Pulse &pulse();

/* What a message brings up, by the names the counts keep: laughter, and the parts of the game named. */
[[nodiscard]] bool laughs(std::string_view text);
[[nodiscard]] std::vector<std::string> topics(std::string_view text);

}

#endif
