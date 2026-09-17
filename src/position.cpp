#include "position.hpp"

#include <algorithm>
#include <cmath>

namespace norelecbot::position {

Point coordinates_of(std::int64_t id) {
    const std::int64_t place = ((id % ids) + ids) % ids;
    return {place % plane_side, place / plane_side};
}

std::int64_t distance(Point first, Point second) {
    const auto width = static_cast<double>(first.x - second.x);
    const auto height = static_cast<double>(first.y - second.y);
    return static_cast<std::int64_t>(std::llround(std::hypot(width, height)));
}

std::int64_t travel_seconds(std::int64_t distance, int divisor) {
    if (divisor <= 0) {
        return shortest_travel;
    }
    return std::max(shortest_travel, distance / divisor);
}

}
