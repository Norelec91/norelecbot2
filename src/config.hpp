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
    static constexpr int default_balloon_cost = 1000;
    static constexpr int default_cooldown_seconds = 300;
    static constexpr int default_attack_cost = 100;
    static constexpr int default_travel_divisor = 350;
    static constexpr int default_raid_share = 4;
    static constexpr int default_ghost_min_seconds = 60;
    static constexpr int default_ghost_max_seconds = 600;
    static constexpr int default_mishap_min_seconds = 0;
    static constexpr int default_mishap_max_seconds = 1800;
    static constexpr int default_virus_cooldown_seconds = 600;
    static constexpr int default_magic_most = 5;
    static constexpr int default_flipper_odds = 0;
    static constexpr int default_lucky_swing = 500;
    static constexpr int default_cascade_most = 5;
    static constexpr int default_tax_percent = 0;
    static constexpr int default_reprogram_min_seconds = 1800;
    static constexpr int default_reprogram_max_seconds = 7200;
    static constexpr int default_flegyas_share = 3;
    static constexpr int default_flegyas_min_seconds = 0;
    static constexpr int default_flegyas_max_seconds = 3600;
    static constexpr int default_chaos_min_seconds = 0;
    static constexpr int default_chaos_max_seconds = 7200;
    static constexpr int default_happening_min_seconds = 0;
    static constexpr int default_happening_max_seconds = 5400;
    static constexpr int default_shield_seconds = 3600;
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
    /* Whoever may see and delete the quotes, on Telegram. */
    std::vector<std::int64_t> owner_ids;
    int quote_cost = default_quote_cost;
    /* Pieces of word a quote may not contain, whatever the spelling. */
    std::vector<std::string> quote_banned;
    /* Players who are answered for and get nowhere: their quotes are dropped and their raids fail. */
    std::vector<std::string> shadowed;
    /* A player of the bot's own, who goes for @TheConquister37 by himself; empty means nobody does. */
    std::string ghost_raider;
    int ghost_min_seconds = default_ghost_min_seconds;
    int ghost_max_seconds = default_ghost_max_seconds;
    /* How often something small happens to somebody, for no reason; zero and nothing ever does. */
    int mishap_min_seconds = default_mishap_min_seconds;
    int mishap_max_seconds = default_mishap_max_seconds;
    /* How long a player waits between one move of the virus game and the next. */
    int virus_cooldown_seconds = default_virus_cooldown_seconds;
    /* Words that multiply the palle of whoever says them; empty and nothing happens. */
    std::vector<std::string> magic_words;
    int magic_most = default_magic_most;
    /* The five small spells: double or halve, give to the poorest, earn or lose a point of simpatia. */
    std::vector<std::string> bet_words;
    std::vector<std::string> alms_words;
    std::vector<std::string> charisma_words;
    std::vector<std::string> taunt_words;
    std::vector<std::string> sixseven_words;
    std::vector<std::string> blessing_words;
    /* How often something happens to the world at large; zero and nothing does. */
    int happening_min_seconds = default_happening_min_seconds;
    int happening_max_seconds = default_happening_max_seconds;
    /* One message in this many hits the pinball table; zero and there is no table. */
    int flipper_odds = default_flipper_odds;
    /* Words that hit the table for certain, whatever the odds say. */
    std::vector<std::string> flipper_words;
    /* Words that win or lose palle for whoever says them, and how much is at stake. */
    std::vector<std::string> lucky_words;
    int lucky_swing = default_lucky_swing;
    /* Words that set off a run of things at once, and how many at most. */
    std::vector<std::string> cascade_words;
    int cascade_most = default_cascade_most;
    /* What the taxman takes from whoever tops the leaderboard, once a day. */
    int tax_percent = default_tax_percent;
    /* How often Kio takes over somebody. */
    int reprogram_min_seconds = default_reprogram_min_seconds;
    int reprogram_max_seconds = default_reprogram_max_seconds;
    /* How often something comes down on a player and takes its share; zero and nothing does. */
    int flegyas_min_seconds = default_flegyas_min_seconds;
    int flegyas_max_seconds = default_flegyas_max_seconds;
    int flegyas_share = default_flegyas_share;
    /* How often the rules themselves are drawn again; zero and they stay as they are. */
    int chaos_min_seconds = default_chaos_min_seconds;
    int chaos_max_seconds = default_chaos_max_seconds;
    int balloon_cost = default_balloon_cost;
    int cooldown_seconds = default_cooldown_seconds;
    int attack_cost = default_attack_cost;
    /* Seconds of travel per unit of distance, and the share of a raid's loot. */
    int travel_divisor = default_travel_divisor;
    int raid_share = default_raid_share;
    int boost_cost = default_boost_cost;
    int boost_multiplier = default_boost_multiplier;
    /* Players whose real sign the owner knows, instead of the one their name gives. */
    std::vector<zodiac::Override> zodiac_signs;
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
