#include "test_paths.hpp"

#include "game.hpp"
#include "telegram_commands.hpp"

#include <fstream>
#include <print>

int main() {
    using namespace norelecbot;

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

        assert(ignored("ciao"));
        assert(ignored("   "));
        assert(reply("/leaderboard") == "Classifica vuota. Scrivi \"We @TheConquister37\" per entrare in @TheConquister37!");
        assert(ignored("/classifica"));

        config.conquister_chat_id = -1001234567890;
        context.chat_id = -100999;
        assert(ignored("We @TheConquister37"));
        assert(reply("/leaderboard").starts_with("Classifica vuota"));
        context.chat_id = -1001234567890;

        context.username = "";
        assert(reply("We @TheConquister37").contains("Imposta uno username"));
        context.username = "alice";
        std::string answer = reply("We @TheConquister37");
        assert(answer.contains("alice sei in ") && !answer.contains('\n'));

        answer = reply("  /LEADERBOARD@ExampleBot  ");
        assert(answer.contains("Classifica") && !answer.contains("palle @TheConquister37"));
        assert(reply("/quotes 8") == "Solo il proprietario può vedere le citazioni.");
        answer = reply("/addquote");
        assert(answer.contains("Uso: /addquote") && answer.contains("1000 palle."));
        assert(reply("/delquote 1").contains("Solo il proprietario"));
        context.user_id = 99;
        context.username = "owner";
        assert(reply("/delquote 1") == "Citazione non trovata.");
        assert(reply("/quotes 8") == "Nessuna citazione in collezione.");

        assert(quote_add(storage, "owner", "citazione di prova", 0).status == QuoteAddStatus::added);
        assert(reply("/quotes").contains("\n1. citazione di prova"));
        context.user_id = 1;
        context.username = "alice";
        answer = reply("We @TheConquister37");
        assert(answer.contains("alice sei già in") && !answer.contains("citazione di prova"));
        context.user_id = 2;
        context.username = "bob";
        answer = reply("We @TheConquister37");
        assert(answer.contains("bob sei in ") && answer.contains("!\n\ncitazione di prova"));
        answer = reply("/leaderboard");
        assert(answer.contains("🏆 Classifica @TheConquister37:\n\n1. "));
        assert(answer.contains(" — 📜 1 citazione\n"));
        assert(answer.contains("\n\n🪐 In @TheConquister37 ora: bob"));
        context.user_id = 99;
        assert(reply("/delquote 1") == "Citazione eliminata: citazione di prova");

        {
            std::ofstream broken{config.conquister_path, std::ios::binary};
            broken << "{";
        }
        assert(reply("/leaderboard") == "Errore interno: riprova tra poco.");
    }

    const bool saved = std::filesystem::exists(config.conquister_path);
    assert(saved);
    std::println("Telegram command tests: ok");
    return 0;
}
