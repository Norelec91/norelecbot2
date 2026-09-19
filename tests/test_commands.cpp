#include "test_paths.hpp"

#include "game.hpp"
#include "commands.hpp"
#include "game.hpp"
#include "virus.hpp"

#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest/doctest.h>

#include <filesystem>
#include <chrono>
#include <fstream>
#include <map>

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
        CommandContext context{.storage = storage, .config = config, .user_id = 1, .username = "alice", .whisper = {}};
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

        CHECK(quote_add(storage, "owner", "citazione di prova", 0, 0).status == QuoteAddStatus::added);
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
    CommandContext context{.storage = storage, .config = config, .user_id = 5, .username = "erin", .whisper = {}};
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
            .whisper = {},
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

    const CommandContext context{.storage = storage, .config = config, .user_id = 2, .username = "bob", .whisper = {}};
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

    CommandContext context{.storage = storage, .config = config, .user_id = 2, .username = "bob", .whisper = {}};
    const auto reply = [&](std::string_view text) {
        return command_dispatch(context, text).value_or("<nessuna risposta>");
    };

    CHECK(reply("We @alice") == "🚚 bob ritira di persona da alice: è in zona, ci arriva in 5 secondi. bob resta scoperto.");
    CHECK(reply("We @alice") == "🚀 bob sei già in viaggio, torni tra 10 secondi.");
    /* Another player, who is at home and can therefore get an answer of his own. */
    context.username = "carol";
    context.user_id = 3;
    CHECK(reply("We @carol") == "🪐 carol sei già in @carol!");
    CHECK(reply("We @CAROL") == "🪐 carol sei già in @carol!");
    CHECK(reply("We @nessuno") == "🚀 carol non conosco nessun giocatore di nome nessuno.");
    /* Without the @ a name nobody plays under is just talk: the bot keeps quiet. */
    CHECK_FALSE(command_dispatch(context, "We nessuno").has_value());
    CHECK_FALSE(command_dispatch(context, "We ragazzi").has_value());
    context.username = "bob";
    context.user_id = 2;

    /* The place is still its own move, and a message that names nobody is not one. */
    CHECK(reply("We @TheConquister37").contains("@TheConquister37"));
    CHECK_FALSE(command_dispatch(context, "We @").has_value());
    CHECK_FALSE(command_dispatch(context, "We @ alice").has_value());
    CHECK_FALSE(command_dispatch(context, "We @alice ora").has_value());
    CHECK_FALSE(command_dispatch(context, "we @alice").has_value());
    /* A nick with no @ in front, the way it is written on IRC. */
    context.username = "dave";
    context.user_id = 5;
    CHECK(reply("We alice") == "🚚 dave ritira di persona da alice: è in zona, ci arriva in 5 secondi. dave resta scoperto.");
    context.username = "bob";
    context.user_id = 2;

    /* On the road, naming yourself turns you round. */
    /* The seconds left depend on the clock, so only the wording is pinned here. */
    CHECK(reply("We @bob").starts_with("🚀 bob lasci perdere e torni in @bob: arrivi tra "));
    CHECK(reply("We @alice").starts_with("🚀 bob sei già in viaggio, torni tra "));

    CHECK(reply("We @TheConquister37")
              .starts_with("🚀 bob sei per strada: non puoi entrare in @TheConquister37 prima di tornare in @bob, tra "));

    /* The one holding the place stays in it. */
    static_cast<void>(conquister_claim(storage, 9, "erin", seconds_now_for_test()));
    context.username = "erin";
    context.user_id = 9;
    CHECK(reply("We @alice") == "🚀 erin sei in @TheConquister37 e da lì non si parte.");
    context.username = "bob";
    context.user_id = 2;

    CHECK(command_is_for_bot("We @alice"));
    CHECK(command_is_for_bot("We alice"));
    CHECK_FALSE(command_is_for_bot("We @"));
    CHECK_FALSE(command_is_for_bot("We due parole"));

    context.claims_allowed = false;
    CHECK_FALSE(command_dispatch(context, "We @alice").has_value());
}

TEST_CASE("the raids tell what happened") {
    const zodiac::Overrides none;
    RaidEvent event{.kind = RaidEvent::Kind::stolen, .raider = "bob", .target = "alice"};
    event.loot = 250;
    event.seconds = 52;
    CHECK(raid_event_reply(event, none) == "💰 bob hai rubato 250 palle a alice! Consegna prevista in bob tra 52 secondi.");

    event.target_on_telegram = true;
    event.undefended = true;
    CHECK(raid_event_reply(event, none) ==
          "💰 bob hai rubato 250 palle a @alice, che era in giro! Consegna prevista in bob tra 52 secondi.");

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

    RaidEvent home{.kind = RaidEvent::Kind::returned, .raider = "bob", .target = "alice"};
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

    const CommandContext context{.storage = storage, .config = config, .user_id = 1, .username = "alice", .whisper = {}};
    const auto reply = [&](std::string_view text) {
        return command_dispatch(context, text).value_or("<nessuna risposta>");
    };

    const std::string refused = "alice questa citazione non si può aggiungere: nessun addebito.";
    CHECK(reply("/addquote LA FRODE LA FRODE LA FRODE") == refused);
    CHECK(reply("/addquote parliamo di frodi") == refused);
    CHECK(reply("/addquote chi frodava allora") == refused);
    /* Spelled out to slip past the filter. */
    CHECK(reply("/addquote F.R.O.D.E. F.R.O.D.E.") == refused);
    CHECK(reply("/addquote L A   F R O D E") == refused);
    CHECK(reply("/addquote f-r-o-d-e") == refused);
    CHECK(reply("/addquote una bella scommessa") == refused);
    /* Nothing was written down. */
    CHECK(quote_page_load(storage, 1).total == 0);

    CHECK(reply("/addquote una citazione qualunque").starts_with("alice hai aggiunto la citazione"));
    CHECK(quote_page_load(storage, 1).total == 1);
}

TEST_CASE("a shadowed player is answered for and his quote is dropped") {
    const TestPaths paths{"shadow-quote-test"};
    {
        std::ofstream file{paths.conquister, std::ios::binary};
        file << R"({"current":null,"scores":{"giangiui":3000,"alice":3000},"quotes_added":{}})";
    }
    AppConfig config;
    config.conquister_path = paths.conquister;
    config.quotes_path = paths.quotes;
    config.quote_cost = 1000;
    config.quote_banned = {"frod"};
    config.shadowed = {"Giangiui"};
    Storage storage{config.conquister_path, config.quotes_path};

    CommandContext context{.storage = storage, .config = config, .user_id = 1, .username = "giangiui", .whisper = {}};
    const auto reply = [&](std::string_view text) {
        return command_dispatch(context, text).value_or("<nessuna risposta>");
    };

    CHECK(reply("/addquote una citazione") ==
          "giangiui hai aggiunto la citazione spendendo 1000 palle!\n\nuna citazione");
    /* Even the same one twice, and even what everyone else is refused. */
    CHECK(reply("/addquote una citazione").starts_with("giangiui hai aggiunto la citazione"));
    CHECK(reply("/addquote LA FRODE").starts_with("giangiui hai aggiunto la citazione"));
    CHECK(quote_page_load(storage, 1).total == 0);
    /* The palle are spent all the same, and the count does not move. */
    CHECK(conquister_user(storage, "giangiui")->score == 0);
    CHECK(conquister_user(storage, "giangiui")->quotes_added == 0);
    CHECK(reply("/addquote un'altra") == "giangiui ti servono 1000 palle per aggiungere una citazione (ne hai 0).");

    /* Everyone else is treated as before. */
    context.username = "alice";
    CHECK(reply("/addquote la citazione di alice").starts_with("alice hai aggiunto la citazione"));
    CHECK(reply("/addquote LA FRODE") == "alice questa citazione non si può aggiungere: nessun addebito.");
    CHECK(quote_page_load(storage, 1).total == 1);
}

TEST_CASE("the zimbelli are the ones below zero") {
    const TestPaths paths{"zimbelli-test"};
    {
        std::ofstream file{paths.conquister, std::ios::binary};
        file << R"({"current":null,"scores":{"alice":500,"bob":-170,"carol":-70,"dave":0},)"
             << R"("quotes_added":{}})";
    }
    AppConfig config;
    config.conquister_path = paths.conquister;
    config.quotes_path = paths.quotes;
    Storage storage{config.conquister_path, config.quotes_path};

    const CommandContext context{.storage = storage, .config = config, .user_id = 1, .username = "alice", .whisper = {}};
    const std::string reply = command_dispatch(context, "/zimbelli").value_or("<nessuna risposta>");
    CHECK(reply.starts_with("🤡 Zimbelli\n"));
    CHECK(reply.contains("\n1) "));
    CHECK(reply.contains("bob — 170 palle sotto zero"));
    CHECK(reply.contains("carol — 70 palle sotto zero"));
    /* Deepest first, and nobody who is not in the red. */
    CHECK(reply.find("bob") < reply.find("carol"));
    CHECK_FALSE(reply.contains("alice"));
    CHECK_FALSE(reply.contains("dave"));

    {
        std::ofstream file{paths.conquister, std::ios::binary};
        file << R"({"current":null,"scores":{"alice":500},"quotes_added":{}})";
    }
    Storage rich{config.conquister_path, config.quotes_path};
    const CommandContext theirs{.storage = rich, .config = config, .user_id = 1, .username = "alice", .whisper = {}};
    CHECK(command_dispatch(theirs, "/zimbelli") == "Nessuno zimbello: nessuno è sotto zero in @TheConquister37.");
}

TEST_CASE("the profile card says what a player has") {
    const TestPaths paths{"profile-test"};
    {
        std::ofstream file{paths.conquister, std::ios::binary};
        file << R"({"current":{"user_id":1,"username":"alice","since":100},"scores":{"alice":500,"bob":900},)"
             << R"("quotes_added":{"alice":2},"balloons":{"alice":1},"cooldowns":{},"shields":{},)"
             << R"("boosts":{"alice":3}})";
    }
    AppConfig config;
    config.conquister_path = paths.conquister;
    config.quotes_path = paths.quotes;
    config.travel_divisor = 1000000;
    Storage storage{config.conquister_path, config.quotes_path};

    CommandContext context{.storage = storage, .config = config, .user_id = 1, .username = "alice", .whisper = {}};
    const auto reply = [&](std::string_view text) {
        return command_dispatch(context, text).value_or("<nessuna risposta>");
    };

    const std::string mine = reply("/profilo");
    CHECK(mine.starts_with("🪐 alice "));
    CHECK(mine.contains("\n💰 500 palle — 2° posto — 📜 2"));
    CHECK(mine.contains("\n🔮 oggi è giorno di "));
    CHECK(mine.contains("\n🪐 in @TheConquister37 da "));
    CHECK(mine.contains("\n🎈 palloncino: ha respinto 1 attacchi, il prossimo lo buca al 50%"));
    CHECK(mine.contains("\n⚡ boost x3 pronto"));

    /* Somebody else's card says where he is, not what he is hiding. */
    const std::string theirs = reply("/profilo @bob");
    CHECK(theirs.starts_with("🪐 bob "));
    CHECK(theirs.contains("\n💰 900 palle — 1° posto"));
    CHECK(theirs.contains("\n🗺️ dista "));
    CHECK_FALSE(theirs.contains("palloncino"));
    CHECK_FALSE(theirs.contains("boost"));

    CHECK(reply("/profilo bob").starts_with("🪐 bob "));
    CHECK(reply("/profilo nessuno") == "alice non conosco nessun giocatore di nome nessuno.");
}

TEST_CASE("the virus is a game of who is what") {
    const TestPaths paths{"virus-test"};
    {
        std::ofstream file{paths.conquister, std::ios::binary};
        file << R"({"current":null,"scores":{"alice":1000,"bob":1000,"carol":1000,"dave":1000},)"
             << R"("quotes_added":{}})";
    }
    AppConfig config;
    config.conquister_path = paths.conquister;
    config.quotes_path = paths.quotes;
    config.virus_cooldown_seconds = 0;
    Storage storage{config.conquister_path, config.quotes_path};

    std::map<std::string, std::string> whispered;
    const Whisper whisper = [&whispered](std::string_view name, std::string_view said) {
        whispered[std::string{name}] = std::string{said};
    };
    CommandContext context{
        .storage = storage,
        .config = config,
        .user_id = 1,
        .username = "alice",
        .claims_allowed = true,
        .owner = true,
        .whisper = whisper,
    };
    const auto reply = [&](std::string_view text) {
        return command_dispatch(context, text).value_or("<nessuna risposta>");
    };

    CHECK(reply("/virus") == "🦠 Nessuna epidemia in corso.");
    const std::string started = reply("/virus start");
    CHECK(started.starts_with("🦠 IL VIRUS DORONZO STA COLPENDO TUTTI I GIOCATORI!"));
    CHECK(started.contains("4 in gioco"));
    /* Each of them has been told in private what he is, and nobody else can read it. */
    CHECK(whispered.size() == 4);
    CHECK_FALSE(started.contains("DORONZO. Infetti"));

    const auto doronzo_of = [&](const std::string &name) {
        return whispered.at(name).starts_with("🦠");
    };
    std::string doronzo;
    std::vector<std::string> healthy;
    for (const auto &[name, said] : whispered) {
        (doronzo_of(name) ? doronzo : healthy.emplace_back()) = name;
    }
    REQUIRE_FALSE(doronzo.empty());
    REQUIRE(healthy.size() == 3);

    /* A move nobody may make is refused in private, so the channel learns nothing. */
    context.username = healthy[0];
    whispered.clear();
    CHECK_FALSE(command_dispatch(context, "/infetta " + healthy[1]).has_value());
    CHECK(whispered.at(healthy[0]) == "🦠 Non è una cosa che puoi fare tu.");

    /* Shooting says out loud what the one shot turned out to be. */
    const std::string shot = reply("/spara " + doronzo);
    CHECK(shot.starts_with("🔫 " + healthy[0] + " ha sparato a " + doronzo + ", che era un doronzo"));
    CHECK(shot.contains("500 palle"));
    CHECK(shot.contains("🏁 Non è rimasto nessun doronzo: VINCONO I SANI."));

    const VirusReport over = virus_report(storage);
    CHECK_FALSE(over.running);
    CHECK(over.dead == 1);
}

TEST_CASE("the word the owner picked multiplies whoever says it") {
    const TestPaths paths{"magic-test"};
    {
        std::ofstream file{paths.conquister, std::ios::binary};
        file << R"({"current":null,"scores":{"alice":1000},"quotes_added":{}})";
    }
    AppConfig config;
    config.conquister_path = paths.conquister;
    config.quotes_path = paths.quotes;
    config.magic_words = {"la"};
    config.magic_most = 5;
    Storage storage{config.conquister_path, config.quotes_path};

    CommandContext context{
        .storage = storage,
        .config = config,
        .user_id = 1,
        .username = "alice",
        .claims_allowed = true,
        .owner = false,
        .whisper = {},
    };
    const auto said = [&](std::string_view text) { return command_dispatch(context, text); };

    const std::optional<std::string> reply = said("ho visto la luna");
    REQUIRE(reply);
    CHECK(reply->starts_with("🥤 alice ha detto \"la\" · MOUNTAIN DEW: le palle si moltiplicano per "));
    const std::int64_t now = conquister_user(storage, "alice")->score;
    CHECK(now >= 2000);
    CHECK(now <= 5000);
    CHECK(now % 1000 == 0);

    /* Inside another word it does not count, and a command stays a command. */
    CHECK_FALSE(said("parlare di scale").has_value());
    CHECK(said("/leaderboard la").value_or("").contains("Classifica"));
    CHECK(conquister_user(storage, "alice")->score == now);

    /* Somebody with nothing has nothing to multiply. */
    context.username = "bob";
    CHECK_FALSE(said("la la la").has_value());

    /* Off unless the owner sets it. */
    config.magic_words.clear();
    context.username = "alice";
    CHECK_FALSE(said("ho visto la luna").has_value());
}

TEST_CASE("Kio takes people over, and the paperwork to free them can fail") {
    const TestPaths paths{"reprogram-test"};
    {
        std::ofstream file{paths.conquister, std::ios::binary};
        file << R"({"current":null,"scores":{"alice":1000,"bob":1000},"quotes_added":{}})";
    }
    AppConfig config;
    config.conquister_path = paths.conquister;
    config.quotes_path = paths.quotes;
    Storage storage{config.conquister_path, config.quotes_path};

    const std::optional<ReprogramResult> first = reprogram(storage, 100);
    REQUIRE(first);
    const std::optional<ReprogramResult> second = reprogram(storage, 100);
    REQUIRE(second);
    CHECK(second->player != first->player);
    /* Everybody is his now. */
    CHECK_FALSE(reprogram(storage, 100));

    CommandContext context{
        .storage = storage,
        .config = config,
        .user_id = 1,
        .username = first->player,
        .claims_allowed = true,
        .owner = false,
        .whisper = {},
    };
    const auto reply = [&](std::string_view text) {
        return command_dispatch(context, text).value_or("<nessuna risposta>");
    };

    /* Whatever he tries, Kio decides. */
    CHECK(reply("We @TheConquister37") == std::format("🤖 {} è riprogrammato: decide Kio per lui.", first->player));
    CHECK(reply("/leaderboard") == std::format("🤖 {} è riprogrammato: decide Kio per lui.", first->player));
    /* Except asking for somebody to be set free. */
    CHECK(reply("/libera nessuno") == "🔓 nessuno non è riprogrammato.");

    int attempts = 0;
    while (is_reprogrammed(storage, first->player) && attempts < 50) {
        const std::string said = reply(std::format("/libera {}", first->player));
        CHECK((said.starts_with("🔓") || said.starts_with("🤖")));
        ++attempts;
    }
    CHECK(attempts < 50);
    CHECK(reply("/leaderboard").contains("Classifica"));
}

TEST_CASE("the taxman calls on the leader once a day") {
    const TestPaths paths{"tax-test"};
    {
        std::ofstream file{paths.conquister, std::ios::binary};
        file << R"({"current":null,"scores":{"alice":10000,"bob":500},"quotes_added":{}})";
    }
    Storage storage{paths.conquister, paths.quotes};

    const std::optional<TaxResult> first = tax_the_leader(storage, 10, 1000);
    REQUIRE(first);
    CHECK(first->player == "alice");
    CHECK(first->palle == 1000);
    CHECK(first->left == 9000);
    /* Not twice in the same day. */
    CHECK_FALSE(tax_the_leader(storage, 10, 2000));
    CHECK(tax_the_leader(storage, 10, 1000 + 86400).has_value());
    /* Off unless the owner asks for it. */
    CHECK_FALSE(tax_the_leader(storage, 0, 1000 + (3 * 86400)));
}

TEST_CASE("the five games answer to whatever gets written") {
    const TestPaths paths{"games-test"};
    {
        std::ofstream file{paths.conquister, std::ios::binary};
        file << R"({"current":null,"scores":{"alice":10000,"bob":10000},"quotes_added":{}})";
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
        .whisper = {},
    };
    const auto said = [&](std::string_view text) { return command_dispatch(context, text); };

    /* Nothing is open, so a plain message is a plain message. */
    CHECK_FALSE(said("ciao a tutti").has_value());

    /* A duel is fought the moment somebody asks for one. */
    const std::optional<std::string> fight = said("facciamo un duello");
    REQUIRE(fight);
    CHECK(fight->starts_with("⚔️ alice ha sfidato a duello bob e ha "));
    CHECK(fight->contains("palle passano di mano"));

    SUBCASE("the race goes to whoever speaks first") {
        std::optional<GameOpened> opened;
        for (int tries = 0; tries < 40 && (!opened || opened->kind != Game::race); ++tries) {
            static_cast<void>(game_close(storage, seconds_now_for_test() + 100000));
            opened = game_open(storage, seconds_now_for_test(), 600, 5000);
        }
        REQUIRE(opened);
        REQUIRE(opened->kind == Game::race);
        CHECK(game_opened_reply(*opened).contains("PRONTI? VIA!"));

        const std::optional<std::string> won = said("io!");
        REQUIRE(won);
        CHECK(*won == "🏁 alice è arrivato primo e si prende 5000 palle.");
        CHECK(conquister_user(storage, "alice")->score > 10000);
        /* Over: the next message is nobody's business. */
        CHECK_FALSE(said("e io?").has_value());
    }

    SUBCASE("the number has to be the right one") {
        std::optional<GameOpened> opened;
        for (int tries = 0; tries < 40 && (!opened || opened->kind != Game::guess); ++tries) {
            static_cast<void>(game_close(storage, seconds_now_for_test() + 100000));
            opened = game_open(storage, seconds_now_for_test(), 600, 5000);
        }
        REQUIRE(opened);
        REQUIRE(opened->kind == Game::guess);
        CHECK(opened->secret >= 1);
        CHECK(opened->secret <= 100);

        const std::int64_t wrong = opened->secret == 1 ? 2 : 1;
        CHECK_FALSE(said(std::format("dico {}", wrong)).has_value());
        const std::optional<std::string> hit = said(std::format("allora {}", opened->secret));
        REQUIRE(hit);
        CHECK(hit->contains("ha indovinato"));
    }
}

