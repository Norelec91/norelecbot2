#include "quote_service.hpp"

#include "text.hpp"

#include <algorithm>
#include <limits>

namespace norelecbot {
namespace {

constexpr std::size_t quotes_page_size = 30;

std::optional<std::size_t> find_quote(const StorageTransaction &transaction, std::string_view quote) {
    for (std::size_t index = 0; index < transaction.quotes_count(); ++index) {
        if (transaction.quote(index) == quote) {
            return index;
        }
    }
    return std::nullopt;
}

}

QuoteAddResult quote_add(
    Storage &storage,
    const std::string &username,
    const std::string &quote,
    int cost
) {
    StorageTransaction transaction{storage, StorageDocuments::all};
    const std::int64_t score = transaction.score(username);
    if (score < cost) {
        return {QuoteAddStatus::insufficient_score, score};
    }
    if (find_quote(transaction, quote)) {
        return {QuoteAddStatus::duplicate, score};
    }
    const std::int64_t added = transaction.quotes_added(username);
    if (added == std::numeric_limits<std::int64_t>::max()) {
        throw StorageError("Quote counter overflow");
    }
    transaction.append_quote(quote);
    transaction.set_score(username, score - cost);
    transaction.set_quotes_added(username, added + 1);
    transaction.commit();
    return {QuoteAddStatus::added, score - cost};
}

QuotePage quote_page_load(Storage &storage, int requested_page) {
    const StorageTransaction transaction{storage, StorageDocuments::quotes};
    QuotePage page;
    page.total = transaction.quotes_count();
    if (page.total == 0) {
        return page;
    }
    page.pages = ((page.total - 1) / quotes_page_size) + 1;
    page.page = std::min(requested_page > 0 ? static_cast<std::size_t>(requested_page) : 1U, page.pages);
    const std::size_t offset = (page.page - 1) * quotes_page_size;
    page.first_number = offset + 1;
    const std::size_t count = std::min(page.total - offset, quotes_page_size);
    page.items.reserve(count);
    for (std::size_t index = 0; index < count; ++index) {
        page.items.emplace_back(transaction.quote(offset + index));
    }
    return page;
}

std::optional<std::string> quote_random(Storage &storage) {
    StorageTransaction transaction{storage, StorageDocuments::quotes};
    const std::size_t count = transaction.quotes_count();
    if (count == 0) {
        return std::nullopt;
    }
    return std::string{transaction.quote(transaction.random_index(count))};
}

std::optional<std::string> quote_delete(Storage &storage, std::string_view selector) {
    StorageTransaction transaction{storage, StorageDocuments::quotes};
    std::optional<std::size_t> selected;
    const std::optional<std::int64_t> position = text::parse_int64(selector);
    if (position && *position > 0 && static_cast<std::uint64_t>(*position) <= transaction.quotes_count()) {
        selected = static_cast<std::size_t>(*position - 1);
    } else {
        selected = find_quote(transaction, selector);
    }
    if (!selected) {
        return std::nullopt;
    }
    std::string removed{transaction.quote(*selected)};
    transaction.remove_quote(*selected);
    transaction.commit();
    return removed;
}

}
