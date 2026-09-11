#include "telegram_commands.h"

#include "conquister_service.h"
#include "quote_service.h"
#include "text.h"

#include <ctype.h>
#include <stdint.h>
#include <string.h>
#include <time.h>

static constexpr size_t TELEGRAM_LEADERBOARD_SIZE = 10;

typedef bool (*TelegramCommandHandler)(
    const TelegramCommandContext *context,
    const char *argument,
    DynamicString *reply
);

typedef struct {
    const char *name;
    TelegramCommandHandler handler;
} TelegramCommandDefinition;

static char *trim_copy(Arena *arena, const char *text) {
    char *copy = arena_strdup(arena, text);
    return copy != nullptr ? text_trim(copy) : nullptr;
}

static void command_and_argument(char *text, char command[64], char **argument) {
    size_t length = strcspn(text, " \t\r\n");
    if (length >= 64U) {
        length = 63U;
    }
    memcpy(command, text, length);
    command[length] = '\0';
    char *suffix = strchr(command, '@');
    if (suffix != nullptr) {
        *suffix = '\0';
    }
    for (char *cursor = command; *cursor != '\0'; ++cursor) {
        *cursor = (char)tolower((unsigned char)*cursor);
    }
    char *cursor = text + strcspn(text, " \t\r\n");
    while (isspace((unsigned char)*cursor) != 0) {
        ++cursor;
    }
    *argument = cursor;
}

static bool missing_username_reply(DynamicString *reply) {
    return dynamic_string_appendf(
        reply,
        "Imposta uno username Telegram per giocare a %s.",
        TELEGRAM_CONQUISTER_PLACE
    );
}

static bool append_random_quote(const TelegramCommandContext *context, DynamicString *reply) {
    char *quote = nullptr;
    // The claim is already saved: a missing or unreadable quote must not turn the reply into an error.
    if (!quote_random(context->storage, context->arena, &quote) || quote == nullptr) {
        return true;
    }
    return dynamic_string_appendf(reply, "\n\n%s", quote);
}

static bool handle_claim(
    const TelegramCommandContext *context,
    [[maybe_unused]] const char *argument,
    DynamicString *reply
) {
    if (context->username == nullptr || *context->username == '\0') {
        return missing_username_reply(reply);
    }
    ClaimResult result;
    if (!conquister_claim(
            context->storage,
            context->user_id,
            context->username,
            (int64_t)time(nullptr),
            &result
        )) {
        return false;
    }
    if (result.status == CLAIM_ALREADY_HELD) {
        return dynamic_string_appendf(
            reply,
            "%s sei già in %s!",
            context->username,
            TELEGRAM_CONQUISTER_PLACE
        );
    }
    if (result.previous_username[0] != '\0' &&
        (!dynamic_string_appendf(
            reply,
            "%s hai cacciato @%s da %s.\n",
            context->username,
            result.previous_username,
            TELEGRAM_CONQUISTER_PLACE
        ) ||
         !dynamic_string_appendf(
             reply,
             "%s hai guadagnato %lld palle!\n",
             result.previous_username,
             (long long)result.earned
         ))) {
        return false;
    }
    return dynamic_string_appendf(
               reply,
               "%s sei in %s!",
               context->username,
               TELEGRAM_CONQUISTER_PLACE
           ) &&
           append_random_quote(context, reply);
}

static bool handle_leaderboard(
    const TelegramCommandContext *context,
    [[maybe_unused]] const char *argument,
    DynamicString *reply
) {
    Leaderboard leaderboard;
    if (!conquister_leaderboard(
            context->storage,
            context->arena,
            TELEGRAM_LEADERBOARD_SIZE,
            &leaderboard
        )) {
        return false;
    }
    bool ok = true;
    if (leaderboard.count == 0U) {
        ok = dynamic_string_appendf(
            reply,
            "Classifica vuota. Scrivi \"%s\" per entrare in %s!",
            TELEGRAM_CONQUISTER_TRIGGER,
            TELEGRAM_CONQUISTER_PLACE
        );
    } else {
        ok = dynamic_string_appendf(reply, "🏆 Classifica %s:\n", TELEGRAM_CONQUISTER_PLACE);
        for (size_t index = 0U; ok && index < leaderboard.count; ++index) {
            LeaderboardEntry *entry = &leaderboard.entries[index];
            ok = dynamic_string_appendf(
                reply,
                "\n%zu. %s — %lld palle",
                index + 1U,
                entry->username,
                (long long)entry->score
            );
            if (ok && entry->quotes_added > 0) {
                ok = dynamic_string_appendf(
                    reply,
                    " — 📜 %lld %s",
                    (long long)entry->quotes_added,
                    entry->quotes_added == 1 ? "citazione" : "citazioni"
                );
            }
        }
        if (ok && leaderboard.current_username != nullptr) {
            ok = dynamic_string_appendf(
                reply,
                "\n\n🪐 In %s ora: %s",
                TELEGRAM_CONQUISTER_PLACE,
                leaderboard.current_username
            );
        }
    }
    return ok;
}

static bool handle_add_quote(
    const TelegramCommandContext *context,
    const char *argument,
    DynamicString *reply
) {
    if (context->username == nullptr || *context->username == '\0') {
        return missing_username_reply(reply);
    }
    if (*argument == '\0') {
        return dynamic_string_appendf(
            reply,
            "Uso: /addquote <testo>. Costa %d palle.",
            context->config->quote_cost
        );
    }
    QuoteAddResult result;
    if (!quote_add(
            context->storage,
            context->username,
            argument,
            context->config->quote_cost,
            &result
        )) {
        return false;
    }
    if (result.status == QUOTE_INSUFFICIENT_SCORE) {
        return dynamic_string_appendf(
            reply,
            "%s ti servono %d palle per aggiungere una citazione (ne hai %lld).",
            context->username,
            context->config->quote_cost,
            (long long)result.available_score
        );
    }
    if (result.status == QUOTE_DUPLICATE) {
        return dynamic_string_append(reply, "Citazione già presente o non salvabile: nessun addebito.");
    }
    return dynamic_string_appendf(
        reply,
        "%s hai speso %d palle e aggiunto la citazione alla collezione!\n\n%s",
        context->username,
        context->config->quote_cost,
        argument
    );
}

static bool handle_quotes(
    const TelegramCommandContext *context,
    const char *argument,
    DynamicString *reply
) {
    if (context->user_id != context->config->owner_id) {
        return dynamic_string_append(reply, "Solo il proprietario può vedere le citazioni.");
    }
    int64_t requested = 0;
    int page = text_parse_int64(argument, &requested) && requested > 0 && requested <= INT32_MAX
        ? (int)requested
        : 1;
    QuotePage quotes;
    if (!quote_page_load(context->storage, context->arena, page, &quotes)) {
        return false;
    }
    bool ok = true;
    if (quotes.total == 0U) {
        ok = dynamic_string_append(reply, "Nessuna citazione in collezione.");
    } else {
        size_t last_number = quotes.first_number + quotes.count - 1U;
        ok = dynamic_string_appendf(
            reply,
            "📜 Citazioni %zu-%zu di %zu (pagina %zu/%zu):",
            quotes.first_number,
            last_number,
            quotes.total,
            quotes.page,
            quotes.pages
        );
        for (size_t index = 0U; ok && index < quotes.count; ++index) {
            const char *quote = quotes.items[index];
            size_t first_eighty = text_utf8_prefix_bytes(quote, 80U);
            bool truncated = quote[first_eighty] != '\0';
            size_t bytes = truncated ? text_utf8_prefix_bytes(quote, 77U) : strlen(quote);
            ok = dynamic_string_appendf(reply, "\n%zu. ", quotes.first_number + index) &&
                 dynamic_string_append_n(reply, quote, bytes) &&
                 (!truncated || dynamic_string_append(reply, "…"));
        }
        if (ok && quotes.pages > 1U) {
            ok = dynamic_string_append(reply, "\n\nUsa /quotes <pagina> per le altre pagine.");
        }
    }
    return ok;
}

static bool handle_delete_quote(
    const TelegramCommandContext *context,
    const char *argument,
    DynamicString *reply
) {
    if (context->user_id != context->config->owner_id) {
        return dynamic_string_append(reply, "Solo il proprietario può eliminare le citazioni.");
    }
    if (*argument == '\0') {
        return dynamic_string_append(reply, "Uso: /delquote <numero da /quotes | testo esatto>.");
    }
    char *removed = nullptr;
    if (!quote_delete(context->storage, context->arena, argument, &removed)) {
        return false;
    }
    if (removed == nullptr) {
        return dynamic_string_append(reply, "Citazione non trovata.");
    }
    return dynamic_string_appendf(reply, "Citazione eliminata: %s", removed);
}

static const TelegramCommandDefinition TELEGRAM_COMMANDS[] = {
    {"/classifica", handle_leaderboard},
    {"/addquote", handle_add_quote},
    {"/quotes", handle_quotes},
    {"/delquote", handle_delete_quote},
};

TelegramCommandResult telegram_command_dispatch(
    const TelegramCommandContext *context,
    const char *text,
    DynamicString *reply
) {
    if (context == nullptr || context->storage == nullptr || context->arena == nullptr ||
        context->config == nullptr || text == nullptr || reply == nullptr) {
        return TELEGRAM_COMMAND_ERROR;
    }
    char *message = trim_copy(context->arena, text);
    if (message == nullptr) {
        return TELEGRAM_COMMAND_ERROR;
    }
    dynamic_string_reset(reply);
    if (*message == '\0') {
        return TELEGRAM_COMMAND_IGNORED;
    }

    TelegramCommandHandler handler = nullptr;
    const char *argument = "";
    if (strcmp(message, TELEGRAM_CONQUISTER_TRIGGER) == 0) {
        handler = handle_claim;
    } else {
        char command[64];
        char *parsed_argument = nullptr;
        command_and_argument(message, command, &parsed_argument);
        argument = parsed_argument;
        size_t command_count = sizeof(TELEGRAM_COMMANDS) / sizeof(TELEGRAM_COMMANDS[0]);
        for (size_t index = 0U; index < command_count; ++index) {
            if (strcmp(command, TELEGRAM_COMMANDS[index].name) == 0) {
                handler = TELEGRAM_COMMANDS[index].handler;
                break;
            }
        }
    }
    if (handler == nullptr) {
        return TELEGRAM_COMMAND_IGNORED;
    }
    return handler(context, argument, reply) ? TELEGRAM_COMMAND_REPLIED : TELEGRAM_COMMAND_ERROR;
}
