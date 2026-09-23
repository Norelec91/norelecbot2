#ifndef NORELECBOT_ZODIAC_HPP
#define NORELECBOT_ZODIAC_HPP

#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace norelecbot::zodiac {

enum class Element { water, fire, air, earth };

struct Sign {
    std::string_view name;
    std::string_view symbol;
    Element element;
};

/* A player whose real sign is known, rather than the one their name would give. */
struct Override {
    std::string username;
    std::string sign;
};

using Overrides = std::span<const Override>;

[[nodiscard]] std::optional<Sign> sign_named(std::string_view name);
/* Always the same sign for the same name, whatever the spelling, unless the owner set it by hand. */
[[nodiscard]] Sign sign_of(std::string_view username, Overrides overrides = {});
/* The house of the day, which turns at midnight: water, fire, air, earth. */
[[nodiscard]] Element element_of_day(std::int64_t now);
/* First second of the current local day. */
[[nodiscard]] std::int64_t day_start(std::int64_t now);
/* First second of the next local day, accounting for daylight-saving changes. */
[[nodiscard]] std::int64_t next_day_start(std::int64_t now);
[[nodiscard]] std::string_view element_name(Element element);
/* Fire against water, air against earth. */
[[nodiscard]] bool opposed(Element first, Element second);
/* What a hold is worth today: 125 in the right house, 75 in the opposite one, 100 otherwise. */
[[nodiscard]] int percent_for(std::string_view username, std::int64_t now, Overrides overrides = {});

}

#endif
