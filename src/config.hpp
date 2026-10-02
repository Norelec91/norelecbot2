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
    /* How much dearer an emoji gets, in percent, for every copy of it already in the game. */
    static constexpr int default_furniture_inflation = 50;
    /* How much road buys a palla: the distance between the two is divided by this before it
       becomes loot, so a journey is worth a few minutes of holding the place, not a day of it. */
    static constexpr int default_loot_divisor = 50;
    static constexpr int default_furniture_limit = 10;
    static constexpr int default_cooldown_seconds = 300;
    static constexpr int default_travel_divisor = 350;
    static constexpr int default_lightning_percent = 25;
    /* How long a player hit by a 💩 stays "lo smerdato": a day. */
    static constexpr int default_smeared_seconds = 86400;
    /* How much faster every 🚀 on the name makes a ride, in percent. */
    static constexpr int default_rocket_percent = 25;
    /* How much less a raider takes for every 🥺 on the target's name, in percent. */
    static constexpr int default_pleading_percent = 5;
    /* How often, in percent, a 💣 is a dud that goes off in the thrower's hand. */
    static constexpr int default_bomb_dud_percent = 10;
    /* What the ☢️ that starts the game over costs. */
    static constexpr int default_nuke_cost = 1'000'000;
    /* What the 🌀 that flings a player far away costs: nothing, since only the admins can have one. */
    static constexpr int default_vortex_cost = 0;
    /* The chance, in percent, that each 🐶 on a name gives of stopping a raid on the house. */
    static constexpr int default_dog_percent = 10;
    /* What a new 🎈 costs once the one everybody starts with has popped. */
    static constexpr int default_balloon_cost = 1000;
    /* The chance, in percent, that each 📮 on a name gives of sending back what is thrown at the house. */
    static constexpr int default_mailbox_percent = 10;
    /* The chance, in percent, that each 🥷 a raider has with him gives of slipping past a house's guards. */
    static constexpr int default_ninja_percent = 10;
    /* What each 🔊 at a house takes off a raider's chance of slipping past, in percent points. */
    static constexpr int default_alarm_percent = 10;
    /* The chance, in percent, that each 🏴‍☠️ a raider has with him gives of carrying off an emoji. */
    static constexpr int default_pirate_percent = 10;
    /* How long after a 💦 lands the child is born: nine hours. */
    static constexpr int default_pregnancy_seconds = 9 * 60 * 60;
    /* How long a child stays in each of its four ages: a day. */
    static constexpr int default_child_stage_seconds = 24 * 60 * 60;
    /* The palle each 🐔 on a name lays for its owner every minute. */
    static constexpr int default_hen_per_minute = 1;
    /* The palle a child makes every second of its grown-up age. */
    static constexpr int default_adult_per_second = 1;
    /* The chance, in percent, that a grown-up girl and boy of the same house have a child. */
    static constexpr int default_mating_percent = 50;
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
    int furniture_inflation = default_furniture_inflation;
    int loot_divisor = default_loot_divisor;
    int cooldown_seconds = default_cooldown_seconds;
    /* Seconds of travel per unit of distance, and the share of a raid's loot. */
    int travel_divisor = default_travel_divisor;
    int lightning_percent = default_lightning_percent;
    int smeared_seconds = default_smeared_seconds;
    int rocket_percent = default_rocket_percent;
    int pleading_percent = default_pleading_percent;
    int bomb_dud_percent = default_bomb_dud_percent;
    int nuke_cost = default_nuke_cost;
    int vortex_cost = default_vortex_cost;
    int dog_percent = default_dog_percent;
    int balloon_cost = default_balloon_cost;
    int mailbox_percent = default_mailbox_percent;
    int ninja_percent = default_ninja_percent;
    int alarm_percent = default_alarm_percent;
    int pirate_percent = default_pirate_percent;
    int pregnancy_seconds = default_pregnancy_seconds;
    int child_stage_seconds = default_child_stage_seconds;
    int hen_per_minute = default_hen_per_minute;
    int adult_per_second = default_adult_per_second;
    int mating_percent = default_mating_percent;
    /* Whether a player seen for the first time is handed a 🎈. */
    bool starter_balloon = true;
    /* Players whose real sign the owner knows, instead of the one their name gives. */
    std::vector<zodiac::Override> zodiac_signs;
    std::int64_t conquister_chat_id = 0;
    std::string api_host{default_api_host};
    int api_port = default_api_port;
    std::string conquister_path{default_conquister_path};
    std::string quotes_path{default_quotes_path};
    /* What changed in the game, told once to both chats when the bot starts, then deleted; empty = never. */
    std::string news_path;
    bool irc_enabled = false;
    std::string irc_server;
    int irc_port = default_irc_port;
    std::string irc_nick{default_irc_nick};
    std::string irc_nickserv_password;
    std::string irc_channel;
    std::string irc_no_forward_prefix;
    std::string irc_owner_nick;
    /* The nicks that are admins on IRC, as the Telegram ids in admin_ids are there. */
    std::vector<std::string> irc_admin_nicks;
};

/* Reads the required .env file, then applies the environment variables on top of it. */
[[nodiscard]] std::optional<AppConfig> load_config(const std::filesystem::path &dotenv_path);

}

#endif
