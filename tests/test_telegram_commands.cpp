#include "test_paths.hpp"

#include "quote_service.hpp"
#include "telegram_commands.hpp"

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
        std::string reply;
        CommandContext context{.storage = storage, .config = config, .user_id = 1, .username = "alice"};
        const auto dispatch = [&](std::string_view text) {
            return telegram_command_dispatch(context, text, reply);
        };

        assert(dispatch("ciao") == CommandResult::ignored);
        assert(dispatch("   ") == CommandResult::ignored);
        assert(dispatch("/leaderboard") == CommandResult::replied);
        assert(reply == "Classifica vuota. Scrivi \"We @TheConquister37\" per entrare in @TheConquister37!");
        assert(dispatch("/classifica") == CommandResult::ignored);

        config.conquister_chat_id = -1001234567890;
        context.chat_id = -100999;
        assert(dispatch("We @TheConquister37") == CommandResult::ignored);
        assert(reply.empty());
        assert(dispatch("/leaderboard") == CommandResult::replied);
        context.chat_id = -1001234567890;

        context.username = "";
        assert(dispatch("We @TheConquister37") == CommandResult::replied);
        assert(reply.contains("Imposta uno username"));
        context.username = "alice";
        assert(dispatch("We @TheConquister37") == CommandResult::replied);
        assert(reply.contains("alice sei in "));
        assert(!reply.contains('\n'));

        assert(dispatch("  /LEADERBOARD@ExampleBot  ") == CommandResult::replied);
        assert(reply.contains("Classifica"));
        assert(!reply.contains("palle @TheConquister37"));
        assert(dispatch("/quotes 8") == CommandResult::replied);
        assert(reply == "Solo il proprietario può vedere le citazioni.");
        assert(dispatch("/addquote") == CommandResult::replied);
        assert(reply.contains("Uso: /addquote") && reply.contains("1000 palle."));
        assert(dispatch("/delquote 1") == CommandResult::replied);
        assert(reply.contains("Solo il proprietario"));
        context.user_id = 99;
        context.username = "owner";
        assert(dispatch("/delquote 1") == CommandResult::replied);
        assert(reply == "Citazione non trovata.");
        assert(dispatch("/quotes 8") == CommandResult::replied);
        assert(reply == "Nessuna citazione in collezione.");

        assert(quote_add(storage, "owner", "citazione di prova", 0).status == QuoteAddStatus::added);
        assert(dispatch("/quotes") == CommandResult::replied);
        assert(reply.contains("\n1. citazione di prova"));
        context.user_id = 1;
        context.username = "alice";
        assert(dispatch("We @TheConquister37") == CommandResult::replied);
        assert(reply.contains("alice sei già in"));
        assert(!reply.contains("citazione di prova"));
        context.user_id = 2;
        context.username = "bob";
        assert(dispatch("We @TheConquister37") == CommandResult::replied);
        assert(reply.contains("bob sei in "));
        assert(reply.contains("!\n\ncitazione di prova"));
        assert(dispatch("/leaderboard") == CommandResult::replied);
        assert(reply.contains("🏆 Classifica @TheConquister37:\n\n1. "));
        assert(reply.contains(" — 📜 1 citazione\n"));
        assert(reply.contains("\n\n🪐 In @TheConquister37 ora: bob"));
        context.user_id = 99;
        assert(dispatch("/delquote 1") == CommandResult::replied);
        assert(reply == "Citazione eliminata: citazione di prova");
    }

    const bool saved = std::filesystem::exists(config.conquister_path);
    assert(saved);
    std::println("Telegram command tests: ok");
    return 0;
}
