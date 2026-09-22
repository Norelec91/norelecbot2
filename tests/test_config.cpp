#include "config.hpp"

#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest/doctest.h>

#include <filesystem>
#include <fstream>
#include <string>
#include <string_view>
#include <system_error>

namespace {

/* Writes the .env under test and removes it afterwards. */
class ConfigFile {
public:
    ConfigFile() = default;

    ~ConfigFile() {
        std::error_code ignored;
        std::filesystem::remove(path_, ignored);
    }

    ConfigFile(const ConfigFile &) = delete;
    ConfigFile &operator=(const ConfigFile &) = delete;
    ConfigFile(ConfigFile &&) = delete;
    ConfigFile &operator=(ConfigFile &&) = delete;

    [[nodiscard]] std::optional<norelecbot::AppConfig> load(std::string_view content) const {
        std::ofstream file{path_, std::ios::binary};
        file << content;
        file.close();
        REQUIRE(file.good());
        return norelecbot::load_config(path_);
    }

private:
    std::string path_{"config-test.env"};
};

}

TEST_CASE("a missing .env file is an error") {
    CHECK_FALSE(norelecbot::load_config("missing-required-config-test.env"));
}

TEST_CASE("settings are read from the .env file") {
    const ConfigFile file;

    const auto cost = file.load("NORELECBOT_QUOTE_COST=250\n");
    REQUIRE(cost);
    CHECK(cost->quote_cost == 250);

    const auto free_quotes = file.load("NORELECBOT_QUOTE_COST=0\n");
    REQUIRE(free_quotes);
    CHECK(free_quotes->quote_cost == 0);

    const auto shield = file.load("NORELECBOT_RAID_SHIELD_COST=1250\n");
    REQUIRE(shield);
    CHECK(shield->raid_shield_cost == 1250);

    const auto chat = file.load("NORELECBOT_CONQUISTER_CHAT_ID=-1001234567890\n");
    REQUIRE(chat);
    CHECK(chat->conquister_chat_id == -1001234567890);
}

TEST_CASE("an empty value falls back to the default") {
    const ConfigFile file;

    const auto config = file.load("NORELECBOT_OWNER_ID=\n");
    REQUIRE(config);
    CHECK(config->quote_cost == 1000);
    CHECK(config->raid_shield_cost == 1000);
    CHECK(config->conquister_chat_id == 0);
    CHECK(config->api_port == 8000);
}

TEST_CASE("an invalid value is refused") {
    const ConfigFile file;

    CHECK_FALSE(file.load("NORELECBOT_CONQUISTER_CHAT_ID=gruppo\n"));
    CHECK_FALSE(file.load("NORELECBOT_QUOTE_COST=-1\n"));
    CHECK_FALSE(file.load("NORELECBOT_RAID_SHIELD_COST=-1\n"));
    CHECK_FALSE(file.load("NORELECBOT_QUOTE_COST=tante\n"));
    CHECK_FALSE(file.load("NORELECBOT_QUOTE_COST=2147483648\n"));
}

TEST_CASE("carriage returns, comments and quotes are handled") {
    const ConfigFile file;

    const auto config = file.load("# commento\r\n\r\nNORELECBOT_API_PORT = \"9000\"\r\nNORELECBOT_QUOTE_COST=5");
    REQUIRE(config);
    CHECK(config->api_port == 9000);
    CHECK(config->quote_cost == 5);
}

TEST_CASE("the signs set by hand are read, and a wrong one is refused") {
    const ConfigFile file;

    const auto config = file.load("NORELECBOT_ZODIAC_SIGNS=Giangiui=vergine, mifaisonno=leone\n");
    REQUIRE(config);
    REQUIRE(config->zodiac_signs.size() == 2);
    CHECK(config->zodiac_signs[0].username == "Giangiui");
    CHECK(config->zodiac_signs[0].sign == "vergine");
    CHECK(config->zodiac_signs[1].username == "mifaisonno");

    CHECK(file.load("NORELECBOT_ZODIAC_SIGNS=\n"));
    CHECK_FALSE(file.load("NORELECBOT_ZODIAC_SIGNS=Giangiui\n"));
    CHECK_FALSE(file.load("NORELECBOT_ZODIAC_SIGNS=Giangiui=ofiuco\n"));
    CHECK_FALSE(file.load("NORELECBOT_ZODIAC_SIGNS==vergine\n"));
}

TEST_CASE("the bot can have more than one owner") {
    const ConfigFile file;

    const auto config = file.load("NORELECBOT_OWNER_ID=12345, 998877\n");
    REQUIRE(config);
    REQUIRE(config->owner_ids.size() == 2);
    CHECK(config->owner_ids[0] == 12345);
    CHECK(config->owner_ids[1] == 998877);

    const auto alone = file.load("NORELECBOT_OWNER_ID=12345\n");
    REQUIRE(alone);
    CHECK(alone->owner_ids.size() == 1);

    const auto nobody = file.load("NORELECBOT_OWNER_ID=\n");
    REQUIRE(nobody);
    CHECK(nobody->owner_ids.empty());

    CHECK_FALSE(file.load("NORELECBOT_OWNER_ID=12345, tizio\n"));
}
