#include "position.hpp"

#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest/doctest.h>

using namespace norelecbot;

TEST_CASE("an id spells out a point on the square") {
    CHECK(position::coordinates_of(0) == position::Point{0, 0});
    CHECK(position::coordinates_of(1) == position::Point{1, 0});
    CHECK(position::coordinates_of(99999) == position::Point{99999, 0});
    CHECK(position::coordinates_of(100000) == position::Point{0, 1});
    CHECK(position::coordinates_of(4200099999) == position::Point{99999, 42000});
    /* The last point of the square, and then round we go. */
    CHECK(position::coordinates_of(position::ids - 1) == position::Point{99999, 99999});
    CHECK(position::coordinates_of(position::ids) == position::Point{0, 0});
}

TEST_CASE("the distance is the one on the map") {
    const position::Point home{0, 0};
    CHECK(position::distance(home, home) == 0);
    CHECK(position::distance(home, {3, 4}) == 5);
    CHECK(position::distance({3, 4}, home) == 5);
    CHECK(position::distance({10, 20}, {13, 24}) == 5);
    /* Corner to corner. */
    CHECK(position::distance(home, {99999, 99999}) == 141420);
}

TEST_CASE("the ride takes as long as the distance says") {
    CHECK(position::travel_seconds(52000, 1000) == 52);
    CHECK(position::travel_seconds(141420, 1000) == 141);
    /* Neighbours still take the shortest ride. */
    CHECK(position::travel_seconds(0, 1000) == position::shortest_travel);
    CHECK(position::travel_seconds(4999, 1000) == position::shortest_travel);
    CHECK(position::travel_seconds(5000, 1000) == 5);
    CHECK(position::travel_seconds(6000, 1000) == 6);
    /* A divisor of nothing must not divide by zero. */
    CHECK(position::travel_seconds(52000, 0) == position::shortest_travel);
}
