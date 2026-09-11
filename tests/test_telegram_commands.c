#include "quote_service.h"
#include "telegram_commands.h"
#include "test_paths.h"

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int main(void) {
    AppConfig config = {};
    test_paths("telegram-command-test", config.conquister_path, config.quotes_path);
    config.owner_id = 99;
    config.quote_cost = 1000;

    Storage storage = {};
    Arena arena = {};
    DynamicString reply = {};
    assert(storage_open(&storage, config.conquister_path, config.quotes_path));
    assert(dynamic_string_init(&reply, &arena, 256U));
    TelegramCommandContext context = {
        .storage = &storage,
        .arena = &arena,
        .config = &config,
        .user_id = 1,
        .username = "alice",
    };

    assert(telegram_command_dispatch(nullptr, "ciao", &reply) == TELEGRAM_COMMAND_ERROR);
    assert(telegram_command_dispatch(&context, "ciao", &reply) == TELEGRAM_COMMAND_IGNORED);
    assert(telegram_command_dispatch(&context, "   ", &reply) == TELEGRAM_COMMAND_IGNORED);
    assert(telegram_command_dispatch(&context, "/classifica", &reply) == TELEGRAM_COMMAND_REPLIED);
    assert(strcmp(
               reply.data,
               "Classifica vuota. Scrivi \"We @TheConquister37\" per entrare in @TheConquister37!"
           ) == 0);
    context.username = nullptr;
    assert(telegram_command_dispatch(&context, "We @TheConquister37", &reply) ==
           TELEGRAM_COMMAND_REPLIED);
    assert(strstr(reply.data, "Imposta uno username") != nullptr);
    context.username = "alice";
    assert(telegram_command_dispatch(&context, "We @TheConquister37", &reply) ==
           TELEGRAM_COMMAND_REPLIED);
    assert(strstr(reply.data, "alice sei in ") != nullptr);
    assert(strchr(reply.data, '\n') == nullptr);

    assert(telegram_command_dispatch(&context, "  /CLASSIFICA@ExampleBot  ", &reply) ==
           TELEGRAM_COMMAND_REPLIED);
    assert(strstr(reply.data, "Classifica") != nullptr);
    assert(strstr(reply.data, "palle @TheConquister37") == nullptr);
    assert(telegram_command_dispatch(&context, "/quotes 8", &reply) == TELEGRAM_COMMAND_REPLIED);
    assert(strcmp(reply.data, "Solo il proprietario può vedere le citazioni.") == 0);
    assert(telegram_command_dispatch(&context, "/addquote", &reply) == TELEGRAM_COMMAND_REPLIED);
    assert(strstr(reply.data, "Uso: /addquote") != nullptr);
    assert(strstr(reply.data, "1000 palle.") != nullptr);
    assert(telegram_command_dispatch(&context, "/delquote 1", &reply) ==
           TELEGRAM_COMMAND_REPLIED);
    assert(strstr(reply.data, "Solo il proprietario") != nullptr);
    context.user_id = 99;
    context.username = "owner";
    assert(telegram_command_dispatch(&context, "/delquote 1", &reply) ==
           TELEGRAM_COMMAND_REPLIED);
    assert(strcmp(reply.data, "Citazione non trovata.") == 0);
    assert(telegram_command_dispatch(&context, "/quotes 8", &reply) == TELEGRAM_COMMAND_REPLIED);
    assert(strcmp(reply.data, "Nessuna citazione in collezione.") == 0);

    QuoteAddResult addition;
    assert(quote_add(&storage, "owner", "citazione di prova", 0, &addition));
    assert(addition.status == QUOTE_ADDED);
    context.user_id = 1;
    context.username = "alice";
    assert(telegram_command_dispatch(&context, "We @TheConquister37", &reply) ==
           TELEGRAM_COMMAND_REPLIED);
    assert(strstr(reply.data, "alice sei già in") != nullptr);
    assert(strstr(reply.data, "citazione di prova") == nullptr);
    context.user_id = 2;
    context.username = "bob";
    assert(telegram_command_dispatch(&context, "We @TheConquister37", &reply) ==
           TELEGRAM_COMMAND_REPLIED);
    assert(strstr(reply.data, "bob sei in ") != nullptr);
    assert(strstr(reply.data, "!\n\ncitazione di prova") != nullptr);
    assert(telegram_command_dispatch(&context, "/classifica", &reply) == TELEGRAM_COMMAND_REPLIED);
    assert(strstr(reply.data, "🏆 Classifica @TheConquister37:\n\n1. ") != nullptr);
    assert(strstr(reply.data, " — 📜 1 citazione\n") != nullptr);
    assert(strstr(reply.data, "\n\n🪐 In @TheConquister37 ora: bob") != nullptr);

    arena_free(&arena);
    storage_close(&storage);
    FILE *saved = fopen(config.conquister_path, "rb");
    assert(saved != nullptr);
    int closed = fclose(saved);
    assert(closed == 0);
    test_paths_remove(config.conquister_path, config.quotes_path);
    puts("Telegram command tests: ok");
    return 0;
}
