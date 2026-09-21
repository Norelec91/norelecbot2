#include "config.hpp"

#include "logging.hpp"
#include "text.hpp"

#include <algorithm>
#include <array>
#include <cstdlib>
#include <fstream>
#include <limits>

namespace norelecbot {
namespace {

template <typename Number>
bool set_number(
    Number &target,
    std::string_view value,
    Number fallback,
    Number minimum = std::numeric_limits<Number>::min(),
    Number maximum = std::numeric_limits<Number>::max()
) {
    const std::optional<std::int64_t> parsed =
        value.empty() ? std::optional<std::int64_t>{fallback} : text::parse_int64(value);
    if (!parsed || *parsed < minimum || *parsed > maximum) {
        return false;
    }
    target = static_cast<Number>(*parsed);
    return true;
}

/* A list of names, separated by commas or spaces. */
std::vector<std::string> split_names(std::string_view value) {
    std::vector<std::string> names;
    while (!value.empty()) {
        const std::size_t end = std::min(value.find_first_of(", \t"), value.size());
        if (const std::string_view name = text::trim(value.substr(0, end)); !name.empty()) {
            names.emplace_back(name);
        }
        value.remove_prefix(std::min(end + 1, value.size()));
    }
    return names;
}

bool set_text(std::string &target, std::string_view value, std::string_view fallback) {
    target = value.empty() ? fallback : value;
    return true;
}

struct Setting {
    const char *name;
    bool (*apply)(AppConfig &config, std::string_view value);
};

// Environment variables are applied in this order.
constexpr std::array settings{
    Setting{"NORELECBOT_TELEGRAM_TOKEN", [](AppConfig &config, std::string_view value) {
        return set_text(config.bot_token, value, {});
    }},
    Setting{"NORELECBOT_CONQUISTER_ENABLED", [](AppConfig &config, std::string_view value) {
        config.conquister_enabled =
            !value.empty() && value != "0" && !text::equals_ignore_case(value, "false");
        return true;
    }},
    Setting{"NORELECBOT_OWNER_ID", [](AppConfig &config, std::string_view value) {
        std::vector<std::int64_t> owners;
        for (const std::string &name : split_names(value)) {
            const std::optional<std::int64_t> id = text::parse_int64(name);
            if (!id) {
                return false;
            }
            owners.push_back(*id);
        }
        config.owner_ids = std::move(owners);
        return true;
    }},
    Setting{"NORELECBOT_QUOTE_COST", [](AppConfig &config, std::string_view value) {
        return set_number(config.quote_cost, value, AppConfig::default_quote_cost, 0);
    }},
    Setting{"NORELECBOT_QUOTE_BANNED", [](AppConfig &config, std::string_view value) {
        config.quote_banned = split_names(value);
        return true;
    }},
    Setting{"NORELECBOT_BALLOON_COST", [](AppConfig &config, std::string_view value) {
        return set_number(config.balloon_cost, value, AppConfig::default_balloon_cost, 0);
    }},
    Setting{"NORELECBOT_FURNITURE_COST", [](AppConfig &config, std::string_view value) {
        return set_number(config.furniture_cost, value, AppConfig::default_furniture_cost, 0);
    }},
    Setting{"NORELECBOT_FURNITURE_LIMIT", [](AppConfig &config, std::string_view value) {
        return set_number(config.furniture_limit, value, AppConfig::default_furniture_limit, 1);
    }},
    Setting{"NORELECBOT_PRICE_PERCENT", [](AppConfig &config, std::string_view value) {
        return set_number(config.price_percent, value, AppConfig::default_price_percent, 0);
    }},
    Setting{"NORELECBOT_PRICE_CEILING", [](AppConfig &config, std::string_view value) {
        return set_number(config.price_ceiling, value, AppConfig::default_price_ceiling, 1);
    }},
    Setting{"NORELECBOT_COOLDOWN_SECONDS", [](AppConfig &config, std::string_view value) {
        return set_number(config.cooldown_seconds, value, AppConfig::default_cooldown_seconds, 0);
    }},
    Setting{"NORELECBOT_ATTACK_COST", [](AppConfig &config, std::string_view value) {
        return set_number(config.attack_cost, value, AppConfig::default_attack_cost, 0);
    }},
    Setting{"NORELECBOT_TRAVEL_DIVISOR", [](AppConfig &config, std::string_view value) {
        return set_number(config.travel_divisor, value, AppConfig::default_travel_divisor, 1);
    }},
    Setting{"NORELECBOT_ZODIAC_SIGNS", [](AppConfig &config, std::string_view value) {
        std::vector<zodiac::Override> chosen;
        for (const std::string &pair : split_names(value)) {
            const std::size_t equals = pair.find('=');
            if (equals == std::string::npos) {
                return false;
            }
            zodiac::Override entry{pair.substr(0, equals), pair.substr(equals + 1)};
            if (entry.username.empty() || !zodiac::sign_named(entry.sign)) {
                return false;
            }
            chosen.push_back(std::move(entry));
        }
        config.zodiac_signs = std::move(chosen);
        return true;
    }},
    Setting{"NORELECBOT_BOOST_COST", [](AppConfig &config, std::string_view value) {
        return set_number(config.boost_cost, value, AppConfig::default_boost_cost, 0);
    }},
    Setting{"NORELECBOT_BOOST_MULTIPLIER", [](AppConfig &config, std::string_view value) {
        return set_number(config.boost_multiplier, value, AppConfig::default_boost_multiplier, 1, 100);
    }},
    Setting{"NORELECBOT_SHIELD_USERS", [](AppConfig &config, std::string_view value) {
        config.shield_users = split_names(value);
        return true;
    }},
    Setting{"NORELECBOT_SHIELD_SECONDS", [](AppConfig &config, std::string_view value) {
        return set_number(config.shield_seconds, value, AppConfig::default_shield_seconds, 0);
    }},
    Setting{"NORELECBOT_CONQUISTER_CHAT_ID", [](AppConfig &config, std::string_view value) {
        return set_number(config.conquister_chat_id, value, std::int64_t{0});
    }},
    Setting{"NORELECBOT_API_HOST", [](AppConfig &config, std::string_view value) {
        return set_text(config.api_host, value, AppConfig::default_api_host);
    }},
    Setting{"NORELECBOT_API_PORT", [](AppConfig &config, std::string_view value) {
        return set_number(config.api_port, value, AppConfig::default_api_port, 1, 65535);
    }},
    Setting{"NORELECBOT_CONQUISTER_FILE", [](AppConfig &config, std::string_view value) {
        return set_text(config.conquister_path, value, AppConfig::default_conquister_path);
    }},
    Setting{"NORELECBOT_IRC_ENABLED", [](AppConfig &config, std::string_view value) {
        config.irc_enabled = !value.empty() && value != "0" && !text::equals_ignore_case(value, "false");
        return true;
    }},
    Setting{"NORELECBOT_IRC_SERVER", [](AppConfig &config, std::string_view value) {
        return set_text(config.irc_server, value, {});
    }},
    Setting{"NORELECBOT_IRC_PORT", [](AppConfig &config, std::string_view value) {
        return set_number(config.irc_port, value, AppConfig::default_irc_port, 1, 65535);
    }},
    Setting{"NORELECBOT_IRC_NICK", [](AppConfig &config, std::string_view value) {
        return set_text(config.irc_nick, value, AppConfig::default_irc_nick);
    }},
    Setting{"NORELECBOT_IRC_NICKSERV_PASSWORD", [](AppConfig &config, std::string_view value) {
        return set_text(config.irc_nickserv_password, value, {});
    }},
    Setting{"NORELECBOT_IRC_CHANNEL", [](AppConfig &config, std::string_view value) {
        return set_text(config.irc_channel, value, {});
    }},
    Setting{"NORELECBOT_IRC_NO_FORWARD_PREFIX", [](AppConfig &config, std::string_view value) {
        return set_text(config.irc_no_forward_prefix, value, {});
    }},
    Setting{"NORELECBOT_IRC_OWNER", [](AppConfig &config, std::string_view value) {
        return set_text(config.irc_owner_nick, value, {});
    }},
    Setting{"NORELECBOT_QUOTES_FILE", [](AppConfig &config, std::string_view value) {
        return set_text(config.quotes_path, value, AppConfig::default_quotes_path);
    }},
};

bool apply_setting(AppConfig &config, std::string_view name, std::string_view value) {
    const auto setting = std::ranges::find_if(settings, [name](const Setting &candidate) {
        return name == candidate.name;
    });
    return setting == settings.end() || setting->apply(config, value);
}

bool apply_dotenv(AppConfig &config, const std::filesystem::path &path) {
    std::ifstream file{path};
    if (!file.is_open()) {
        return false;
    }
    std::string line;
    while (std::getline(file, line)) {
        const std::string_view entry = text::trim(line);
        const std::size_t equals = entry.find('=');
        if (entry.empty() || entry.front() == '#' || equals == std::string_view::npos) {
            continue;
        }
        const std::string_view name = text::trim(entry.substr(0, equals));
        std::string_view value = text::trim(entry.substr(equals + 1));
        if (value.size() >= 2 && (value.front() == '"' || value.front() == '\'') &&
            value.back() == value.front()) {
            value = value.substr(1, value.size() - 2);
        }
        if (!name.empty() && !apply_setting(config, name, value)) {
            return false;
        }
    }
    return !file.bad();
}

bool apply_environment(AppConfig &config) {
    return std::ranges::all_of(settings, [&config](const Setting &setting) {
        const char *value = std::getenv(setting.name);
        return value == nullptr || setting.apply(config, value);
    });
}

}

std::optional<AppConfig> load_config(const std::filesystem::path &dotenv_path) {
    AppConfig config;
    if (!apply_dotenv(config, dotenv_path)) {
        log_error(
            "Required configuration file '{}' is missing, unreadable, or invalid",
            dotenv_path.string()
        );
        return std::nullopt;
    }
    if (!apply_environment(config)) {
        log_error("Invalid environment configuration");
        return std::nullopt;
    }
    return config;
}

}
