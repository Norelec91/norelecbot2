#include "test_paths.hpp"

#include "game.hpp"
#include "commands.hpp"
#include "zodiac.hpp"

#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest/doctest.h>

#include <filesystem>
#include <chrono>
#include <fstream>
#include <format>

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
        CHECK(reply("/quotes 8") == "Solo gli amministratori possono vedere le citazioni.");
        answer = reply("/addquote");
        CHECK(answer.contains("Uso: /addquote"));
        CHECK(answer.contains("1000 palle."));
        CHECK(reply("/delquote 1").contains("Solo gli amministratori"));

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

        storage.transaction([](StorageSession &session) {
            session.state().balloons["tg:1"] = 3;
            return 0;
        });

        context.user_id = 2;
        context.username = "bob";
        answer = reply("We @TheConquister37");
        CHECK(answer.contains("bob sei in "));
        CHECK(answer.contains("\n\ncitazione di prova"));
        CHECK_FALSE(answer.contains("Palloncino gratuito attivo"));
        answer = reply("/leaderboard");
        CHECK(answer.contains("🏆 Classifica @TheConquister37\nOggi è giorno di "));
        CHECK(answer.contains("\n\n1) "));
        CHECK(answer.contains(" — 📜 1 citazione\n"));
        CHECK(answer.contains("\n\n🪐 In @TheConquister37 ora: bob"));

        context.owner = true;
        CHECK(reply("/delquote 1") == "Citazione eliminata: citazione di prova");

        context.owner = false;
        context.user_id = 4;
        context.username = "dave";
        const std::string deprecated_balloon =
            "🎈 /buyballoon è deprecato: non serve più comprare il palloncino. "
            "Lo ricevi gratis quando entri in @TheConquister37, se non hai un boost. "
            "Sparisce quando lasci il posto e non protegge a casa.";
        CHECK(command_is_for_bot("/buyballoon"));
        CHECK(reply("/buyballoon") == deprecated_balloon);
        context.user_id = 5;
        context.username = "erin";
        CHECK(reply("/buyballoon") == deprecated_balloon);

        config.boost_cost = 0;
        context.owner = false;
        context.user_id = 6;
        context.username = "frank";
        CHECK(reply("/buyboost") ==
              "⚡ frank hai comprato un boost spendendo 0 palle! Il tuo prossimo possesso di "
              "@TheConquister37 vale x3, fino a quando ti spodestano.");
        CHECK(reply("/buyboost") == "frank hai già un boost x3 pronto.");
        config.boost_cost = 1500;
        context.user_id = 7;
        context.username = "grace";
        CHECK(reply("/buyboost") == "grace ti servono 1500 palle per un boost (ne hai 0).");

        context.user_id = 8;
        context.username = "heidi";
        CHECK(command_is_for_bot("/buyshield"));
        CHECK(reply("/buyshield") ==
              "🛡️ /buyshield è deprecato: lo scudo non esiste più. "
              "Contro le razzie resta la resistenza, che dimezza il bottino dopo ogni furto subito.");

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

TEST_CASE("Telegram ID keeps its player after a rename and namesakes stay separate") {
    const TestPaths paths{"identity-command-test"};
    AppConfig config;
    config.conquister_path = paths.conquister;
    config.quotes_path = paths.quotes;
    config.boost_cost = 0;
    Storage storage{paths.conquister, paths.quotes};

    CommandContext alice{.storage = storage, .config = config, .user_id = 11, .username = "Alice"};
    CHECK(command_dispatch(alice, "/buyboost")->contains("hai comprato"));
    alice.username = "AliceNuova";
    CHECK(command_dispatch(alice, "/buyboost") == "AliceNuova hai già un boost x3 pronto.");
    CHECK(command_dispatch(alice, "We @AliceNuova") == "🪐 AliceNuova sei già in @AliceNuova!");
    CHECK(command_dispatch(alice, "We @Alice") ==
          "🚀 AliceNuova non conosco nessun giocatore di nome @Alice.");

    const CommandContext namesake{.storage = storage, .config = config, .user_id = 22, .username = "Alice"};
    CHECK(command_dispatch(namesake, "/buyboost")->contains("hai comprato"));
    CHECK(command_dispatch(alice, "/buyboost") == "AliceNuova hai già un boost x3 pronto.");

    const CommandContext irc{.storage = storage, .config = config, .user_id = 0, .username = "AliceNuova"};
    CHECK(command_dispatch(irc, "/buyboost")->contains("hai comprato"));
    CHECK(command_dispatch(alice, "We AliceNuova")->contains("parti per AliceNuova"));
    CHECK(command_dispatch(irc, "We AliceNuova") == "🪐 AliceNuova sei già in AliceNuova!");

    const Json state = Json::parse(std::ifstream{paths.conquister});
    CHECK(state.at("accounts").at("tg:11") != state.at("accounts").at("tg:22"));
    CHECK(state.at("accounts").at("tg:11") != state.at("accounts").at("irc:alicenuova"));
    CHECK(state.at("boosts").size() == 3);
}

TEST_CASE("two authenticated accounts link only after reciprocal confirmation") {
    const TestPaths paths{"link-command-test"};
    AppConfig config;
    config.conquister_path = paths.conquister;
    config.quotes_path = paths.quotes;
    config.boost_cost = 0;
    Storage storage{paths.conquister, paths.quotes};
    const CommandContext telegram{.storage = storage, .config = config, .user_id = 11, .username = "Alice"};
    const CommandContext irc{.storage = storage, .config = config, .user_id = 0, .username = "Alice"};

    CHECK(command_dispatch(telegram, "/buyboost")->contains("hai comprato"));
    CHECK(command_dispatch(telegram, "/link Alice") ==
          "Non conosco ancora quell'account: deve prima usare un comando del gioco.");
    CHECK(command_dispatch(irc, "/leaderboard").has_value());
    CHECK(command_dispatch(telegram, "/link Alice")->contains("Richiesta registrata"));
    CHECK(command_dispatch(irc, "/link @Alice") ==
          "Account collegati: ora condividono lo stesso giocatore.");
    CHECK(command_dispatch(irc, "/buyboost") == "Alice hai già un boost x3 pronto.");
    const Json state = Json::parse(std::ifstream{paths.conquister});
    CHECK(state.at("accounts").at("tg:11") == state.at("accounts").at("irc:alice"));
    Storage reopened{paths.conquister, paths.quotes};
    const CommandContext after_restart{.storage = reopened, .config = config, .user_id = 0,
                                       .username = "Alice"};
    CHECK(command_dispatch(after_restart, "/buyboost") == "Alice hai già un boost x3 pronto.");
}

TEST_CASE("linking never silently merges two inventories") {
    const TestPaths paths{"link-conflict-command-test"};
    AppConfig config;
    config.conquister_path = paths.conquister;
    config.quotes_path = paths.quotes;
    config.boost_cost = 0;
    Storage storage{paths.conquister, paths.quotes};
    const CommandContext telegram{.storage = storage, .config = config, .user_id = 11, .username = "Alice"};
    const CommandContext irc{.storage = storage, .config = config, .user_id = 0, .username = "Alice"};
    CHECK(command_dispatch(telegram, "/buyboost")->contains("hai comprato"));
    CHECK(command_dispatch(irc, "/buyboost")->contains("hai comprato"));
    CHECK(command_dispatch(telegram, "/link Alice")->contains("Richiesta registrata"));
    CHECK(command_dispatch(irc, "/link @Alice")->contains("fusione manuale"));
    const Json state = Json::parse(std::ifstream{paths.conquister});
    CHECK(state.at("accounts").at("tg:11") != state.at("accounts").at("irc:alice"));
    CHECK(state.at("boosts").size() == 2);
}

TEST_CASE("legacy Telegram assets follow their recorded ID, not a reused name") {
    const TestPaths paths{"legacy-identity-command-test"};
    {
        std::ofstream file{paths.conquister, std::ios::binary};
        file << R"({"current":null,"scores":{"Alice":1500},"boosts":{"Alice":3},)"
                R"("telegram_ids":{"Alice":11}})";
    }
    AppConfig config;
    config.conquister_path = paths.conquister;
    config.quotes_path = paths.quotes;
    Storage storage{paths.conquister, paths.quotes};
    const CommandContext rightful{.storage = storage, .config = config, .user_id = 11,
                                  .username = "AliceNuova"};
    const CommandContext namesake{.storage = storage, .config = config, .user_id = 22,
                                 .username = "Alice"};
    const CommandContext irc{.storage = storage, .config = config, .user_id = 0,
                            .username = "Alice"};
    CHECK(command_dispatch(rightful, "/buyboost") == "AliceNuova hai già un boost x3 pronto.");
    CHECK(command_dispatch(namesake, "/buyboost")->contains("(ne hai 0)"));
    CHECK(command_dispatch(irc, "/buyboost")->contains("(ne hai 0)"));
    const Json state = Json::parse(std::ifstream{paths.conquister});
    CHECK(state.at("accounts").at("tg:11") == "Alice");
    CHECK(state.at("accounts").at("tg:22") != "Alice");
    CHECK(state.at("accounts").at("irc:alice") != "Alice");
}

TEST_CASE("ambiguous cross-platform legacy assets remain unclaimed") {
    const TestPaths paths{"ambiguous-identity-command-test"};
    {
        std::ofstream file{paths.conquister, std::ios::binary};
        file << R"({"current":null,"scores":{"Alice":1500},"boosts":{"Alice":3},)"
                R"("telegram_ids":{"Alice":11},"irc_names":{"Alice":1}})";
    }
    AppConfig config;
    config.conquister_path = paths.conquister;
    config.quotes_path = paths.quotes;
    Storage storage{paths.conquister, paths.quotes};
    const CommandContext telegram{.storage = storage, .config = config, .user_id = 11,
                                  .username = "Alice"};
    const CommandContext irc{.storage = storage, .config = config, .user_id = 0,
                            .username = "Alice"};
    CHECK(command_dispatch(telegram, "/buyboost")->contains("(ne hai 0)"));
    CHECK(command_dispatch(irc, "/buyboost")->contains("(ne hai 0)"));
    const Json state = Json::parse(std::ifstream{paths.conquister});
    CHECK(state.at("accounts").at("tg:11") != "Alice");
    CHECK(state.at("accounts").at("irc:alice") != "Alice");
    CHECK(state.at("scores").at("Alice") == 1500);
}

TEST_CASE("unattributed legacy assets stay unclaimed") {
    const TestPaths paths{"unclaimed-identity-command-test"};
    {
        std::ofstream file{paths.conquister, std::ios::binary};
        file << R"({"current":null,"scores":{"Alice":1000}})";
    }
    AppConfig config;
    config.conquister_path = paths.conquister;
    config.quotes_path = paths.quotes;
    Storage storage{paths.conquister, paths.quotes};
    const CommandContext telegram{.storage = storage, .config = config, .user_id = 11,
                                  .username = "Alice"};
    CHECK(command_dispatch(telegram, "/buyboost")->contains("(ne hai 0)"));
    const Json state = Json::parse(std::ifstream{paths.conquister});
    CHECK(state.at("scores").at("Alice") == 1000);
    CHECK(state.at("accounts").at("tg:11") != "Alice");
}

TEST_CASE("IRC nick aliases share the verified NickServ account") {
    const TestPaths paths{"irc-account-command-test"};
    AppConfig config;
    config.conquister_path = paths.conquister;
    config.quotes_path = paths.quotes;
    config.boost_cost = 0;
    Storage storage{paths.conquister, paths.quotes};
    CommandContext irc{.storage = storage, .config = config, .user_id = 0,
                       .username = "FirstNick", .account_name = "RegisteredAccount"};
    CHECK(command_dispatch(irc, "/buyboost")->contains("hai comprato"));
    irc.username = "SecondNick";
    CHECK(command_dispatch(irc, "/buyboost") == "SecondNick hai già un boost x3 pronto.");
    const Json state = Json::parse(std::ifstream{paths.conquister});
    CHECK(state.at("accounts").at("irc:registeredaccount") == "irc:registeredaccount");
    CHECK(state.at("boosts").size() == 1);
    CHECK(state.at("irc_nicks").find("firstnick") == state.at("irc_nicks").end());
    CHECK(state.at("irc_nicks").at("secondnick") == "irc:registeredaccount");
}

TEST_CASE("buyboost refuses an existing hold without charging the player") {
    const TestPaths paths{"boost-current-hold-reply-test"};
    {
        std::ofstream file{paths.conquister, std::ios::binary};
        file << R"({"current":{"user_id":1,"username":"alice","since":0},)"
             << R"("scores":{"alice":2000},"quotes_added":{}})";
    }
    AppConfig config;
    config.conquister_path = paths.conquister;
    config.quotes_path = paths.quotes;
    Storage storage{paths.conquister, paths.quotes};
    const CommandContext context{.storage = storage, .config = config, .user_id = 1, .username = "alice"};

    CHECK(command_dispatch(context, "/buyboost") ==
          "alice sei già in @TheConquister37: torna a casa prima di comprare il boost per il prossimo possesso.");
    CHECK(conquister_user(storage, "alice")->score == 2000);
}

TEST_CASE("the balloon replies are the ones the players read") {
    const TestPaths paths{"balloon-reply-test"};
    const std::int64_t now = std::chrono::duration_cast<std::chrono::seconds>(
                                 std::chrono::system_clock::now().time_since_epoch()
                             ).count();
    {
        std::ofstream file{paths.conquister, std::ios::binary};
        file << R"({"current":{"user_id":1,"username":"alice","since":0},"scores":{},"quotes_added":{},)"
             << R"("balloons":{"alice":3},"cooldowns":{"erin":)" << now + 290
             << R"(},"telegram_ids":{"erin":5,"alice":1}})";
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
    storage.transaction([](StorageSession &session) {
        session.state().balloons["irc:alice"] = 3;
        return 0;
    });
    std::string reply = play(2, "bob");
    CHECK(reply.contains("bob hai cacciato alice da @TheConquister37.\n"));
    CHECK_FALSE(reply.contains("@alice"));
    /* The place keeps its name. */
    CHECK(reply.contains("🪐 bob sei in @TheConquister37!"));

    /* bob played from Telegram, so his name is a mention that reaches him. */
    storage.transaction([](StorageSession &session) {
        session.state().balloons["tg:2"] = 3;
        return 0;
    });
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
        file << R"({"current":null,"scores":{"alice":1000},"quotes_added":{},)"
             << R"("telegram_ids":{"alice":1}})";
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
    CHECK(reply("We @nessuno") == "🚀 carol non conosco nessun giocatore di nome @nessuno.");
    context.username = "bob";
    context.user_id = 2;

    /* The place is still its own move, and a message that names nobody is not one. */
    CHECK(reply("We @TheConquister37").contains("@TheConquister37"));
    CHECK_FALSE(command_dispatch(context, "We @").has_value());
    CHECK_FALSE(command_dispatch(context, "We @ alice").has_value());
    CHECK_FALSE(command_dispatch(context, "We @alice ora").has_value());
    CHECK_FALSE(command_dispatch(context, "we @alice").has_value());

    /* On the road, naming yourself turns you round, and the way back is the road already
       walked: he turned round the moment he left, so he is home at once. */
    CHECK(reply("We @bob") == "🚀 bob lasci perdere e torni in @bob: arrivi tra 0 secondi.");
    CHECK(reply("We @alice") == "🚀 bob sei già in viaggio, torni tra 0 secondi.");

    CHECK(reply("We @TheConquister37") ==
          "🚀 bob sei per strada: non puoi entrare in @TheConquister37 prima di tornare in @bob, tra 0 secondi.");

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
    CHECK_FALSE(command_is_for_bot("We "));

    context.claims_allowed = false;
    CHECK_FALSE(command_dispatch(context, "We @alice").has_value());
}

TEST_CASE("the @ prefix selects Telegram names and bare names select IRC nicks") {
    const TestPaths paths{"irc-raid-target-command-test"};
    AppConfig config;
    config.conquister_path = paths.conquister;
    config.quotes_path = paths.quotes;
    Storage storage{config.conquister_path, config.quotes_path};

    CommandContext context{.storage = storage, .config = config, .user_id = 2, .username = "Giangiui"};
    CHECK(command_dispatch(context, "We @Lucy") ==
          "🚀 Giangiui non conosco nessun giocatore di nome @Lucy.");
    CHECK(command_dispatch(context, "We Lucy") ==
          "🚀 Giangiui non conosco nessun giocatore di nome Lucy.");

    /* The same spelling on IRC and Telegram denotes two accounts until they link. */
    context.user_id = 0;
    context.username = "Lucy";
    CHECK(command_dispatch(context, "We Lucy") == "🪐 Lucy sei già in Lucy!");
    context.user_id = 9;
    context.username = "Lucy";
    CHECK(command_dispatch(context, "/leaderboard").has_value());
    CHECK(command_dispatch(context, "We @Lucy") == "🪐 Lucy sei già in @Lucy!");
    CHECK(command_dispatch(context, "We Lucy")->contains("parti per Lucy"));
    const Json saved = Json::parse(std::ifstream{paths.conquister});
    CHECK(saved.at("telegram_ids").at("tg:9") == 9);
    CHECK(saved.at("irc_names").at("irc:lucy") == 1);
    CHECK(saved.at("accounts").at("tg:9") != saved.at("accounts").at("irc:lucy"));

    context.user_id = 10;
    context.username = "Nina";
    CHECK(command_dispatch(context, "We @Lucy")->contains("Nina parti per Lucy"));
    CHECK(command_dispatch(context, "We Lucy")->contains("sei già in viaggio"));
}

TEST_CASE("We invests and withdraws only with the owner's platform name") {
    const TestPaths paths{"investment-command-test"};
    AppConfig config;
    config.conquister_path = paths.conquister;
    config.quotes_path = paths.quotes;
    Storage storage{paths.conquister, paths.quotes};
    const CommandContext alice{.storage = storage, .config = config, .user_id = 1, .username = "Alice"};
    REQUIRE(command_dispatch(alice, "/leaderboard"));
    storage.transaction([](StorageSession &session) {
        session.state().scores["tg:1"] = 2000;
        return 0;
    });
    CHECK(command_is_for_bot("We @Alice 1000"));
    CHECK_FALSE(command_is_for_bot("We @Alice ora"));
    /* Another name means a delivery, so the refusal is about the player, not about the deposit. */
    CHECK(command_dispatch(alice, "We Alice 1000")->contains("non conosco nessun giocatore di nome Alice"));
    CHECK(command_dispatch(alice, "We @Bob 1000")->contains("non conosco nessun giocatore di nome @Bob"));
    CHECK(command_dispatch(alice, "We @Alice 0")->contains("maggiore di zero"));
    CHECK(command_dispatch(alice, "We @Alice 3000")->contains("solo 2000"));
    const std::string deposited = command_dispatch(alice, "We @Alice 1000").value_or("");
    CHECK(deposited.contains("hai investito 1000 palle."));
    CHECK(deposited.contains("oroscopo "));
    CHECK(conquister_user(storage, "Alice", RaidTargetKind::telegram)->score == 1000);
    const std::string withdrawn = command_dispatch(alice, "We @Alice").value_or("");
    CHECK(withdrawn.contains("hai ritirato"));
    CHECK(withdrawn.contains("rendimento:"));
    CHECK(conquister_user(storage, "Alice", RaidTargetKind::telegram)->score >= 1999);
    CHECK(command_dispatch(alice, "We @Alice") == "🪐 Alice sei già in @Alice!");

    const CommandContext irc{.storage = storage, .config = config, .user_id = 0, .username = "Bob"};
    REQUIRE(command_dispatch(irc, "/leaderboard"));
    storage.transaction([](StorageSession &session) {
        session.state().scores["irc:bob"] = 1000;
        return 0;
    });
    CHECK(command_dispatch(irc, "We Bob 1000")->contains("hai investito 1000"));
    CHECK(command_dispatch(irc, "We Bob")->contains("hai ritirato"));
}

TEST_CASE("investment replies explain the zodiac sign without a percentage multiplier") {
    const TestPaths paths{"investment-horoscope-reply-test"};
    AppConfig config;
    config.conquister_path = paths.conquister;
    config.quotes_path = paths.quotes;
    const std::int64_t today = seconds_now_for_test();
    const zodiac::Element house = zodiac::element_of_day(today);
    const std::string_view favorable = house == zodiac::Element::water ? "cancro" :
        house == zodiac::Element::fire ? "leone" :
        house == zodiac::Element::air ? "gemelli" : "toro";
    const std::string_view unfavorable = house == zodiac::Element::water ? "leone" :
        house == zodiac::Element::fire ? "cancro" :
        house == zodiac::Element::air ? "toro" : "gemelli";
    const std::string_view neutral = house == zodiac::Element::water || house == zodiac::Element::fire
        ? "gemelli" : "cancro";
    config.zodiac_signs = {{"Alice", std::string{favorable}}, {"Bob", std::string{neutral}},
                           {"Carol", std::string{unfavorable}}};
    Storage storage{paths.conquister, paths.quotes};
    storage.transaction([&](StorageSession &session) {
        session.state().investment_magnitudes[std::to_string(zodiac::day_start(today))] = 69;
        return 0;
    });
    const auto check_reply = [&](std::int64_t user_id, std::string_view name,
                                 std::string_view rate, std::string_view label) {
        const CommandContext context{.storage = storage, .config = config, .user_id = user_id, .username = name};
        REQUIRE(command_dispatch(context, "/leaderboard"));
        storage.transaction([&](StorageSession &session) {
            session.state().scores[std::format("tg:{}", user_id)] = 365;
            return 0;
        });
        const std::string message = command_dispatch(context, std::format("We @{} 365", name)).value_or("");
        CHECK(message.contains(std::format("Rendimento di oggi: {}% (oroscopo {}).", rate, label)));
        CHECK_FALSE(message.contains("oroscopo 125%"));
    };
    check_reply(1, "Alice", "+69", "favorevole");
    check_reply(2, "Bob", "+0", "neutro");
    check_reply(3, "Carol", "-69", "sfavorevole");
}

TEST_CASE("an ambiguous old holder is not claimed by an IRC namesake") {
    const TestPaths paths{"legacy-dual-target-command-test"};
    {
        std::ofstream file{paths.conquister, std::ios::binary};
        file << R"({"current":{"user_id":0,"username":"Alice","since":0},)"
             << R"("scores":{"Alice":1000},"telegram_ids":{"Alice":7}})";
    }
    AppConfig config;
    config.conquister_path = paths.conquister;
    config.quotes_path = paths.quotes;
    Storage storage{config.conquister_path, config.quotes_path};
    const CommandContext context{.storage = storage, .config = config, .user_id = 8, .username = "Bob"};
    CHECK(command_dispatch(context, "We Alice") ==
          "🚀 Bob non conosco nessun giocatore di nome Alice.");
}

TEST_CASE("the raids tell what happened") {
    RaidEvent event;
    event.kind = RaidEvent::Kind::stolen;
    event.raider = "bob";
    event.target = "alice";
    event.loot = 250;
    event.seconds = 52;
    CHECK(raid_event_reply(event) == "💰 bob hai rubato 250 palle a alice! Torni in bob tra 52 secondi.");

    event.resistance_absorbed = 125;
    CHECK(raid_event_reply(event) ==
          "💰 bob hai rubato 250 palle a alice! Torni in bob tra 52 secondi.");

    event.target_on_telegram = true;
    event.undefended = true;
    CHECK(raid_event_reply(event) ==
          "💰 bob hai rubato 250 palle a @alice, che non era a casa! Torni in bob tra 52 secondi.");

    event.undefended = false;
    event.raider_percent = 125;
    event.target_percent = 75;
    CHECK(raid_event_reply(event) ==
          "💰 bob hai rubato 250 palle a @alice! Torni in bob tra 52 secondi.");

    RaidEvent home;
    home.kind = RaidEvent::Kind::returned;
    home.raider = "bob";
    home.target = "alice";
    home.loot = 250;
    CHECK(raid_event_reply(home) == "🪐 bob sei tornato in bob con 250 palle.");
    home.loot = 0;
    CHECK(raid_event_reply(home) == "🪐 bob sei tornato in bob a mani vuote.");

    RaidEvent given;
    given.kind = RaidEvent::Kind::delivered;
    given.raider = "bob";
    given.target = "alice";
    given.gift = 700;
    given.seconds = 52;
    CHECK(raid_event_reply(given) == "🎁 bob hai consegnato 700 palle a alice! Torni in bob tra 52 secondi.");
    given.target_on_telegram = true;
    CHECK(raid_event_reply(given) == "🎁 bob hai consegnato 700 palle a @alice! Torni in bob tra 52 secondi.");

    /* He turned back, so the palle he was carrying are his again. */
    home.gift = 700;
    CHECK(raid_event_reply(home) == "🎁 bob sei tornato in bob con le tue 700 palle ancora in tasca.");
}

TEST_CASE("the quotes are open to the admins as well as to the owner") {
    const TestPaths paths{"admin-command-test"};
    AppConfig config;
    config.conquister_path = paths.conquister;
    config.quotes_path = paths.quotes;
    config.quote_cost = 0;
    Storage storage{paths.conquister, paths.quotes};
    CommandContext context{.storage = storage, .config = config, .user_id = 1, .username = "Alice"};
    REQUIRE(command_dispatch(context, "/addquote citazione di prova"));

    const auto reply = [&](std::string_view text) {
        return command_dispatch(context, text).value_or("<nessuna risposta>");
    };
    CHECK(reply("/quotes") == "Solo gli amministratori possono vedere le citazioni.");
    CHECK(reply("/delquote 1") == "Solo gli amministratori possono eliminare le citazioni.");

    context.admin = true;
    CHECK(reply("/quotes").contains("citazione di prova"));
    /* The debug switch stays with the owner alone. */
    CHECK(reply("/debug 1") == "Solo il proprietario può accendere il debug.");
    CHECK(reply("/delquote 1") == "Citazione eliminata: citazione di prova");

    /* An owner is trusted with the quotes without being listed as an admin too. */
    context.admin = false;
    context.owner = true;
    CHECK(reply("/quotes") == "Nessuna citazione in collezione.");
    CHECK(reply("/debug 0").contains("Debug spento"));
}

TEST_CASE("We with a number for the place destroys the palle") {
    const TestPaths paths{"burn-command-test"};
    AppConfig config;
    config.conquister_path = paths.conquister;
    config.quotes_path = paths.quotes;
    Storage storage{paths.conquister, paths.quotes};
    const CommandContext alice{.storage = storage, .config = config, .user_id = 1, .username = "Alice"};
    REQUIRE(command_dispatch(alice, "/leaderboard"));
    storage.transaction([](StorageSession &session) {
        session.state().scores["tg:1"] = 1000;
        return 0;
    });

    CHECK(command_is_for_bot("We @TheConquister37 400"));
    CHECK(command_dispatch(alice, "We @TheConquister37 0")->contains("maggiore di zero"));
    CHECK(command_dispatch(alice, "We @TheConquister37 1001")->contains("hai solo 1000 palle disponibili"));
    CHECK(conquister_user(storage, "Alice", RaidTargetKind::telegram)->score == 1000);

    CHECK(command_dispatch(alice, "We @TheConquister37 400") ==
          "🔥 Alice hai riportato 400 palle in @TheConquister37: sono uscite dal gioco. Te ne restano 600.");
    CHECK(conquister_user(storage, "Alice", RaidTargetKind::telegram)->score == 600);
    /* The place is the place however it is written, and taking it is still a claim. */
    CHECK(command_dispatch(alice, "We theconquister37 100")->contains("uscite dal gioco"));
    CHECK(conquister_user(storage, "Alice", RaidTargetKind::telegram)->score == 500);
    CHECK(command_dispatch(alice, "We @TheConquister37")->contains("Alice"));
}

TEST_CASE("We with a number for somebody else sends the palle to them") {
    const TestPaths paths{"gift-command-test"};
    AppConfig config;
    config.conquister_path = paths.conquister;
    config.quotes_path = paths.quotes;
    config.travel_divisor = 1000000;
    Storage storage{paths.conquister, paths.quotes};
    const CommandContext alice{.storage = storage, .config = config, .user_id = 1, .username = "Alice"};
    const CommandContext bob{.storage = storage, .config = config, .user_id = 2, .username = "Bob"};
    REQUIRE(command_dispatch(alice, "/leaderboard"));
    REQUIRE(command_dispatch(bob, "/leaderboard"));
    storage.transaction([](StorageSession &session) {
        session.state().scores["tg:1"] = 1000;
        session.state().scores["tg:2"] = 50;
        return 0;
    });

    CHECK(command_dispatch(alice, "We @Bob 0")->contains("maggiore di zero"));
    CHECK(command_dispatch(alice, "We @Bob 1001")->contains("hai solo 1000 palle disponibili"));
    CHECK(conquister_user(storage, "Alice", RaidTargetKind::telegram)->score == 1000);

    const std::string leaving = command_dispatch(alice, "We @Bob 400").value_or("");
    CHECK(leaving.contains("🎁 Alice parti per Bob con 400 palle da consegnare"));
    CHECK(leaving.contains("Alice resta scoperto"));
    CHECK(conquister_user(storage, "Alice", RaidTargetKind::telegram)->score == 600);
    /* Handed over only when he gets there, not when he sets off. */
    CHECK(conquister_user(storage, "Bob", RaidTargetKind::telegram)->score == 50);
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
        file << R"({"current":null,"scores":{"alice":30000,"bob":500},"quotes_added":{},)"
                R"("telegram_ids":{"alice":1}})";
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
        file << R"({"current":null,"scores":{"alice":500,"norelec":500},"quotes_added":{},)"
                R"("telegram_ids":{"alice":1,"norelec":2}})";
    }
    AppConfig config;
    config.conquister_path = paths.conquister;
    config.quotes_path = paths.quotes;
    config.boost_cost = 1000;
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

    /* Five hundred palle do not buy a boost that costs a thousand. */
    const std::optional<std::string> broke = command_dispatch(player, "/buyboost");
    REQUIRE(broke);
    CHECK(broke->contains("ti servono"));

    /* The owner turns it on for himself: he buys for nothing. */
    const std::optional<std::string> on = command_dispatch(owner, "/debug 1");
    REQUIRE(on);
    CHECK(on->contains("per te"));
    CHECK(debug_on(storage, "norelec"));
    const std::optional<std::string> bought = command_dispatch(owner, "/buyboost");
    REQUIRE(bought);
    CHECK(bought->contains("boost"));
    CHECK(conquister_user(storage, "norelec")->score == 500);

    /* Everybody else goes on paying as before. */
    CHECK_FALSE(debug_on(storage, "alice"));
    const std::optional<std::string> still_broke = command_dispatch(player, "/buyboost");
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
                R"("quotes_added":{},"telegram_ids":{"d":1}})";
    }
    AppConfig config;
    config.conquister_path = paths.conquister;
    config.quotes_path = paths.quotes;
    config.boost_cost = 1000;
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

    /* The median is ten thousand, so a boost costs a fifth of it, not the list price. */
    REQUIRE(command_dispatch(context, "/buyboost"));
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
        file << R"({"current":null,"scores":{"a":2000,"b":10,"c":30},"quotes_added":{},)"
                R"("telegram_ids":{"a":1}})";
    }
    AppConfig config;
    config.conquister_path = paths.conquister;
    config.quotes_path = paths.quotes;
    config.boost_cost = 1000;
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
    REQUIRE(command_dispatch(context, "/buyboost"));
    CHECK(conquister_user(storage, "a")->score == 1000);

    /* A group swimming in palle stops at the ceiling, fifty times the list price. */
    {
        std::ofstream file{paths.conquister, std::ios::binary};
        file << R"({"current":null,"scores":{"a":90000000,"b":90000000,"c":90000000},)"
                R"("quotes_added":{},"telegram_ids":{"a":1}})";
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
    REQUIRE(command_dispatch(loaded, "/buyboost"));
    CHECK(conquister_user(rich, "a")->score == 90000000 - 50000);
}
