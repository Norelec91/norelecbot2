#ifndef NORELECBOT_CONFIG_HPP
#define NORELECBOT_CONFIG_HPP

#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>

namespace norelecbot {

struct AppConfig {
    static constexpr int default_quote_cost = 1000;
    static constexpr int default_api_port = 8000;
    static constexpr std::string_view default_api_host = "0.0.0.0";
    static constexpr std::string_view default_conquister_path = "conquister.json";
    static constexpr std::string_view default_quotes_path = "quotes.json";

    std::string bot_token;
    /* 0 leaves the choice to the system, 1 forces IPv4, 2 forces IPv6. */
    int telegram_ip_version = 0;
    bool conquister_enabled = false;
    std::int64_t owner_id = 0;
    int quote_cost = default_quote_cost;
    std::int64_t conquister_chat_id = 0;
    std::string api_host{default_api_host};
    int api_port = default_api_port;
    std::string conquister_path{default_conquister_path};
    std::string quotes_path{default_quotes_path};
};

/* Reads the required .env file, then applies the environment variables on top of it. */
[[nodiscard]] std::optional<AppConfig> load_config(const std::filesystem::path &dotenv_path);

}

#endif
