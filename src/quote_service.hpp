#ifndef NORELECBOT_QUOTE_SERVICE_HPP
#define NORELECBOT_QUOTE_SERVICE_HPP

#include "storage.hpp"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace norelecbot {

enum class QuoteAddStatus { added, duplicate, insufficient_score };

struct QuoteAddResult {
    QuoteAddStatus status = QuoteAddStatus::added;
    std::int64_t available_score = 0;
};

struct QuotePage {
    std::vector<std::string> items;
    std::size_t total = 0;
    std::size_t page = 0;
    std::size_t pages = 0;
    std::size_t first_number = 0;
};

[[nodiscard]] QuoteAddResult quote_add(
    Storage &storage,
    const std::string &username,
    const std::string &quote,
    int cost
);
[[nodiscard]] QuotePage quote_page_load(Storage &storage, int requested_page);
[[nodiscard]] std::optional<std::string> quote_random(Storage &storage);
/* The selector is a position shown by /quotes or the exact quote text. */
[[nodiscard]] std::optional<std::string> quote_delete(Storage &storage, std::string_view selector);

}

#endif
