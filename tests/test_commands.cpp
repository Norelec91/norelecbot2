#include "test_paths.hpp"

#include "game.hpp"
#include "commands.hpp"

#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest/doctest.h>

#include <filesystem>
#include <chrono>
#include <fstream>

using namespace norelecbot;

namespace {

std::int64_t seconds_now_for_test() {
    return std::chrono::duration_cast<std::chrono::seconds>(
               std::chrono::system_clock::now().time_since_epoch()
    ).count();
}

}

TEST_CASE("the bot answers the commands it knows and ignores the rest") {
    const TestPaths paths{"command-test"};
    AppConfig config;
    config.conquister_path = paths.conquister;
    config.quotes_path = paths.quotes;
        config.quote_cost = 1000;

    {
        Storage storage{config.conquister_path, config.quotes_path};
        CommandContext context{.storage = storage, .config = config, .user_id = 1, .username = "alice"};
        const auto ignored = [&](std::string_view text) {
            return !command_dispatch(context, text).has_value();
        };
        const auto reply = [&](std::string_view text) {
            return command_dispatch(context, text).value_or("<nessuna risposta>");
        };

        CHECK(ignored("ciao"));
        CHECK(ignored("   "));
        CHECK(reply("/leaderboard") ==
              "Classifica vuota. Scrivi \"We @TheConquister37\" per entrare in @TheConquister37!");
        CHECK(ignored("/classifica"));

        context.claims_allowed = false;
        CHECK(ignored("We @TheConquister37"));
        CHECK(reply("/leaderboard").starts_with("Classifica vuota"));
        context.claims_allowed = true;

        context.username = "";
        CHECK(reply("We @TheConquister37").contains("Imposta uno username"));
        context.username = "alice";
        std::string answer = reply("We @TheConquister37");
        CHECK(answer.starts_with("🪐 alice sei in @TheConquister37!"));
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
        context.owner = true;
        CHECK(reply("/delquote 1") == "Citazione non trovata.");
        CHECK(reply("/quotes 8") == "Nessuna citazione in collezione.");

        CHECK(quote_add(storage, "owner", "citazione di prova", 0).status == QuoteAddStatus::added);
        CHECK(reply("/quotes").contains("\n1) citazione di prova"));

        context.user_id = 1;
        context.username = "alice";
        context.owner = false;
        answer = reply("We @TheConquister37");
        CHECK(answer.contains("alice sei già in"));
        CHECK_FALSE(answer.contains("citazione di prova"));

        context.user_id = 2;
        context.username = "bob";
        answer = reply("We @TheConquister37");
        CHECK(answer.contains("bob sei in "));
        CHECK(answer.contains("!\n\ncitazione di prova"));
        answer = reply("/leaderboard");
        CHECK(answer.contains("🏆 Classifica @TheConquister37\nOggi è giorno di "));
        CHECK(answer.contains("\n\n1) "));
        CHECK(answer.contains(" — 📜 1 citazione\n"));
        CHECK(answer.contains("\n\n🪐 In @TheConquister37 ora: bob"));

        context.owner = true;
        CHECK(reply("/delquote 1") == "Citazione eliminata: citazione di prova");

        config.balloon_cost = 0;
        context.owner = false;
        context.user_id = 4;
        context.username = "dave";
        CHECK(reply("/buyballoon") ==
              "🎈 dave hai comprato un palloncino spendendo 0 palle! Ora puoi difendere la tua posizione in @TheConquister37.");
        CHECK(reply("/buyballoon") == "dave hai già un palloncino.");
        config.balloon_cost = 1000;
        context.user_id = 5;
        context.username = "erin";
        CHECK(reply("/buyballoon") == "erin ti servono 1000 palle per un palloncino (ne hai 0).");

        config.boost_cost = 0;
        context.owner = false;
        context.user_id = 6;
        context.username = "frank";
        CHECK(reply("/buyboost") ==
              "⚡ frank hai comprato un boost spendendo 0 palle! Il tuo prossimo possesso di "
              "@TheConquister37 vale x3, fino a quando ti spodestano.");
        CHECK(reply("/buyboost") == "frank hai già un boost x3 pronto.");
        CHECK(reply("/buyballoon") == "frank hai un boost attivo: il palloncino puoi comprarlo dopo.");
        config.boost_cost = 1500;
        context.user_id = 7;
        context.username = "grace";
        CHECK(reply("/buyboost") == "grace ti servono 1500 palle per un boost (ne hai 0).");

        config.quote_cost = 0;
        context.user_id = 3;
        context.username = "carol";
        CHECK(reply("/addquote nuova citazione") ==
              "carol hai aggiunto la citazione spendendo 0 palle!\n\nnuova citazione");
        config.quote_cost = 1000;

        {
            std::ofstream broken{config.conquister_path, std::ios::binary};
            broken << "{";
        }
        CHECK(reply("/leaderboard") == "Errore interno: riprova tra poco.");
    }

    CHECK(std::filesystem::exists(config.conquister_path));
}

TEST_CASE("the balloon replies are the ones the players read") {
    const TestPaths paths{"balloon-reply-test"};
    const std::int64_t now = std::chrono::duration_cast<std::chrono::seconds>(
                                 std::chrono::system_clock::now().time_since_epoch()
                             ).count();
    {
        std::ofstream file{paths.conquister, std::ios::binary};
        file << R"({"current":{"user_id":1,"username":"alice","since":0},"scores":{},"quotes_added":{},)"
             << R"("balloons":{"alice":3},"cooldowns":{"erin":)" << now + 290 << "}}";
    }
    AppConfig config;
    config.conquister_path = paths.conquister;
    config.quotes_path = paths.quotes;

    Storage storage{config.conquister_path, config.quotes_path};
    CommandContext context{.storage = storage, .config = config, .user_id = 5, .username = "erin"};
    const auto reply = [&](std::string_view text) {
        return command_dispatch(context, text).value_or("<nessuna risposta>");
    };

    const std::string waiting = reply("We @TheConquister37");
    CHECK(waiting.starts_with("⏳ erin hai ancora "));
    CHECK(waiting.contains(" minut"));
    CHECK(waiting.ends_with(" di penalità."));

    context.user_id = 2;
    context.username = "bob";
    const std::string popped = reply("We @TheConquister37");
    CHECK(popped.starts_with("💥 bob hai bucato il palloncino di @alice!\n"));
    CHECK(popped.contains("bob hai cacciato @alice da @TheConquister37.\n"));
    CHECK(popped.contains("bob sei in @TheConquister37!"));
}

TEST_CASE("a message can be recognised as a command without running it") {
    CHECK(command_is_for_bot("We @TheConquister37"));
    CHECK(command_is_for_bot("  We @TheConquister37  "));
    CHECK(command_is_for_bot("/leaderboard"));
    CHECK(command_is_for_bot("/LEADERBOARD@ExampleBot"));
    CHECK(command_is_for_bot("/addquote una citazione"));
    CHECK_FALSE(command_is_for_bot("we @theconquister37"));
    CHECK_FALSE(command_is_for_bot("ciao"));
    CHECK_FALSE(command_is_for_bot(""));
    CHECK_FALSE(command_is_for_bot("/classifica"));
}

TEST_CASE("a name that came from IRC is never written as a mention") {
    const TestPaths paths{"mention-test"};
    AppConfig config;
    config.conquister_path = paths.conquister;
    config.quotes_path = paths.quotes;
    Storage storage{config.conquister_path, config.quotes_path};

    const auto play = [&](std::int64_t user_id, std::string_view username) {
        const CommandContext context{
            .storage = storage,
            .config = config,
            .user_id = user_id,
            .username = username,
        };
        return command_dispatch(context, "We @TheConquister37").value_or("<nessuna risposta>");
    };

    /* alice plays from IRC, where there are no Telegram ids. */
    static_cast<void>(play(0, "alice"));
    std::string reply = play(2, "bob");
    CHECK(reply.contains("bob hai cacciato alice da @TheConquister37.\n"));
    CHECK_FALSE(reply.contains("@alice"));
    /* The place keeps its name. */
    CHECK(reply.contains("🪐 bob sei in @TheConquister37!"));

    /* bob played from Telegram, so his name is a mention that reaches him. */
    reply = play(0, "carol");
    CHECK(reply.contains("carol hai cacciato @bob da @TheConquister37.\n"));
}

TEST_CASE("the balloon replies follow the same rule") {
    const TestPaths paths{"mention-balloon-test"};
    {
        std::ofstream file{paths.conquister, std::ios::binary};
        file << R"({"current":{"user_id":0,"username":"alice","since":0},"scores":{},"quotes_added":{},)"
             << R"("balloons":{"alice":3},"cooldowns":{}})";
    }
    AppConfig config;
    config.conquister_path = paths.conquister;
    config.quotes_path = paths.quotes;
    Storage storage{config.conquister_path, config.quotes_path};

    const CommandContext context{.storage = storage, .config = config, .user_id = 2, .username = "bob"};
    const std::string reply = command_dispatch(context, "We @TheConquister37").value_or("<nessuna risposta>");
    CHECK(reply.starts_with("💥 bob hai bucato il palloncino di alice!\n"));
    CHECK_FALSE(reply.contains("@alice"));
}

TEST_CASE("We @someone sends the player out to rob them") {
    const TestPaths paths{"raid-command-test"};
    {
        std::ofstream file{paths.conquister, std::ios::binary};
        file << R"({"current":null,"scores":{"alice":1000},"quotes_added":{}})";
    }
    AppConfig config;
    config.conquister_path = paths.conquister;
    config.quotes_path = paths.quotes;
    config.travel_divisor = 1000000;
    Storage storage{config.conquister_path, config.quotes_path};

    CommandContext context{.storage = storage, .config = config, .user_id = 2, .username = "bob"};
    const auto reply = [&](std::string_view text) {
        return command_dispatch(context, text).value_or("<nessuna risposta>");
    };

    CHECK(reply("We @alice") == "🚀 bob parti per alice: arrivi tra 5 secondi. bob resta scoperto.");
    CHECK(reply("We @alice") == "🚀 bob sei già in viaggio, torni tra 10 secondi.");
    /* Another player, who is at home and can therefore get an answer of his own. */
    context.username = "carol";
    context.user_id = 3;
    CHECK(reply("We @carol") == "🪐 carol sei già in @carol!");
    CHECK(reply("We @CAROL") == "🪐 carol sei già in @carol!");
    CHECK(reply("We @nessuno") == "🚀 carol non conosco nessun giocatore di nome nessuno.");
    context.username = "bob";
    context.user_id = 2;

    /* The place is still its own move, and a message that names nobody is not one. */
    CHECK(reply("We @TheConquister37").contains("@TheConquister37"));
    CHECK_FALSE(command_dispatch(context, "We @").has_value());
    CHECK_FALSE(command_dispatch(context, "We @ alice").has_value());
    CHECK_FALSE(command_dispatch(context, "We @alice ora").has_value());
    CHECK_FALSE(command_dispatch(context, "we @alice").has_value());

    /* On the road, naming yourself turns you round. */
    CHECK(reply("We @bob") == "🚀 bob lasci perdere e torni in @bob: arrivi tra 10 secondi.");
    CHECK(reply("We @alice") == "🚀 bob sei già in viaggio, torni tra 10 secondi.");

    CHECK(reply("We @TheConquister37") ==
          "🚀 bob sei per strada: non puoi entrare in @TheConquister37 prima di tornare in @bob, tra 10 secondi.");

    /* The one holding the place stays in it. */
    static_cast<void>(conquister_claim(storage, 9, "erin", seconds_now_for_test()));
    context.username = "erin";
    context.user_id = 9;
    CHECK(reply("We @alice") == "🚀 erin sei in @TheConquister37 e da lì non si parte.");
    context.username = "bob";
    context.user_id = 2;

    CHECK(command_is_for_bot("We @alice"));
    CHECK_FALSE(command_is_for_bot("We @"));

    context.claims_allowed = false;
    CHECK_FALSE(command_dispatch(context, "We @alice").has_value());
}

TEST_CASE("the raids tell what happened") {
    const zodiac::Overrides none;
    RaidEvent event;
    event.kind = RaidEvent::Kind::stolen;
    event.raider = "bob";
    event.target = "alice";
    event.loot = 250;
    event.seconds = 52;
    CHECK(raid_event_reply(event, none) == "💰 bob hai rubato 250 palle a alice! Torni in bob tra 52 secondi.");

    event.target_on_telegram = true;
    event.undefended = true;
    CHECK(raid_event_reply(event, none) ==
          "💰 bob hai rubato 250 palle a @alice, che era in giro! Torni in bob tra 52 secondi.");

    event.undefended = false;
    event.balloon_popped = true;
    event.raider_percent = 125;
    event.target_percent = 75;
    CHECK(raid_event_reply(event, none).contains("bucandogli il palloncino ("));
    CHECK(raid_event_reply(event, none).contains(": 125/75)"));

    RaidEvent defended{
        .kind = RaidEvent::Kind::defended,
        .raider = "bob",
        .target = "alice",
        .loot = 0,
        .cost = 100,
        .raider_emoji = {},
        .target_emoji = {},
        .seconds = 52,
    };
    CHECK(raid_event_reply(defended, none) ==
          "🎈 bob il palloncino di alice ha resistito e ti costa 100 palle. Torni in bob a mani vuote tra 52 secondi.");

    /* Names of people stay bare; the mention belongs to the planet he is heading back to. */
    defended.raider_on_telegram = true;
    defended.target_on_telegram = true;
    CHECK(raid_event_reply(defended, none) ==
          "🎈 bob il palloncino di alice ha resistito e ti costa 100 palle. "
          "Torni in @bob a mani vuote tra 52 secondi.");

    RaidEvent home;
    home.kind = RaidEvent::Kind::returned;
    home.raider = "bob";
    home.target = "alice";
    home.loot = 250;
    CHECK(raid_event_reply(home, none) == "🪐 bob sei tornato in bob con 250 palle.");
    home.raider_on_telegram = true;
    CHECK(raid_event_reply(home, none) == "🪐 bob sei tornato in @bob con 250 palle.");
    home.raider_on_telegram = false;
    home.loot = 0;
    CHECK(raid_event_reply(home, none) == "🪐 bob sei tornato in bob a mani vuote.");
}

TEST_CASE("a quote about what the owner has banned is turned away") {
    const TestPaths paths{"banned-quote-test"};
    AppConfig config;
    config.conquister_path = paths.conquister;
    config.quotes_path = paths.quotes;
    config.quote_cost = 0;
    config.quote_banned = {"frod", "scommess"};
    Storage storage{config.conquister_path, config.quotes_path};

    const CommandContext context{.storage = storage, .config = config, .user_id = 1, .username = "alice"};
    const auto reply = [&](std::string_view text) {
        return command_dispatch(context, text).value_or("<nessuna risposta>");
    };

    const std::string refused = "alice questa citazione non si può aggiungere: nessun addebito.";
    CHECK(reply("/addquote LA FRODE LA FRODE LA FRODE") == refused);
    CHECK(reply("/addquote parliamo di frodi") == refused);
    CHECK(reply("/addquote chi frodava allora") == refused);
    CHECK(reply("/addquote una bella scommessa") == refused);
    /* Nothing was written down. */
    CHECK(quote_page_load(storage, 1).total == 0);

    CHECK(reply("/addquote una citazione qualunque").starts_with("alice hai aggiunto la citazione"));
    CHECK(quote_page_load(storage, 1).total == 1);
}


TEST_CASE("a bought emoji follows the name everywhere") {
    const TestPaths paths{"dressed-test"};
    {
        std::ofstream file{paths.conquister, std::ios::binary};
        file << R"({"current":null,"scores":{"alice":30000,"bob":500},"quotes_added":{}})";
    }
    AppConfig config;
    config.conquister_path = paths.conquister;
    config.quotes_path = paths.quotes;
    Storage storage{config.conquister_path, config.quotes_path};

    const CommandContext context{
        .storage = storage,
        .config = config,
        .user_id = 1,
        .username = "alice",
        .claims_allowed = true,
        .owner = false,
    };

    /* Bought, and the reply already shows the name dressed. */
    const std::optional<std::string> bought = command_dispatch(context, "/buyfurniture 🎈🍕");
    REQUIRE(bought);
    CHECK(bought->contains("alice (🎈🍕)"));
    CHECK(bought->contains("2 su 10"));

    /* Anything that is not an emoji is turned away without a charge. */
    const std::int64_t before = conquister_user(storage, "alice")->score;
    const std::optional<std::string> refused = command_dispatch(context, "/buyfurniture ciao");
    REQUIRE(refused);
    CHECK(refused->contains("solo emoji"));
    CHECK(conquister_user(storage, "alice")->score == before);

    /* The leaderboard shows the dressed name, and whoever bought nothing stays bare. */
    const std::optional<std::string> board = command_dispatch(context, "/leaderboard");
    REQUIRE(board);
    CHECK(board->contains("alice (🎈🍕)"));
    CHECK(board->contains("bob —"));

    /* And taking the seat, too. */
    const std::optional<std::string> claimed = command_dispatch(context, conquister_trigger);
    REQUIRE(claimed);
    CHECK(claimed->contains("alice (🎈🍕) sei in"));
}

TEST_CASE("the owner turns the prices off for himself, not for everyone") {
    const TestPaths paths{"debug-test"};
    {
        std::ofstream file{paths.conquister, std::ios::binary};
        file << R"({"current":null,"scores":{"alice":500,"norelec":500},"quotes_added":{}})";
    }
    AppConfig config;
    config.conquister_path = paths.conquister;
    config.quotes_path = paths.quotes;
    config.balloon_cost = 1000;
    Storage storage{config.conquister_path, config.quotes_path};

    const CommandContext player{
        .storage = storage,
        .config = config,
        .user_id = 1,
        .username = "alice",
        .claims_allowed = true,
        .owner = false,
    };
    const CommandContext owner{
        .storage = storage,
        .config = config,
        .user_id = 2,
        .username = "norelec",
        .claims_allowed = true,
        .owner = true,
    };

    /* Anyone who is not an owner cannot even turn it on. */
    const std::optional<std::string> refused = command_dispatch(player, "/debug 1");
    REQUIRE(refused);
    CHECK(refused->contains("Solo il proprietario"));
    CHECK_FALSE(debug_on(storage, "alice"));

    /* Five hundred palle do not buy a balloon that costs a thousand. */
    const std::optional<std::string> broke = command_dispatch(player, "/buyballoon");
    REQUIRE(broke);
    CHECK(broke->contains("ti servono"));

    /* The owner turns it on for himself: he buys for nothing. */
    const std::optional<std::string> on = command_dispatch(owner, "/debug 1");
    REQUIRE(on);
    CHECK(on->contains("per te"));
    CHECK(debug_on(storage, "norelec"));
    const std::optional<std::string> bought = command_dispatch(owner, "/buyballoon");
    REQUIRE(bought);
    CHECK(bought->contains("palloncino"));
    CHECK(conquister_user(storage, "norelec")->score == 500);

    /* Everybody else goes on paying as before. */
    CHECK_FALSE(debug_on(storage, "alice"));
    const std::optional<std::string> still_broke = command_dispatch(player, "/buyballoon");
    REQUIRE(still_broke);
    CHECK(still_broke->contains("ti servono"));
    CHECK(conquister_user(storage, "alice")->score == 500);

    /* Switched off, he pays again like the rest. */
    const std::optional<std::string> off = command_dispatch(owner, "/debug 0");
    REQUIRE(off);
    CHECK(off->contains("tornano a costare"));
    CHECK_FALSE(debug_on(storage, "norelec"));

    /* With no argument it only says how things stand for whoever asked. */
    const std::optional<std::string> asked = command_dispatch(owner, "/debug");
    REQUIRE(asked);
    CHECK(asked->contains("spento"));
}

TEST_CASE("prices follow how rich the group has become") {
    const TestPaths paths{"prices-test"};
    {
        std::ofstream file{paths.conquister, std::ios::binary};
        /* Five players: the middle one holds ten thousand. */
        file << R"({"current":null,"scores":{"a":100,"b":5000,"c":10000,"d":40000,"e":900000},)"
                R"("quotes_added":{}})";
    }
    AppConfig config;
    config.conquister_path = paths.conquister;
    config.quotes_path = paths.quotes;
    config.balloon_cost = 1000;
    config.price_percent = 20;
    config.price_ceiling = 50;
    Storage storage{config.conquister_path, config.quotes_path};

    const CommandContext context{
        .storage = storage,
        .config = config,
        .user_id = 1,
        .username = "d",
        .claims_allowed = true,
        .owner = true,
    };

    const Wealth wealth = wealth_now(storage);
    CHECK(wealth.players == 5);
    CHECK(wealth.middle == 10000);
    CHECK(wealth.total == 955100);

    /* The median is ten thousand, so a balloon costs a fifth of it, not the list price. */
    REQUIRE(command_dispatch(context, "/buyballoon"));
    CHECK(conquister_user(storage, "d")->score == 38000);

    /* With the debug switch on, whoever threw it pays nothing. */
    REQUIRE(command_dispatch(context, "/debug 1"));
    REQUIRE(command_dispatch(context, "/addquote una citazione qualunque"));
    CHECK(conquister_user(storage, "d")->score == 38000);
    REQUIRE(command_dispatch(context, "/debug 0"));
}

TEST_CASE("a poor group pays the list price, and a rich one stops at the ceiling") {
    const TestPaths paths{"prices-edges-test"};
    {
        std::ofstream file{paths.conquister, std::ios::binary};
        file << R"({"current":null,"scores":{"a":2000,"b":10,"c":30},"quotes_added":{}})";
    }
    AppConfig config;
    config.conquister_path = paths.conquister;
    config.quotes_path = paths.quotes;
    config.balloon_cost = 1000;
    config.price_percent = 20;
    config.price_ceiling = 50;
    Storage storage{config.conquister_path, config.quotes_path};

    const CommandContext context{
        .storage = storage,
        .config = config,
        .user_id = 1,
        .username = "a",
        .claims_allowed = true,
        .owner = false,
    };
    /* A group with nothing pays the list price. */
    REQUIRE(command_dispatch(context, "/buyballoon"));
    CHECK(conquister_user(storage, "a")->score == 1000);

    /* A group swimming in palle stops at the ceiling, fifty times the list price. */
    {
        std::ofstream file{paths.conquister, std::ios::binary};
        file << R"({"current":null,"scores":{"a":90000000,"b":90000000,"c":90000000},"quotes_added":{}})";
    }
    Storage rich{paths.conquister, paths.quotes};
    const CommandContext loaded{
        .storage = rich,
        .config = config,
        .user_id = 1,
        .username = "a",
        .claims_allowed = true,
        .owner = false,
    };
    REQUIRE(command_dispatch(loaded, "/buyballoon"));
    CHECK(conquister_user(rich, "a")->score == 90000000 - 50000);
}
