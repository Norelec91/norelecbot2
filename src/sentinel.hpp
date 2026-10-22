#ifndef NORELECBOT_SENTINEL_HPP
#define NORELECBOT_SENTINEL_HPP

#include <cstdint>
#include <functional>
#include <map>
#include <mutex>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace norelecbot {

/* Watches how every account plays, without ever touching the game, and tells the owner which accounts
   look like scripts and which look like one person behind several names. It remembers a week. */
class Sentinel {
public:
    /* What the game tells it: a ride set off (to give, or to rob), a try at @TheConquister37, a rider
       home again, and a player robbed by another. Players are the keys the game files them under. */
    void ride(const std::string &player, const std::string &target, bool gift, std::int64_t now);
    void claim(const std::string &player, std::int64_t now);
    void back(const std::string &player, std::int64_t now);
    void robbed(const std::string &victim, const std::string &robber, std::int64_t now);
    /* What the chats tell it: somebody wrote, a command or not, from where on IRC. An identity is "tg:<id>"
       or "irc:<nick>"; link says which player it plays. */
    void heard(const std::string &identity, std::string_view host, bool command, std::int64_t now);
    void link(const std::string &identity, const std::string &player);

    /* The players' names as the chats know them, by key. */
    using Names = std::function<std::string(const std::string &player)>;
    /* Who looks like a script and who looks like the same person, and why. */
    [[nodiscard]] std::string report(std::int64_t now, const Names &name) const;

    /* The day the daily report last went out, so that a restart does not send it twice. */
    [[nodiscard]] std::int64_t reported_day() const;
    void set_reported_day(std::int64_t day);

    void load(const std::string &path);
    void save(const std::string &path) const;

    /* What it saw in what it remembers. */
    struct Act {
        std::int64_t at = 0;
        /* 0 a ride to rob, 1 a ride to give, 2 a try at @TheConquister37. */
        int kind = 0;
        std::string target;
    };
    struct Player {
        std::int64_t first_seen = 0;
        std::vector<Act> acts;
        std::vector<std::int64_t> backs;
        std::vector<std::pair<std::int64_t, std::string>> robbed_by;
    };
    struct Identity {
        std::string player;
        std::string host;
        std::vector<std::int64_t> commands;
        std::vector<std::int64_t> chats;
    };

private:
    Player &player(const std::string &key, std::int64_t now);
    void forget(std::int64_t now);

    mutable std::mutex mutex_;
    std::int64_t started_ = 0;
    std::int64_t reported_day_ = 0;
    std::int64_t forgotten_ = 0;
    std::map<std::string, Player> players_;
    std::map<std::string, Identity> identities_;
};

/* The one the bot keeps. */
[[nodiscard]] Sentinel &sentinel();

/* The hour of the day and the day, in local time. */
[[nodiscard]] int local_hour(std::int64_t at);
[[nodiscard]] std::int64_t local_day(std::int64_t at);

}

#endif
