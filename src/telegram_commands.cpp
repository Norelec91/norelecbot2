#include "telegram_commands.hpp"

#include "conquister_service.hpp"
#include "quote_service.hpp"
#include "text.hpp"

#include <algorithm>
#include <array>
#include <cctype>
#include <chrono>
#include <exception>
#include <format>
#include <iterator>
#include <limits>
#include <optional>
#include <utility>

namespace norelecbot {
namespace {

constexpr std::size_t leaderboard_size = 10;

using CommandHandler = void (*)(const CommandContext &context, std::string_view argument, std::string &reply);

struct CommandDefinition {
    std::string_view name;
    CommandHandler handler;
};

struct ParsedCommand {
    std::string name;
    std::string_view argument;
};

template <typename... Args>
void append(std::string &reply, std::format_string<Args...> format, Args &&...arguments) {
    std::format_to(std::back_inserter(reply), format, std::forward<Args>(arguments)...);
}

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

void missing_username_reply(std::string &reply) {
    append(reply, "Imposta uno username Telegram per giocare a {}.", conquister_place);
}

// The claim is already saved: a missing or unreadable quote must not turn the reply into an error.
std::optional<std::string> optional_random_quote(Storage &storage) {
    try {
        return quote_random(storage);
    } catch (const std::exception &) {
        return std::nullopt;
    }
}

void handle_claim(const CommandContext &context, std::string_view, std::string &reply) {
    if (context.username.empty()) {
        missing_username_reply(reply);
        return;
    }
    const std::string username{context.username};
    const std::int64_t now = std::chrono::duration_cast<std::chrono::seconds>(
                                 std::chrono::system_clock::now().time_since_epoch()
                             ).count();
    const ClaimResult result = conquister_claim(context.storage, context.user_id, username, now);
    if (result.status == ClaimStatus::already_held) {
        append(reply, "{} sei già in {}!", username, conquister_place);
        return;
    }
    if (!result.previous_username.empty()) {
        append(reply, "{} hai cacciato @{} da {}.\n", username, result.previous_username, conquister_place);
        append(reply, "{} hai guadagnato {} palle!\n", result.previous_username, result.earned);
    }
    append(reply, "{} sei in {}!", username, conquister_place);
    if (const std::optional<std::string> quote = optional_random_quote(context.storage)) {
        append(reply, "\n\n{}", *quote);
    }
}

void handle_leaderboard(const CommandContext &context, std::string_view, std::string &reply) {
    const Leaderboard leaderboard = conquister_leaderboard(context.storage, leaderboard_size);
    if (leaderboard.entries.empty()) {
        append(
            reply,
            "Classifica vuota. Scrivi \"{}\" per entrare in {}!",
            conquister_trigger,
            conquister_place
        );
        return;
    }
    append(reply, "🏆 Classifica {}:\n", conquister_place);
    for (std::size_t position = 1; const LeaderboardEntry &entry : leaderboard.entries) {
        append(reply, "\n{}. {} — {} palle", position++, entry.username, entry.score);
        if (entry.quotes_added > 0) {
            append(
                reply,
                " — 📜 {} {}",
                entry.quotes_added,
                entry.quotes_added == 1 ? "citazione" : "citazioni"
            );
        }
    }
    if (leaderboard.current) {
        append(reply, "\n\n🪐 In {} ora: {}", conquister_place, leaderboard.current->username);
    }
}

void handle_add_quote(const CommandContext &context, std::string_view argument, std::string &reply) {
    if (context.username.empty()) {
        missing_username_reply(reply);
        return;
    }
    const int cost = context.config.quote_cost;
    if (argument.empty()) {
        append(reply, "Uso: /addquote <testo>. Costa {} palle.", cost);
        return;
    }
    const std::string username{context.username};
    const std::string quote{argument};
    const QuoteAddResult result = quote_add(context.storage, username, quote, cost);
    if (result.status == QuoteAddStatus::insufficient_score) {
        append(
            reply,
            "{} ti servono {} palle per aggiungere una citazione (ne hai {}).",
            username,
            cost,
            result.available_score
        );
    } else if (result.status == QuoteAddStatus::duplicate) {
        reply += "Citazione già presente o non salvabile: nessun addebito.";
    } else {
        append(
            reply,
            "{} hai speso {} palle e aggiunto la citazione alla collezione!\n\n{}",
            username,
            cost,
            quote
        );
    }
}

void handle_quotes(const CommandContext &context, std::string_view argument, std::string &reply) {
    if (context.user_id != context.config.owner_id) {
        reply += "Solo il proprietario può vedere le citazioni.";
        return;
    }
    const std::optional<std::int64_t> requested = text::parse_int64(argument);
    const int page = requested && *requested > 0 && *requested <= std::numeric_limits<std::int32_t>::max()
        ? static_cast<int>(*requested)
        : 1;
    const QuotePage quotes = quote_page_load(context.storage, page);
    if (quotes.total == 0) {
        reply += "Nessuna citazione in collezione.";
        return;
    }
    append(
        reply,
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
        append(reply, "\n{}. {}{}", number++, shown, truncated ? "…" : "");
    }
    if (quotes.pages > 1) {
        reply += "\n\nUsa /quotes <pagina> per le altre pagine.";
    }
}

void handle_delete_quote(const CommandContext &context, std::string_view argument, std::string &reply) {
    if (context.user_id != context.config.owner_id) {
        reply += "Solo il proprietario può eliminare le citazioni.";
        return;
    }
    if (argument.empty()) {
        reply += "Uso: /delquote <numero da /quotes | testo esatto>.";
        return;
    }
    const std::optional<std::string> removed = quote_delete(context.storage, argument);
    if (!removed) {
        reply += "Citazione non trovata.";
        return;
    }
    append(reply, "Citazione eliminata: {}", *removed);
}

constexpr std::array commands{
    CommandDefinition{"/leaderboard", handle_leaderboard},
    CommandDefinition{"/addquote", handle_add_quote},
    CommandDefinition{"/quotes", handle_quotes},
    CommandDefinition{"/delquote", handle_delete_quote},
};

}

CommandResult telegram_command_dispatch(
    const CommandContext &context,
    std::string_view text,
    std::string &reply
) {
    reply.clear();
    try {
        const std::string_view message = text::trim(text);
        CommandHandler handler = nullptr;
        std::string_view argument;
        if (message == conquister_trigger) {
            if (context.config.conquister_chat_id == 0 ||
                context.chat_id == context.config.conquister_chat_id) {
                handler = handle_claim;
            }
        } else if (!message.empty()) {
            const ParsedCommand command = parse_command(message);
            argument = command.argument;
            const auto found = std::ranges::find_if(commands, [&command](const CommandDefinition &definition) {
                return definition.name == command.name;
            });
            if (found != commands.end()) {
                handler = found->handler;
            }
        }
        if (handler == nullptr) {
            return CommandResult::ignored;
        }
        handler(context, argument, reply);
        return CommandResult::replied;
    } catch (const std::exception &) {
        return CommandResult::error;
    }
}

}
