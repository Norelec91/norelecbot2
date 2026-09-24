#ifndef NORELECBOT_CONFIG_HPP
#define NORELECBOT_CONFIG_HPP

#include "zodiac.hpp"

#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace norelecbot {

struct AppConfig {
    static constexpr int default_quote_cost = 1000;
    static constexpr int default_furniture_cost = 10000;
    /* What share of the middle player's wealth a thing costs, and how many times the list price
       it may never pass. Zero per cent leaves the prices fixed. */
    /* How much road buys a palla: the distance between the two is divided by this before it
       becomes loot, so a journey is worth a few minutes of holding the place, not a day of it. */
    static constexpr int default_loot_divisor = 50;
    /* A raid takes at most this fraction of what the target owns: three means a third. Zero
       lifts the ceiling and lets a raid clean him out. */
    static constexpr int default_raid_share = 3;
    static constexpr int default_price_percent = 20;
    static constexpr int default_price_ceiling = 50;
    static constexpr int default_furniture_limit = 10;
    static constexpr int default_cooldown_seconds = 300;
    static constexpr int default_attack_cost = 100;
    static constexpr int default_travel_divisor = 350;
    static constexpr int default_boost_cost = 1500;
    static constexpr int default_boost_multiplier = 3;
    static constexpr int default_api_port = 8000;
    static constexpr int default_irc_port = 6697;
    static constexpr std::string_view default_irc_nick = "NorelecBot";
    static constexpr std::string_view default_api_host = "0.0.0.0";
    static constexpr std::string_view default_conquister_path = "conquister.json";
    static constexpr std::string_view default_quotes_path = "quotes.json";

    std::string bot_token;
    bool conquister_enabled = false;
    /* Whoever owns the bot, on Telegram: an admin who may also switch the debug on. */
    std::vector<std::int64_t> owner_ids;
    /* Whoever may see and delete the quotes, on Telegram; the owners can do it too. */
    std::vector<std::int64_t> admin_ids;
    int quote_cost = default_quote_cost;
    /* Pieces of word a quote may not contain, whatever the spelling. */
    std::vector<std::string> quote_banned;
    /* What a shelf of emoji costs, and how many of them a name can carry. */
    int furniture_cost = default_furniture_cost;
    int furniture_limit = default_furniture_limit;
    int loot_divisor = default_loot_divisor;
    int raid_share = default_raid_share;
    int price_percent = default_price_percent;
    int price_ceiling = default_price_ceiling;
    int cooldown_seconds = default_cooldown_seconds;
    int attack_cost = default_attack_cost;
    /* Seconds of travel per unit of distance, and the share of a raid's loot. */
    int travel_divisor = default_travel_divisor;
    int boost_cost = default_boost_cost;
    int boost_multiplier = default_boost_multiplier;
    /* Players whose real sign the owner knows, instead of the one their name gives. */
    std::vector<zodiac::Override> zodiac_signs;
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
