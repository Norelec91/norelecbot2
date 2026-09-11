#include "telegram_commands.hpp"

#include "game.hpp"
#include "text.hpp"

#include <algorithm>
#include <array>
#include <cctype>
#include <chrono>
#include <exception>
#include <format>
#include <limits>
#include <utility>

namespace norelecbot {
namespace {

constexpr std::size_t leaderboard_size = 10;
constexpr std::string_view internal_error_reply = "Errore interno: riprova tra poco.";

using CommandHandler = std::string (*)(const CommandContext &context, std::string_view argument);

struct CommandDefinition {
    std::string_view name;
    CommandHandler handler;
};

struct ParsedCommand {
    std::string name;
    std::string_view argument;
};

ParsedCommand parse_command(std::string_view message) {
    const std::size_t length = std::min(message.find_first_of(" \t\r\n"), message.size());
    std::string name{message.substr(0, length)};
    if (const std::size_t suffix = name.find('@'); suffix != std::string::npos) {
        name.resize(suffix);
    }
    std::ranges::transform(name, name.begin(), [](char character) {
        return static_cast<char>(std::tolower(static_cast<unsigned char>(character)));
    });
    return {std::move(name), text::trim(message.substr(length))};
}

std::string missing_username_reply() {
    return std::format("Imposta uno username Telegram per giocare a {}.", conquister_place);
}

// The claim is already saved: a missing or unreadable quote must not turn the reply into an error.
std::optional<std::string> optional_random_quote(Storage &storage) {
    try {
        return quote_random(storage);
    } catch (const std::exception &) {
        return std::nullopt;
    }
}

std::string handle_claim(const CommandContext &context, std::string_view) {
    if (context.username.empty()) {
        return missing_username_reply();
    }
    const std::string username{context.username};
    const std::int64_t now = std::chrono::duration_cast<std::chrono::seconds>(
                                 std::chrono::system_clock::now().time_since_epoch()
                             ).count();
    const ClaimResult result = conquister_claim(context.storage, context.user_id, username, now);
    if (result.status == ClaimStatus::already_held) {
        return std::format("{} sei già in {}!", username, conquister_place);
    }
    std::string reply;
    if (!result.previous_username.empty()) {
        reply = std::format(
            "{0} hai cacciato @{1} da {2}.\n{1} hai guadagnato {3} palle!\n",
            username,
            result.previous_username,
            conquister_place,
            result.earned
        );
    }
    reply += std::format("{} sei in {}!", username, conquister_place);
    if (const std::optional<std::string> quote = optional_random_quote(context.storage)) {
        reply += std::format("\n\n{}", *quote);
    }
    return reply;
}

std::string handle_leaderboard(const CommandContext &context, std::string_view) {
    const Leaderboard leaderboard = conquister_leaderboard(context.storage, leaderboard_size);
    if (leaderboard.entries.empty()) {
        return std::format(
            "Classifica vuota. Scrivi \"{}\" per entrare in {}!",
            conquister_trigger,
            conquister_place
        );
    }
    std::string reply = std::format("🏆 Classifica {}:\n", conquister_place);
    for (std::size_t position = 1; const LeaderboardEntry &entry : leaderboard.entries) {
        reply += std::format("\n{}. {} — {} palle", position++, entry.username, entry.score);
        if (entry.quotes_added > 0) {
            reply += std::format(
                " — 📜 {} {}",
                entry.quotes_added,
                entry.quotes_added == 1 ? "citazione" : "citazioni"
            );
        }
    }
    if (leaderboard.current) {
        reply += std::format("\n\n🪐 In {} ora: {}", conquister_place, leaderboard.current->username);
    }
    return reply;
}

std::string handle_add_quote(const CommandContext &context, std::string_view argument) {
    if (context.username.empty()) {
        return missing_username_reply();
    }
    const int cost = context.config.quote_cost;
    if (argument.empty()) {
        return std::format("Uso: /addquote <testo>. Costa {} palle.", cost);
    }
    const std::string username{context.username};
    const std::string quote{argument};
    const QuoteAddResult result = quote_add(context.storage, username, quote, cost);
    if (result.status == QuoteAddStatus::insufficient_score) {
        return std::format(
            "{} ti servono {} palle per aggiungere una citazione (ne hai {}).",
            username,
            cost,
            result.available_score
        );
    }
    if (result.status == QuoteAddStatus::duplicate) {
        return "Citazione già presente o non salvabile: nessun addebito.";
    }
    return std::format(
        "{} hai speso {} palle e aggiunto la citazione alla collezione!\n\n{}",
        username,
        cost,
        quote
    );
}

std::string handle_quotes(const CommandContext &context, std::string_view argument) {
    if (context.user_id != context.config.owner_id) {
        return "Solo il proprietario può vedere le citazioni.";
    }
    const std::optional<std::int64_t> requested = text::parse_int64(argument);
    const int page = requested && *requested > 0 && *requested <= std::numeric_limits<std::int32_t>::max()
        ? static_cast<int>(*requested)
        : 1;
    const QuotePage quotes = quote_page_load(context.storage, page);
    if (quotes.total == 0) {
        return "Nessuna citazione in collezione.";
    }
    std::string reply = std::format(
        "📜 Citazioni {}-{} di {} (pagina {}/{}):",
        quotes.first_number,
        quotes.first_number + quotes.items.size() - 1,
        quotes.total,
        quotes.page,
        quotes.pages
    );
    for (std::size_t number = quotes.first_number; const std::string &quote : quotes.items) {
        const bool truncated = text::utf8_prefix_bytes(quote, 80) < quote.size();
        const std::string_view shown = truncated
            ? std::string_view{quote}.substr(0, text::utf8_prefix_bytes(quote, 77))
            : std::string_view{quote};
        reply += std::format("\n{}. {}{}", number++, shown, truncated ? "…" : "");
    }
    if (quotes.pages > 1) {
        reply += "\n\nUsa /quotes <pagina> per le altre pagine.";
    }
    return reply;
}

std::string handle_delete_quote(const CommandContext &context, std::string_view argument) {
    if (context.user_id != context.config.owner_id) {
        return "Solo il proprietario può eliminare le citazioni.";
    }
    if (argument.empty()) {
        return "Uso: /delquote <numero da /quotes | testo esatto>.";
    }
    const std::optional<std::string> removed = quote_delete(context.storage, argument);
    return removed ? std::format("Citazione eliminata: {}", *removed) : "Citazione non trovata.";
}

constexpr std::array commands{
    CommandDefinition{"/leaderboard", handle_leaderboard},
    CommandDefinition{"/addquote", handle_add_quote},
    CommandDefinition{"/quotes", handle_quotes},
    CommandDefinition{"/delquote", handle_delete_quote},
};

}

std::optional<std::string> telegram_command_dispatch(const CommandContext &context, std::string_view text) {
    try {
        const std::string_view message = text::trim(text);
        if (message == conquister_trigger) {
            const std::int64_t allowed_chat = context.config.conquister_chat_id;
            if (allowed_chat != 0 && context.chat_id != allowed_chat) {
                return std::nullopt;
            }
            return handle_claim(context, {});
        }
        if (message.empty()) {
            return std::nullopt;
        }
        const ParsedCommand command = parse_command(message);
        const auto found = std::ranges::find_if(commands, [&command](const CommandDefinition &definition) {
            return definition.name == command.name;
        });
        if (found == commands.end()) {
            return std::nullopt;
        }
        return found->handler(context, command.argument);
    } catch (const std::exception &) {
        return std::string{internal_error_reply};
    }
}

}
