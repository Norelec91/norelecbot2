#ifndef NORELECBOT_POSITION_HPP
#define NORELECBOT_POSITION_HPP

#include <cstdint>

namespace norelecbot::position {

/* Every player lives on a square of this side, at the point their own id spells out. */
inline constexpr std::int64_t plane_side = 100000;
inline constexpr std::int64_t ids = plane_side * plane_side;
/* Even the closest neighbours are a short ride away. */
inline constexpr std::int64_t shortest_travel = 5;

struct Point {
    std::int64_t x = 0;
    std::int64_t y = 0;

    bool operator==(const Point &) const = default;
};

[[nodiscard]] Point coordinates_of(std::int64_t id);
[[nodiscard]] std::int64_t distance(Point first, Point second);
[[nodiscard]] std::int64_t travel_seconds(std::int64_t distance, int divisor);

}

#endif
