#ifndef NORELECBOT_ZODIAC_HPP
#define NORELECBOT_ZODIAC_HPP

#include <cstdint>
#include <string_view>

namespace norelecbot::zodiac {

enum class Element { water, fire, air, earth };

struct Sign {
    std::string_view name;
    std::string_view symbol;
    Element element;
};

/* Always the same sign for the same name, whatever the spelling. */
[[nodiscard]] Sign sign_of(std::string_view username);
/* The house of the day, which turns at midnight: water, fire, air, earth. */
[[nodiscard]] Element element_of_day(std::int64_t now);
[[nodiscard]] std::string_view element_name(Element element);
/* Fire against water, air against earth. */
[[nodiscard]] bool opposed(Element first, Element second);
/* What a hold is worth today: 125 in the right house, 75 in the opposite one, 100 otherwise. */
[[nodiscard]] int percent_for(std::string_view username, std::int64_t now);

}

#endif
