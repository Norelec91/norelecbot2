#ifndef NORELECBOT_PICTURES_HPP
#define NORELECBOT_PICTURES_HPP

#include <string_view>

namespace norelecbot {

/* A picture the bot shows on Telegram, built into it from assets/, so that nothing travels beside the binary.
   All of them are in the public domain: photographs by the United States government and a plain symbol. */
struct Picture {
    std::string_view name;
    std::string_view file_name;
    std::string_view mime;
    std::string_view bytes;
};

/* The picture by its name (launch, blast, impact, fallout), or nothing when there is none. */
[[nodiscard]] const Picture *picture(std::string_view name);

}

#endif
