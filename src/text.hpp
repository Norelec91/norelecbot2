#ifndef NORELECBOT_TEXT_HPP
#define NORELECBOT_TEXT_HPP

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

namespace norelecbot::text {

[[nodiscard]] bool equals_ignore_case(std::string_view left, std::string_view right);
[[nodiscard]] std::string_view trim(std::string_view text);
/* Surrounding whitespace and a leading '+' are allowed; empty input, trailing characters and overflow are not. */
[[nodiscard]] std::optional<std::int64_t> parse_int64(std::string_view text);
[[nodiscard]] bool contains_ignore_case(std::string_view text, std::string_view piece);
[[nodiscard]] std::string to_lower_copy(std::string_view text);
/* Lower case, without the spaces and punctuation somebody sprinkles to walk past a filter. */
[[nodiscard]] std::string squeeze(std::string_view text);
/* Drops the @ in front of a name, so that a quote does not tag whoever owns it on Telegram. */
[[nodiscard]] std::string strip_mentions(std::string_view text);
/* Invalid UTF-8 bytes count as one codepoint each. */
[[nodiscard]] std::size_t utf8_prefix_bytes(std::string_view text, std::size_t max_codepoints);

}

#endif
