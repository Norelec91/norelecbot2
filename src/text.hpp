#ifndef NORELECBOT_TEXT_HPP
#define NORELECBOT_TEXT_HPP

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace norelecbot::text {

[[nodiscard]] bool equals_ignore_case(std::string_view left, std::string_view right);
[[nodiscard]] std::string_view trim(std::string_view text);
/* Surrounding whitespace and a leading '+' are allowed; empty input, trailing characters and overflow are not. */
[[nodiscard]] std::optional<std::int64_t> parse_int64(std::string_view text);
[[nodiscard]] bool contains_ignore_case(std::string_view text, std::string_view piece);
/* Drops the @ in front of a name, so that a quote does not tag whoever owns it on Telegram. */
[[nodiscard]] std::string strip_mentions(std::string_view text);
/* Invalid UTF-8 bytes count as one codepoint each. */
[[nodiscard]] std::size_t utf8_prefix_bytes(std::string_view text, std::size_t max_codepoints);
/* How many emoji there really are, keeping together what belongs together: a family joined by
   zero width joiners is one, a thumb with a skin tone is one, a flag is one. Nothing at all if
   there is something in there that is not an emoji. */
[[nodiscard]] std::optional<std::size_t> emoji_count(std::string_view text);
/* The same emoji, one by one, or nothing if there is something in there that is not an emoji. */
[[nodiscard]] std::optional<std::vector<std::string>> emoji_split(std::string_view text);

}

#endif
