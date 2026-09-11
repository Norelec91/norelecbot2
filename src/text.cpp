#include "text.hpp"

#include <algorithm>
#include <cctype>
#include <charconv>
#include <system_error>

namespace norelecbot::text {
namespace {

bool is_space(char character) {
    return std::isspace(static_cast<unsigned char>(character)) != 0;
}

char to_lower(char character) {
    return static_cast<char>(std::tolower(static_cast<unsigned char>(character)));
}

}

bool equals_ignore_case(std::string_view left, std::string_view right) {
    return std::ranges::equal(left, right, [](char first, char second) {
        return to_lower(first) == to_lower(second);
    });
}

std::string_view trim(std::string_view text) {
    while (!text.empty() && is_space(text.front())) {
        text.remove_prefix(1);
    }
    while (!text.empty() && is_space(text.back())) {
        text.remove_suffix(1);
    }
    return text;
}

std::optional<std::int64_t> parse_int64(std::string_view text) {
    text = trim(text);
    if (text.starts_with('+') && !text.substr(1).starts_with('-')) {
        text.remove_prefix(1);
    }
    std::int64_t value = 0;
    const char *begin = text.data();
    const char *end = begin + text.size();
    const auto [parsed_end, error] = std::from_chars(begin, end, value);
    if (error != std::errc{} || parsed_end != end) {
        return std::nullopt;
    }
    return value;
}

std::size_t utf8_prefix_bytes(std::string_view text, std::size_t max_codepoints) {
    std::size_t bytes = 0;
    for (std::size_t codepoints = 0; bytes < text.size() && codepoints < max_codepoints; ++codepoints) {
        const auto lead = static_cast<unsigned char>(text[bytes]);
        std::size_t width = 1;
        if ((lead & 0xE0U) == 0xC0U) {
            width = 2;
        } else if ((lead & 0xF0U) == 0xE0U) {
            width = 3;
        } else if ((lead & 0xF8U) == 0xF0U) {
            width = 4;
        }
        for (std::size_t index = 1; index < width; ++index) {
            if (bytes + index >= text.size() ||
                static_cast<unsigned char>(text[bytes + index]) < 0x80U ||
                static_cast<unsigned char>(text[bytes + index]) > 0xBFU) {
                width = 1;
                break;
            }
        }
        bytes += width;
    }
    return bytes;
}

}
