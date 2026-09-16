#ifndef NORELECBOT_CONFIG_HPP
#define NORELECBOT_CONFIG_HPP

#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace norelecbot {

struct AppConfig {
    static constexpr int default_quote_cost = 1000;
    static constexpr int default_balloon_cost = 1000;
    static constexpr int default_cooldown_seconds = 300;
    static constexpr int default_shield_seconds = 3600;
    static constexpr int default_api_port = 8000;
    static constexpr int default_irc_port = 6697;
    static constexpr std::string_view default_irc_nick = "NorelecBot";
    static constexpr std::string_view default_api_host = "0.0.0.0";
    static constexpr std::string_view default_conquister_path = "conquister.json";
    static constexpr std::string_view default_quotes_path = "quotes.json";

    std::string bot_token;
    bool conquister_enabled = false;
    std::int64_t owner_id = 0;
    int quote_cost = default_quote_cost;
    int balloon_cost = default_balloon_cost;
    int cooldown_seconds = default_cooldown_seconds;
    /* The players whose balloon cannot be popped, except by each other, and how long it holds. */
    std::vector<std::string> shield_users;
    int shield_seconds = default_shield_seconds;
    std::int64_t conquister_chat_id = 0;
    std::string api_host{default_api_host};
    int api_port = default_api_port;
    std::string conquister_path{default_conquister_path};
    std::string quotes_path{default_quotes_path};
    bool irc_enabled = false;
    std::string irc_server;
    int irc_port = default_irc_port;
    std::string irc_nick{default_irc_nick};
    std::string irc_nickserv_password;
    std::string irc_channel;
    std::string irc_no_forward_prefix;
    std::string irc_owner_nick;
};

/* Reads the required .env file, then applies the environment variables on top of it. */
[[nodiscard]] std::optional<AppConfig> load_config(const std::filesystem::path &dotenv_path);

}

#endif
