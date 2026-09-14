#ifndef NORELECBOT_IRC_PROTOCOL_HPP
#define NORELECBOT_IRC_PROTOCOL_HPP

#include <cstddef>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace norelecbot::irc {

/* RFC 1459 allows 512 bytes per line, prefix and CRLF included; 400 leaves room for both. */
inline constexpr std::size_t max_text_bytes = 400;

struct Message {
    std::string prefix;
    std::string command;
    std::vector<std::string> params;

    [[nodiscard]] std::string_view param(std::size_t index) const;
    bool operator==(const Message &) const = default;
};

/* The line without its terminator; the command is upper cased, the trailing parameter is the last one. */
[[nodiscard]] std::optional<Message> parse(std::string_view line);
/* The parameters must not contain spaces; the text, when there is one, is sent as the trailing parameter. */
[[nodiscard]] std::string command_line(
    std::string_view command,
    std::span<const std::string_view> params,
    std::optional<std::string_view> text = std::nullopt
);
[[nodiscard]] std::string_view nick_of(std::string_view prefix);
/* Azzurra announces CASEMAPPING=ascii. */
[[nodiscard]] std::string to_lower(std::string_view name);
[[nodiscard]] bool same_name(std::string_view left, std::string_view right);
[[nodiscard]] bool is_channel(std::string_view target);
/* One message per reply: the lines are joined with a dash, and only what does not fit is split,
   never inside a UTF-8 sequence. */
[[nodiscard]] std::vector<std::string> split_text(std::string_view text, std::size_t max_bytes = max_text_bytes);

}

#endif
