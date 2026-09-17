#include "zodiac.hpp"

#include <array>
#include <cctype>
#include <chrono>

namespace norelecbot::zodiac {
namespace {

constexpr std::array signs{
    Sign{"ariete", "♈", Element::fire},
    Sign{"toro", "♉", Element::earth},
    Sign{"gemelli", "♊", Element::air},
    Sign{"cancro", "♋", Element::water},
    Sign{"leone", "♌", Element::fire},
    Sign{"vergine", "♍", Element::earth},
    Sign{"bilancia", "♎", Element::air},
    Sign{"scorpione", "♏", Element::water},
    Sign{"sagittario", "♐", Element::fire},
    Sign{"capricorno", "♑", Element::earth},
    Sign{"acquario", "♒", Element::air},
    Sign{"pesci", "♓", Element::water},
};

constexpr std::array houses{Element::water, Element::fire, Element::air, Element::earth};

constexpr int matching_percent = 125;
constexpr int opposed_percent = 75;
constexpr int plain_percent = 100;

/* FNV-1a, so that the same name always falls on the same sign, on any machine. */
std::uint64_t digest(std::string_view text) {
    std::uint64_t hash = 0xcbf29ce484222325ULL;
    for (const char character : text) {
        const auto lowered = static_cast<unsigned char>(std::tolower(static_cast<unsigned char>(character)));
        hash ^= lowered;
        hash *= 0x100000001b3ULL;
    }
    return hash;
}

}

Sign sign_of(std::string_view username) {
    return signs.at(digest(username) % signs.size());
}

Element element_of_day(std::int64_t now) {
    const std::chrono::sys_seconds instant{std::chrono::seconds{now}};
    const std::chrono::local_seconds local = std::chrono::current_zone()->to_local(instant);
    const auto day = std::chrono::floor<std::chrono::days>(local).time_since_epoch().count();
    const auto house = static_cast<std::size_t>(((day % 4) + 4) % 4);
    return houses.at(house);
}

std::string_view element_name(Element element) {
    switch (element) {
    case Element::water:
        return "acqua";
    case Element::fire:
        return "fuoco";
    case Element::air:
        return "vento";
    case Element::earth:
        break;
    }
    return "terra";
}

bool opposed(Element first, Element second) {
    return (first == Element::fire && second == Element::water) ||
           (first == Element::water && second == Element::fire) ||
           (first == Element::air && second == Element::earth) ||
           (first == Element::earth && second == Element::air);
}

int percent_for(std::string_view username, std::int64_t now) {
    if (username.empty()) {
        return plain_percent;
    }
    const Element house = element_of_day(now);
    const Element own = sign_of(username).element;
    if (own == house) {
        return matching_percent;
    }
    return opposed(own, house) ? opposed_percent : plain_percent;
}

}
