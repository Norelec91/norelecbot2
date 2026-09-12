#include "test_paths.hpp"

#include "game.hpp"
#include "telegram_commands.hpp"

#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest/doctest.h>

#include <filesystem>
#include <fstream>

using namespace norelecbot;

TEST_CASE("the bot answers the commands it knows and ignores the rest") {
    const TestPaths paths{"telegram-command-test"};
    AppConfig config;
    config.conquister_path = paths.conquister;
    config.quotes_path = paths.quotes;
    config.owner_id = 99;
    config.quote_cost = 1000;

    {
        Storage storage{config.conquister_path, config.quotes_path};
        CommandContext context{.storage = storage, .config = config, .user_id = 1, .username = "alice"};
        const auto ignored = [&](std::string_view text) {
            return !telegram_command_dispatch(context, text).has_value();
        };
        const auto reply = [&](std::string_view text) {
            return telegram_command_dispatch(context, text).value_or("<nessuna risposta>");
        };

        CHECK(ignored("ciao"));
        CHECK(ignored("   "));
        CHECK(reply("/leaderboard") ==
              "Classifica vuota. Scrivi \"We @TheConquister37\" per entrare in @TheConquister37!");
        CHECK(ignored("/classifica"));

        config.conquister_chat_id = -1001234567890;
        context.chat_id = -100999;
        CHECK(ignored("We @TheConquister37"));
        CHECK(reply("/leaderboard").starts_with("Classifica vuota"));
        context.chat_id = -1001234567890;

        context.username = "";
        CHECK(reply("We @TheConquister37").contains("Imposta uno username"));
        context.username = "alice";
        std::string answer = reply("We @TheConquister37");
        CHECK(answer.contains("alice sei in "));
        CHECK_FALSE(answer.contains('\n'));

        answer = reply("  /LEADERBOARD@ExampleBot  ");
        CHECK(answer.contains("Classifica"));
        CHECK_FALSE(answer.contains("palle @TheConquister37"));
        CHECK(reply("/quotes 8") == "Solo il proprietario può vedere le citazioni.");
        answer = reply("/addquote");
        CHECK(answer.contains("Uso: /addquote"));
        CHECK(answer.contains("1000 palle."));
        CHECK(reply("/delquote 1").contains("Solo il proprietario"));

        context.user_id = 99;
        context.username = "owner";
        CHECK(reply("/delquote 1") == "Citazione non trovata.");
        CHECK(reply("/quotes 8") == "Nessuna citazione in collezione.");

        CHECK(quote_add(storage, "owner", "citazione di prova", 0).status == QuoteAddStatus::added);
        CHECK(reply("/quotes").contains("\n1. citazione di prova"));

        context.user_id = 1;
        context.username = "alice";
        answer = reply("We @TheConquister37");
        CHECK(answer.contains("alice sei già in"));
        CHECK_FALSE(answer.contains("citazione di prova"));

        context.user_id = 2;
        context.username = "bob";
        answer = reply("We @TheConquister37");
        CHECK(answer.contains("bob sei in "));
        CHECK(answer.contains("!\n\ncitazione di prova"));
        answer = reply("/leaderboard");
        CHECK(answer.contains("🏆 Classifica @TheConquister37:\n\n1. "));
        CHECK(answer.contains(" — 📜 1 citazione\n"));
        CHECK(answer.contains("\n\n🪐 In @TheConquister37 ora: bob"));

        context.user_id = 99;
        CHECK(reply("/delquote 1") == "Citazione eliminata: citazione di prova");

        {
            std::ofstream broken{config.conquister_path, std::ios::binary};
            broken << "{";
        }
        CHECK(reply("/leaderboard") == "Errore interno: riprova tra poco.");
    }

    CHECK(std::filesystem::exists(config.conquister_path));
}
