#include "zodiac.hpp"

#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest/doctest.h>

#include <set>
#include <string>

using namespace norelecbot;

TEST_CASE("a name always falls on the same sign") {
    const zodiac::Sign sign = zodiac::sign_of("Norelec");
    CHECK(zodiac::sign_of("Norelec").name == sign.name);
    CHECK_FALSE(sign.name.empty());
    CHECK_FALSE(sign.symbol.empty());
    /* The spelling does not matter: one player, one sign, wherever they play from. */
    CHECK(zodiac::sign_of("norelec").name == sign.name);
    CHECK(zodiac::sign_of("NORELEC").name == sign.name);

    SUBCASE("the twelve signs are all reachable") {
        std::set<std::string> found;
        for (int index = 0; index < 400; ++index) {
            found.insert(std::string{zodiac::sign_of("giocatore" + std::to_string(index)).name});
        }
        CHECK(found.size() == 12);
    }
}

TEST_CASE("the house turns every day") {
    constexpr std::int64_t day = 86400;
    constexpr std::int64_t noon = 1789560000;
    std::set<std::string> houses;
    for (int index = 0; index < 4; ++index) {
        houses.insert(std::string{zodiac::element_name(zodiac::element_of_day(noon + (index * day)))});
    }
    CHECK(houses.size() == 4);
    CHECK(houses.contains("acqua"));
    CHECK(houses.contains("fuoco"));
    CHECK(houses.contains("vento"));
    CHECK(houses.contains("terra"));

    /* The same day, morning and evening, is the same house. */
    CHECK(zodiac::element_of_day(noon) == zodiac::element_of_day(noon + 3600));
    /* And it comes back after four days. */
    CHECK(zodiac::element_of_day(noon) == zodiac::element_of_day(noon + (4 * day)));
}

TEST_CASE("fire and water are opposed, air and earth are opposed") {
    CHECK(zodiac::opposed(zodiac::Element::fire, zodiac::Element::water));
    CHECK(zodiac::opposed(zodiac::Element::water, zodiac::Element::fire));
    CHECK(zodiac::opposed(zodiac::Element::air, zodiac::Element::earth));
    CHECK(zodiac::opposed(zodiac::Element::earth, zodiac::Element::air));
    CHECK_FALSE(zodiac::opposed(zodiac::Element::fire, zodiac::Element::air));
    CHECK_FALSE(zodiac::opposed(zodiac::Element::fire, zodiac::Element::fire));
}

TEST_CASE("the day is worth more, less or the same") {
    constexpr std::int64_t day = 86400;
    constexpr std::int64_t noon = 1789560000;
    CHECK(zodiac::percent_for("", noon) == 100);

    /* Over four days every player meets their house once and their opposite once. */
    std::multiset<int> percents;
    for (int index = 0; index < 4; ++index) {
        percents.insert(zodiac::percent_for("Norelec", noon + (index * day)));
    }
    CHECK(percents.count(125) == 1);
    CHECK(percents.count(75) == 1);
    CHECK(percents.count(100) == 2);
}
