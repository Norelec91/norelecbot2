#include "irc_protocol.hpp"

#include <algorithm>
#include <cctype>

namespace norelecbot::irc {
namespace {

constexpr std::string_view channel_prefixes = "&#";

std::string_view take_until_space(std::string_view &rest) {
    const std::size_t end = rest.find(' ');
    const std::string_view token = rest.substr(0, end);
    rest.remove_prefix(end == std::string_view::npos ? rest.size() : end);
    return token;
}

void skip_spaces(std::string_view &rest) {
    while (rest.starts_with(' ')) {
        rest.remove_prefix(1);
    }
}

bool is_continuation(char character) {
    const auto byte = static_cast<unsigned char>(character);
    return byte >= 0x80U && byte <= 0xBFU;
}

/* The largest cut not longer than max_bytes that leaves every UTF-8 sequence whole. */
std::size_t boundary_before(std::string_view text, std::size_t max_bytes) {
    std::size_t cut = std::min(max_bytes, text.size());
    while (cut > 0 && cut < text.size() && is_continuation(text[cut])) {
        --cut;
    }
    return cut;
}

}

std::string_view Message::param(std::size_t index) const {
    return index < params.size() ? std::string_view{params[index]} : std::string_view{};
}

std::optional<Message> parse(std::string_view line) {
    while (line.ends_with('\r') || line.ends_with('\n')) {
        line.remove_suffix(1);
    }
    skip_spaces(line);
    Message message;
    if (line.starts_with(':')) {
        line.remove_prefix(1);
        message.prefix = take_until_space(line);
        skip_spaces(line);
        if (message.prefix.empty()) {
            return std::nullopt;
        }
    }
    message.command = take_until_space(line);
    if (message.command.empty()) {
        return std::nullopt;
    }
    std::ranges::transform(message.command, message.command.begin(), [](char character) {
        return static_cast<char>(std::toupper(static_cast<unsigned char>(character)));
    });
    while (true) {
        skip_spaces(line);
        if (line.empty()) {
            break;
        }
        if (line.starts_with(':')) {
            message.params.emplace_back(line.substr(1));
            break;
        }
        message.params.emplace_back(take_until_space(line));
    }
    return message;
}

std::string command_line(
    std::string_view command,
    std::span<const std::string_view> params,
    std::optional<std::string_view> text
) {
    std::string line{command};
    for (const std::string_view param : params) {
        line += ' ';
        line += param;
    }
    if (text) {
        line += " :";
        line += *text;
    }
    line += "\r\n";
    return line;
}

std::string_view nick_of(std::string_view prefix) {
    return prefix.substr(0, std::min(prefix.find('!'), prefix.find('@')));
}

std::string to_lower(std::string_view name) {
    std::string lowered{name};
    std::ranges::transform(lowered, lowered.begin(), [](char character) {
        return static_cast<char>(std::tolower(static_cast<unsigned char>(character)));
    });
    return lowered;
}

bool same_name(std::string_view left, std::string_view right) {
    return to_lower(left) == to_lower(right);
}

bool is_channel(std::string_view target) {
    return !target.empty() && channel_prefixes.contains(target.front());
}

std::vector<std::string> split_text(std::string_view text, std::size_t max_bytes) {
    max_bytes = std::max<std::size_t>(max_bytes, 1);
    std::vector<std::string> lines;
    while (!text.empty()) {
        std::string_view line = text.substr(0, text.find('\n'));
        text.remove_prefix(std::min(line.size() + 1, text.size()));
        while (line.ends_with('\r')) {
            line.remove_suffix(1);
        }
        while (!line.empty()) {
            std::size_t cut = boundary_before(line, max_bytes);
            std::size_t skip = 0;
            if (cut < line.size()) {
                if (const std::size_t space = line.rfind(' ', cut); space != std::string_view::npos &&
                    space * 2 >= cut) {
                    cut = space;
                    skip = 1;
                }
            }
            if (cut == 0) {
                break;
            }
            lines.emplace_back(line.substr(0, cut));
            line.remove_prefix(std::min(cut + skip, line.size()));
        }
    }
    return lines;
}

}
