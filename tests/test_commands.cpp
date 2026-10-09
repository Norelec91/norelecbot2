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

/* What a player keeps in his house, as it is saved. */
std::string house_of(Storage &storage, const std::string &player) {
    return storage.transaction([&player](StorageSession &session) {
        const Authors &houses = session.state().furniture;
        const auto found = houses.find(player);
        return found == houses.end() ? std::string{} : found->second;
    });
}

}

TEST_CASE("the bot answers the commands it knows and ignores the rest") {
    const TestPaths paths{"command-test"};
    AppConfig config;
    config.starter_balloon = false;
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
        CHECK(answer.starts_with("alice sei in @TheConquister37!"));
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
        /* No house of the day: the zodiac does not count. */
        CHECK(answer.contains("Classifica @TheConquister37\n\n1) "));
        CHECK_FALSE(answer.contains("giorno di"));
        CHECK(answer.contains(" — 1 citazione\n"));
        CHECK(answer.contains("\n\nIn @TheConquister37 ora: bob"));

        context.owner = true;
        CHECK(reply("/delquote 1") == "Citazione eliminata: citazione di prova");

        /* The retired purchases are gone: the bot does not answer them any more. */
        context.owner = false;
        for (const std::string_view retired : {"/buyboost", "/buyshield", "/buyfurniture"}) {
            CHECK_FALSE(command_is_for_bot(retired));
            CHECK_FALSE(command_dispatch(context, retired));
        }

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
    config.starter_balloon = false;
    config.conquister_path = paths.conquister;
    config.quotes_path = paths.quotes;
    config.furniture_cost = 0;
    Storage storage{paths.conquister, paths.quotes};

    CommandContext alice{.storage = storage, .config = config, .user_id = 11, .username = "Alice"};
    CHECK(command_dispatch(alice, "We @Alice 🍕")->contains(": 🍕 in casa nel posto 1."));
    alice.username = "AliceNuova";
    CHECK(command_dispatch(alice, "We @AliceNuova 🎁")->contains(": 🎁 in casa nel posto 2."));
    CHECK(command_dispatch(alice, "We @AliceNuova") == "AliceNuova sei già in @AliceNuova!");
    CHECK(command_dispatch(alice, "We @Alice") ==
          "AliceNuova non conosco nessun giocatore di nome @Alice.");

    const CommandContext namesake{.storage = storage, .config = config, .user_id = 22, .username = "Alice"};
    CHECK(command_dispatch(namesake, "We @Alice 🐟")->contains(": 🐟 in casa nel posto 1."));
    CHECK(command_dispatch(alice, "We @AliceNuova 🚀")->contains("🚀 AliceNuova hai speso 0 palle: 🚀 ora è con te"));

    const CommandContext irc{.storage = storage, .config = config, .user_id = 0, .username = "AliceNuova"};
    CHECK(command_dispatch(irc, "We AliceNuova 🧀")->contains(": 🧀 in casa nel posto 1."));
    CHECK(command_dispatch(alice, "We AliceNuova")->contains("parti per AliceNuova"));
    CHECK(command_dispatch(irc, "We AliceNuova") == "AliceNuova sei già in AliceNuova!");

    const Json state = Json::parse(std::ifstream{paths.conquister});
    CHECK(state.at("accounts").at("tg:11") != state.at("accounts").at("tg:22"));
    CHECK(state.at("accounts").at("tg:11") != state.at("accounts").at("irc:alicenuova"));
    CHECK(state.at("furniture").size() == 3);
}

TEST_CASE("two authenticated accounts link only after reciprocal confirmation") {
    const TestPaths paths{"link-command-test"};
    AppConfig config;
    config.starter_balloon = false;
    config.conquister_path = paths.conquister;
    config.quotes_path = paths.quotes;
    config.furniture_cost = 0;
    Storage storage{paths.conquister, paths.quotes};
    const CommandContext telegram{.storage = storage, .config = config, .user_id = 11, .username = "Alice"};
    const CommandContext irc{.storage = storage, .config = config, .user_id = 0, .username = "Alice"};

    CHECK(command_dispatch(telegram, "We @Alice 🍕")->contains("hai speso"));
    CHECK(command_dispatch(telegram, "/link Alice") ==
          "Non conosco ancora quell'account: deve prima usare un comando del gioco.");
    CHECK(command_dispatch(irc, "/leaderboard").has_value());
    CHECK(command_dispatch(telegram, "/link Alice")->contains("Richiesta registrata"));
    CHECK(command_dispatch(irc, "/link @Alice") ==
          "Account collegati: ora condividono lo stesso giocatore.");
    CHECK(command_dispatch(irc, "We Alice 🎁")->contains(": 🎁 in casa nel posto 2."));
    const Json state = Json::parse(std::ifstream{paths.conquister});
    CHECK(state.at("accounts").at("tg:11") == state.at("accounts").at("irc:alice"));
    Storage reopened{paths.conquister, paths.quotes};
    const CommandContext after_restart{.storage = reopened, .config = config, .user_id = 0,
                                       .username = "Alice"};
    CHECK(command_dispatch(after_restart, "We Alice 🚀")->contains("🚀 Alice hai speso"));
}

TEST_CASE("linking never silently merges two inventories") {
    const TestPaths paths{"link-conflict-command-test"};
    AppConfig config;
    config.starter_balloon = false;
    config.conquister_path = paths.conquister;
    config.quotes_path = paths.quotes;
    config.furniture_cost = 0;
    Storage storage{paths.conquister, paths.quotes};
    const CommandContext telegram{.storage = storage, .config = config, .user_id = 11, .username = "Alice"};
    const CommandContext irc{.storage = storage, .config = config, .user_id = 0, .username = "Alice"};
    CHECK(command_dispatch(telegram, "We @Alice 🍕")->contains("hai speso"));
    CHECK(command_dispatch(irc, "We Alice 🎁")->contains("hai speso"));
    CHECK(command_dispatch(telegram, "/link Alice")->contains("Richiesta registrata"));
    CHECK(command_dispatch(irc, "/link @Alice")->contains("fusione manuale"));
    const Json state = Json::parse(std::ifstream{paths.conquister});
    CHECK(state.at("accounts").at("tg:11") != state.at("accounts").at("irc:alice"));
    CHECK(state.at("furniture").size() == 2);
}

TEST_CASE("legacy Telegram assets follow their recorded ID, not a reused name") {
    const TestPaths paths{"legacy-identity-command-test"};
    {
        std::ofstream file{paths.conquister, std::ios::binary};
        file << R"({"current":null,"scores":{"Alice":1500},"furniture":{"Alice":"🍕"},)"
                R"("telegram_ids":{"Alice":11}})";
    }
    AppConfig config;
    config.starter_balloon = false;
    config.conquister_path = paths.conquister;
    config.quotes_path = paths.quotes;
    Storage storage{paths.conquister, paths.quotes};
    const CommandContext rightful{.storage = storage, .config = config, .user_id = 11,
                                  .username = "AliceNuova"};
    const CommandContext namesake{.storage = storage, .config = config, .user_id = 22,
                                 .username = "Alice"};
    const CommandContext irc{.storage = storage, .config = config, .user_id = 0,
                            .username = "Alice"};
    /* The old palle are hers: the price is out of reach, but she is told what she has. */
    CHECK(command_dispatch(rightful, "We @AliceNuova 🎁")->contains("(ne hai 1500)"));
    CHECK(command_dispatch(namesake, "We @Alice 🎁")->contains("(ne hai 0)"));
    CHECK(command_dispatch(irc, "We Alice 🎁")->contains("(ne hai 0)"));
    const Json state = Json::parse(std::ifstream{paths.conquister});
    CHECK(state.at("accounts").at("tg:11") == "Alice");
    CHECK(state.at("accounts").at("tg:22") != "Alice");
    CHECK(state.at("accounts").at("irc:alice") != "Alice");
}

TEST_CASE("ambiguous cross-platform legacy assets remain unclaimed") {
    const TestPaths paths{"ambiguous-identity-command-test"};
    {
        std::ofstream file{paths.conquister, std::ios::binary};
        file << R"({"current":null,"scores":{"Alice":1500},"furniture":{"Alice":"🍕"},)"
                R"("telegram_ids":{"Alice":11},"irc_names":{"Alice":1}})";
    }
    AppConfig config;
    config.starter_balloon = false;
    config.conquister_path = paths.conquister;
    config.quotes_path = paths.quotes;
    Storage storage{paths.conquister, paths.quotes};
    const CommandContext telegram{.storage = storage, .config = config, .user_id = 11,
                                  .username = "Alice"};
    const CommandContext irc{.storage = storage, .config = config, .user_id = 0,
                            .username = "Alice"};
    CHECK(command_dispatch(telegram, "We @Alice 🎁")->contains("(ne hai 0)"));
    CHECK(command_dispatch(irc, "We Alice 🎁")->contains("(ne hai 0)"));
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
    config.starter_balloon = false;
    config.conquister_path = paths.conquister;
    config.quotes_path = paths.quotes;
    Storage storage{paths.conquister, paths.quotes};
    const CommandContext telegram{.storage = storage, .config = config, .user_id = 11,
                                  .username = "Alice"};
    CHECK(command_dispatch(telegram, "We @Alice 🎁")->contains("(ne hai 0)"));
    const Json state = Json::parse(std::ifstream{paths.conquister});
    CHECK(state.at("scores").at("Alice") == 1000);
    CHECK(state.at("accounts").at("tg:11") != "Alice");
}

TEST_CASE("IRC nick aliases share the verified NickServ account") {
    const TestPaths paths{"irc-account-command-test"};
    AppConfig config;
    config.starter_balloon = false;
    config.conquister_path = paths.conquister;
    config.quotes_path = paths.quotes;
    config.furniture_cost = 0;
    Storage storage{paths.conquister, paths.quotes};
    CommandContext irc{.storage = storage, .config = config, .user_id = 0,
                       .username = "FirstNick", .account_name = "RegisteredAccount"};
    CHECK(command_dispatch(irc, "We FirstNick 🍕")->contains("hai speso"));
    irc.username = "SecondNick";
    CHECK(command_dispatch(irc, "We SecondNick 🎁")->contains(": 🎁 in casa nel posto 2."));
    const Json state = Json::parse(std::ifstream{paths.conquister});
    CHECK(state.at("accounts").at("irc:registeredaccount") == "irc:registeredaccount");
    CHECK(state.at("furniture").size() == 1);
    CHECK(state.at("irc_nicks").find("firstnick") == state.at("irc_nicks").end());
    CHECK(state.at("irc_nicks").at("secondnick") == "irc:registeredaccount");
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
             << R"(},"telegram_ids":{"erin":5,"alice":1},"equipped":{"alice":"🎈"}})";
    }
    AppConfig config;
    config.starter_balloon = false;
    config.conquister_path = paths.conquister;
    config.quotes_path = paths.quotes;

    Storage storage{config.conquister_path, config.quotes_path};
    CommandContext context{.storage = storage, .config = config, .user_id = 5, .username = "erin"};
    const auto reply = [&](std::string_view text) {
        return command_dispatch(context, text).value_or("<nessuna risposta>");
    };

    const std::string waiting = reply("We @TheConquister37");
    CHECK(waiting.starts_with("erin hai ancora "));
    CHECK(waiting.contains(" minut"));
    CHECK(waiting.ends_with(" di penalità."));

    context.user_id = 2;
    context.username = "bob";
    const std::string popped = reply("We @TheConquister37");
    CHECK(popped.starts_with("bob hai bucato il palloncino di @alice!\n"));
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
    config.starter_balloon = false;
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
    CHECK(reply.contains("bob sei in @TheConquister37!"));

    /* bob played from Telegram, so his name is a mention that reaches him. */
    storage.transaction([](StorageSession &session) {
        session.state().balloons["tg:2"] = 3;
        return 0;
    });
    reply = play(0, "carol");
    CHECK(reply.contains("carol hai cacciato @bob da @TheConquister37.\n"));
}

TEST_CASE("getting in tells what the 🦞 became") {
    const TestPaths paths{"lobster-command-test"};
    AppConfig config;
    config.starter_balloon = false;
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

    CHECK_FALSE(play(0, "alice").contains("aragoste"));
    storage.transaction([](StorageSession &session) {
        session.state().balloons["irc:alice"] = 3;
        session.state().equipped["irc:alice"] = "🍕[]⚡";
        session.state().equipped["tg:2"] = "🦞🦞🦞";
        return 0;
    });
    const std::string reply = play(2, "bob");
    CHECK(reply.contains("\nLe tue aragoste diventano 🍕⚡ finché resti qui."));
}

TEST_CASE("the balloon replies follow the same rule") {
    const TestPaths paths{"mention-balloon-test"};
    {
        std::ofstream file{paths.conquister, std::ios::binary};
        file << R"({"current":{"user_id":0,"username":"alice","since":0},"scores":{},"quotes_added":{},)"
             << R"("balloons":{"alice":3},"cooldowns":{},"equipped":{"alice":"🎈"}})";
    }
    AppConfig config;
    config.starter_balloon = false;
    config.conquister_path = paths.conquister;
    config.quotes_path = paths.quotes;
    Storage storage{config.conquister_path, config.quotes_path};

    const CommandContext context{.storage = storage, .config = config, .user_id = 2, .username = "bob"};
    const std::string reply = command_dispatch(context, "We @TheConquister37").value_or("<nessuna risposta>");
    CHECK(reply.starts_with("bob hai bucato il palloncino di alice!\n"));
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
    config.starter_balloon = false;
    config.conquister_path = paths.conquister;
    config.quotes_path = paths.quotes;
    config.travel_divisor = 1000000;
    Storage storage{config.conquister_path, config.quotes_path};

    CommandContext context{.storage = storage, .config = config, .user_id = 2, .username = "bob"};
    const auto reply = [&](std::string_view text) {
        return command_dispatch(context, text).value_or("<nessuna risposta>");
    };

    CHECK(reply("We @alice") == "bob parti per alice: arrivi tra 5 secondi. Casa tua resta scoperta.");
    CHECK(reply("We @alice") == "bob sei già in viaggio, torni tra 10 secondi.");
    /* Another player, who is at home and can therefore get an answer of his own. */
    context.username = "carol";
    context.user_id = 3;
    CHECK(reply("We @carol") == "carol sei già in @carol!");
    CHECK(reply("We @CAROL") == "carol sei già in @carol!");
    CHECK(reply("We @nessuno") == "carol non conosco nessun giocatore di nome @nessuno.");
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
    /* The clock may tick between leaving and turning round: no time at all, or one second. */
    const auto soon = [](const std::string &said, std::string_view start) {
        return said == std::format("{}0 secondi.", start) || said == std::format("{}1 secondo.", start);
    };
    CHECK(soon(reply("We @bob"), "bob lasci perdere e torni in @bob: arrivi tra "));
    CHECK(soon(reply("We @alice"), "bob sei già in viaggio, torni tra "));

    CHECK(soon(reply("We @TheConquister37"),
               "bob sei per strada: non puoi entrare in @TheConquister37 prima di tornare in @bob, tra "));

    /* The one holding the place stays in it. */
    static_cast<void>(conquister_claim(storage, 9, "erin", seconds_now_for_test()));
    context.username = "erin";
    context.user_id = 9;
    CHECK(reply("We @alice") ==
          "erin razzie e consegne partono da casa tua: esci prima da @TheConquister37 con We @erin.");
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
    config.starter_balloon = false;
    config.conquister_path = paths.conquister;
    config.quotes_path = paths.quotes;
    Storage storage{config.conquister_path, config.quotes_path};

    CommandContext context{.storage = storage, .config = config, .user_id = 2, .username = "Giangiui"};
    CHECK(command_dispatch(context, "We @Lucy") ==
          "Giangiui non conosco nessun giocatore di nome @Lucy.");
    CHECK(command_dispatch(context, "We Lucy") ==
          "Giangiui non conosco nessun giocatore di nome Lucy.");

    /* The same spelling on IRC and Telegram denotes two accounts until they link. */
    context.user_id = 0;
    context.username = "Lucy";
    CHECK(command_dispatch(context, "We Lucy") == "Lucy sei già in Lucy!");
    context.user_id = 9;
    context.username = "Lucy";
    CHECK(command_dispatch(context, "/leaderboard").has_value());
    CHECK(command_dispatch(context, "We @Lucy") == "Lucy sei già in @Lucy!");
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

TEST_CASE("an ambiguous old holder is not claimed by an IRC namesake") {
    const TestPaths paths{"legacy-dual-target-command-test"};
    {
        std::ofstream file{paths.conquister, std::ios::binary};
        file << R"({"current":{"user_id":0,"username":"Alice","since":0},)"
             << R"("scores":{"Alice":1000},"telegram_ids":{"Alice":7}})";
    }
    AppConfig config;
    config.starter_balloon = false;
    config.conquister_path = paths.conquister;
    config.quotes_path = paths.quotes;
    Storage storage{config.conquister_path, config.quotes_path};
    const CommandContext context{.storage = storage, .config = config, .user_id = 8, .username = "Bob"};
    CHECK(command_dispatch(context, "We Alice") ==
          "Bob non conosco nessun giocatore di nome Alice.");
}

TEST_CASE("the raids tell what happened") {
    RaidEvent event;
    event.kind = RaidEvent::Kind::stolen;
    event.raider = "bob";
    event.target = "alice";
    event.loot = 250;
    event.seconds = 52;
    CHECK(raid_event_reply(event) == "bob hai rubato 250 palle a alice! Torni in bob tra 52 secondi.");

    event.target_on_telegram = true;
    event.undefended = true;
    CHECK(raid_event_reply(event) ==
          "bob hai rubato 250 palle a @alice, che non era a casa! Torni in bob tra 52 secondi.");

    event.undefended = false;
    event.balloon_held = true;
    event.next_chance = 50;
    CHECK(raid_event_reply(event) ==
          "bob il palloncino di @alice ha resistito. "
          "Ora il palloncino ha il 50% di probabilità di essere bucato. Torni in bob tra 52 secondi.");
    event.balloon_held = false;
    event.balloon_popped = true;
    CHECK(raid_event_reply(event) ==
          "bob hai bucato il palloncino di @alice e rubato 250 palle! Torni in bob tra 52 secondi.");
    event.balloon_popped = false;

    event.raider_percent = 125;
    event.target_percent = 75;
    CHECK(raid_event_reply(event) ==
          "bob hai rubato 250 palle a @alice! Torni in bob tra 52 secondi.");

    RaidEvent home;
    home.kind = RaidEvent::Kind::returned;
    home.raider = "bob";
    home.target = "alice";
    home.loot = 250;
    CHECK(raid_event_reply(home) == "bob torni in bob con 250 palle.");
    home.loot = 0;
    CHECK(raid_event_reply(home) == "bob sei tornato in bob.");

    RaidEvent given;
    given.kind = RaidEvent::Kind::delivered;
    given.raider = "bob";
    given.target = "alice";
    given.gift = 700;
    given.seconds = 52;
    CHECK(raid_event_reply(given) == "bob hai consegnato 700 palle a alice! Torni in bob tra 52 secondi.");
    given.target_on_telegram = true;
    CHECK(raid_event_reply(given) == "bob hai consegnato 700 palle a @alice! Torni in bob tra 52 secondi.");

    /* He turned back, so the palle he was carrying are his again. */
    home.gift = 700;
    CHECK(raid_event_reply(home) == "bob torni in bob con le tue 700 palle ancora in tasca.");

    /* A raider from Telegram has his planet written with the mention, as everywhere else. */
    home.raider_on_telegram = true;
    CHECK(raid_event_reply(home) == "bob torni in @bob con le tue 700 palle ancora in tasca.");
    given.raider_on_telegram = true;
    CHECK(raid_event_reply(given) == "bob hai consegnato 700 palle a @alice! Torni in @bob tra 52 secondi.");
}

TEST_CASE("the profile shows where a player stands") {
    const TestPaths paths{"profile-command-test"};
    AppConfig config;
    config.starter_balloon = false;
    config.conquister_path = paths.conquister;
    config.quotes_path = paths.quotes;
    config.travel_divisor = 1000000;
    Storage storage{paths.conquister, paths.quotes};
    const CommandContext alice{.storage = storage, .config = config, .user_id = 1, .username = "Alice"};
    const CommandContext bob{.storage = storage, .config = config, .user_id = 2, .username = "Bob"};
    const CommandContext carol{.storage = storage, .config = config, .user_id = 0, .username = "Carol"};
    REQUIRE(command_dispatch(alice, "/leaderboard"));
    REQUIRE(command_dispatch(bob, "/leaderboard"));
    REQUIRE(command_dispatch(carol, "/leaderboard"));
    storage.transaction([](StorageSession &session) {
        ConquisterState &state = session.state();
        state.scores["tg:1"] = 5000;
        state.scores["tg:2"] = 7000;
        state.furniture["tg:1"] = "🍕";
        state.equipped["tg:1"] = "⚡";
        state.quotes_added["tg:1"] = 3;
        state.balloons["tg:1"] = 1;
        return 0;
    });

    CHECK(command_is_for_bot("/profile"));
    const std::string mine = command_dispatch(alice, "/profile").value_or("");
    /* Carol has no palle yet, so the ranking has two players. */
    CHECK(mine.starts_with("Alice\nCon te: ⚡\n5000 palle, 2° su 2 in classifica\n"));
    CHECK_FALSE(mine.contains("giorno di"));
    CHECK(mine.contains("\na casa, in @Alice\n"));
    /* The balloon stays out of it, worn or not. */
    CHECK_FALSE(mine.contains("🎈"));
    CHECK(mine.ends_with("\n3 citazioni"));

    /* The same card, seen by somebody else, with the name as it is written on that platform. */
    CHECK(command_dispatch(bob, "/profile @Alice") == mine);
    CHECK(command_dispatch(bob, "/profile @Nessuno") == "Bob non conosco nessun giocatore di nome @Nessuno.");
    const std::string irc = command_dispatch(bob, "/profile Carol").value_or("");
    CHECK(irc.starts_with("Carol\nCon te: niente\nnessuna palla ancora\n"));
    CHECK_FALSE(irc.contains("🎈"));
    CHECK(irc.contains("\na casa, in Carol"));

    /* In the place with a ⚡, and on the road. */
    REQUIRE(command_dispatch(alice, "We @TheConquister37"));
    CHECK(command_dispatch(alice, "/profile")->contains("\nin @TheConquister37 da "));
    REQUIRE(command_dispatch(bob, "We @Alice")->contains("parti per Alice"));
    CHECK(command_dispatch(alice, "/profile @Bob")->contains("\nin viaggio verso Alice: rientra tra "));
}

TEST_CASE("the help lists the commands, and of the We lines only the one for the place") {
    const TestPaths paths{"help-command-test"};
    AppConfig config;
    config.starter_balloon = false;
    config.conquister_path = paths.conquister;
    config.quotes_path = paths.quotes;
    Storage storage{paths.conquister, paths.quotes};
    const CommandContext telegram{.storage = storage, .config = config, .user_id = 1, .username = "Alice"};
    const CommandContext irc{.storage = storage, .config = config, .user_id = 0, .username = "Bob"};

    CHECK(command_is_for_bot("/help"));
    const std::string help = command_dispatch(telegram, "/help").value_or("");
    CHECK(help.starts_with("Come si gioca\n\n"));
    CHECK(help.contains("\n/buy 🍕 3 — la compri e la metti nel posto 3 della casa\n"));
    CHECK(help.contains("\n/take ⚡ — prendi ⚡ dalla casa e lo porti con te, dove dà il suo bonus\n"));
    CHECK(help.contains("\n/take ⚡ 🚀 — prendi ⚡ e rimetti in casa 🚀 al suo posto\n"));
    CHECK(help.contains("\n/store ⚡ — rimetti ⚡ in casa\n"));
    CHECK(help.contains("\n/back — torni a casa tua, da @TheConquister37 o dal viaggio\n"));
    CHECK_FALSE(help.contains("/home"));
    CHECK(help.contains("\n/house [nome] — la tua casa posto per posto, o quella di un altro\n"));
    CHECK(help.contains("\n/move 1 2 — sposti l'emoji dal posto 1 al posto 2 della casa\n"));
    CHECK(help.ends_with("\n/link <nome> — collega account Telegram e nick IRC Azzurra registrato"));
    CHECK(help.contains("\n/give @giocatore 500 — gli porti 500 palle\n"));
    CHECK(help.contains("\n/burn 🍕 — bruci una 🍕"));
    CHECK(help.contains("\n/leaderboard — classifica\n"));
    CHECK(help.contains("\n/profile [nome] — il tuo profilo o quello di un altro\n"));
    CHECK(help.contains("\nAccanto al nome si vede quello che hai con te; la casa la vedi con /house. Razzie, regali e "
                        "lanci partono solo da casa tua"));
    /* No We line but the one for the place. */
    std::size_t we_lines = 0;
    for (std::size_t at = help.find("\nWe "); at != std::string::npos; at = help.find("\nWe ", at + 1)) {
        ++we_lines;
    }
    CHECK(we_lines == 1);

    /* On IRC: bare nicks and the bang instead of the slash; the place keeps its @. */
    const std::string on_irc = command_dispatch(irc, "/help").value_or("");
    CHECK(on_irc.contains("\n!raid giocatore — parti per razziarlo\n"));
    CHECK(on_irc.contains("We @TheConquister37 (o !avventura) — entri in @TheConquister37"));
    CHECK(help.contains("We @TheConquister37 (o /avventura) — entri in @TheConquister37"));
    CHECK(on_irc.contains("\n!addquote <testo> — "));

    /* Every reply that names a command names it the way it is typed there. */
    CHECK(command_dispatch(irc, "/addquote")->starts_with("Uso: !addquote <testo>."));
    CHECK(command_dispatch(telegram, "/addquote")->starts_with("Uso: /addquote <testo>."));
    CommandContext owner = irc;
    owner.owner = true;
    CHECK(command_dispatch(owner, "/delquote") == "Uso: !delquote <numero da !quotes | testo esatto>.");
    CHECK(command_dispatch(owner, "/debug")->starts_with("Uso: !debug 1 per accendere, !debug 0 per spegnere."));
}

TEST_CASE("We with an emoji carries it to a player or burns it at the place") {
    const TestPaths paths{"emoji-command-test"};
    AppConfig config;
    config.starter_balloon = false;
    config.conquister_path = paths.conquister;
    config.quotes_path = paths.quotes;
    config.travel_divisor = 1000000;
    Storage storage{paths.conquister, paths.quotes};
    const CommandContext alice{.storage = storage, .config = config, .user_id = 1, .username = "Alice"};
    const CommandContext bob{.storage = storage, .config = config, .user_id = 2, .username = "Bob"};
    REQUIRE(command_dispatch(alice, "/leaderboard"));
    REQUIRE(command_dispatch(bob, "/leaderboard"));
    storage.transaction([](StorageSession &session) {
        session.state().furniture["tg:1"] = "🍕🎈🐟";
        return 0;
    });

    CHECK(command_is_for_bot("We @Bob 🍕"));
    CHECK(command_is_for_bot("We @TheConquister37 🍕"));
    CHECK(command_dispatch(alice, "We @Bob 🍕🎈") == "Alice una emoji per volta.");
    CHECK(command_dispatch(alice, "We @Bob 🍕 2") ==
          "Alice la posizione si sceglie solo sul tuo nome: scrivi We @Bob 🍕.");
    CHECK(command_dispatch(alice, "We @Bob 🚀") == "Alice non hai 🚀, né con te né in casa.");
    /* On her own name it is a purchase: one copy already hangs there, so the price doubles. */
    CHECK(command_dispatch(alice, "We @Alice 🍕")->contains("ce ne sono già 1 in giro"));
    CHECK(command_dispatch(alice, "We @Nessuno 🍕") == "Alice non conosco nessun giocatore di nome @Nessuno.");

    /* Nothing is brought to the place any more: it is burnt with /burn. */
    CHECK(command_dispatch(alice, "We @TheConquister37 🐟") ==
          "Alice a @TheConquister37 non si porta niente: per bruciare 🐟 scrivi /burn 🐟.");
    CHECK(command_dispatch(alice, "/give @TheConquister37 🐟") ==
          "Alice a @TheConquister37 non si porta niente: per bruciare 🐟 scrivi /burn 🐟.");
    CHECK(command_dispatch(alice, "/burn 🐟") == "Alice hai bruciato 🐟: è uscita dal gioco.");
    CHECK(command_dispatch(alice, "/burn 🐟") == "Alice non hai 🐟, né con te né in casa.");
    /* Two slots of her house: the emoji change places. */
    CHECK(command_is_for_bot("We @Alice 1 2"));
    CHECK(command_dispatch(alice, "We @Alice 1 2") ==
          "Alice hai scambiato 🍕 e 🎈: ora 🍕 è nel posto 2 e 🎈 nel posto 1. Casa: 🎈🍕");
    CHECK(command_dispatch(alice, "We @Alice 2 4") == "Alice hai spostato 🍕 dal posto 2 al posto 4. Casa: 🎈[][]🍕");
    CHECK(command_dispatch(alice, "We @Alice 4 1") ==
          "Alice hai scambiato 🍕 e 🎈: ora 🍕 è nel posto 1 e 🎈 nel posto 4. Casa: 🍕[][]🎈");
    CHECK(command_dispatch(alice, "We @Alice 4 2") == "Alice hai spostato 🎈 dal posto 4 al posto 2. Casa: 🍕🎈");
    CHECK(command_dispatch(alice, "We @Alice 3 1") == "Alice nel posto 3 della casa non c'è nessuna emoji.");
    CHECK(command_dispatch(alice, "We @Alice 1 1") == "Alice il posto di partenza e quello di arrivo sono lo stesso.");
    CHECK(command_dispatch(alice, "We @Alice 1 12") == "Alice i posti della casa vanno da 1 a 10.");
    CHECK(command_dispatch(alice, "We @Bob 1 2") == "Alice puoi spostare solo le emoji sul tuo nome.");

    /* A pile of poo is not just burnt: it is thrown. */
    storage.transaction([](StorageSession &session) {
        session.state().furniture["tg:1"] = "🍕🎈💩";
        session.state().scores.emplace("tg:1", 0);
        return 0;
    });
    /* Owning a 💩 is not being hit by one. */
    CHECK(command_dispatch(alice, "/profile").value_or("").starts_with("Alice\nCon te: niente\n"));
    CHECK(command_dispatch(alice, "/house").value_or("").contains("1 🍕  2 🎈  3 💩"));
    CHECK(command_dispatch(alice, "We @TheConquister37 💩") ==
          "@Alice, tiri una palla di cacca a @TheConquister37, bravo hai fatto centro, l'hai completamente smerdato!");
    CHECK(house_of(storage, "tg:1") == "🍕🎈");
    CHECK(command_dispatch(alice, "We @TheConquister37 💩") == "Alice non hai 💩, né con te né in casa.");

    const std::string leaving = command_dispatch(alice, "We @Bob 🍕").value_or("");
    CHECK(leaving.starts_with("Alice parti per Bob con 🍕 da consegnare: arrivi tra "));
    CHECK(house_of(storage, "tg:1") == "[]🎈");
    /* On the road she cannot buy, and the old command only points to the new way. */
    CHECK(command_dispatch(alice, "We @Alice 🚀") ==
          "Alice sei in viaggio: le emoji si comprano da casa tua. Per tornare indietro scrivi /back.");
    CHECK(command_dispatch(alice, "We @Alice 1 2") ==
          "Alice sei in viaggio: le emoji si spostano da casa tua. Per tornare indietro scrivi /back.");
    CHECK(command_dispatch(alice, "/burn 🎈") ==
          "Alice sei in viaggio: si brucia da casa tua o da @TheConquister37. Per tornare indietro scrivi /back.");
    CHECK(command_dispatch(alice, "/burn 1") ==
          "Alice sei in viaggio: si brucia da casa tua o da @TheConquister37. Per tornare indietro scrivi /back.");

    /* Once the pizza is handed over she is on her way back: nothing to turn around, just when she is home. */
    const std::int64_t later = std::chrono::duration_cast<std::chrono::seconds>(
        std::chrono::system_clock::now().time_since_epoch()).count() + 6;
    REQUIRE(raid_due(storage, later, RaidRules{}).size() == 1);
    const std::string back = command_dispatch(alice, "We @Alice 1 2").value_or("");
    CHECK(back.starts_with("Alice sei sulla via del ritorno: le emoji si spostano da casa tua. Rientri tra "));

    RaidEvent given;
    given.kind = RaidEvent::Kind::delivered;
    given.raider = "Alice";
    given.target = "Bob";
    given.target_on_telegram = true;
    given.gift_emoji = "🍕";
    given.target_emoji = "🍕";
    given.seconds = 5;
    CHECK(raid_event_reply(given) == "Alice hai consegnato 🍕 a @Bob! Torni in Alice tra 5 secondi.");
    /* A pile of poo that lands is a throw, like the one at the place. */
    RaidEvent poo = given;
    poo.gift_emoji = "💩";
    poo.raider_on_telegram = true;
    CHECK(raid_event_reply(poo) ==
          "@Alice, tiri una palla di cacca a @Bob, bravo hai fatto centro, l'hai completamente smerdato!");
    /* A bomb says what it took, or that it found nothing. */
    RaidEvent bomb = given;
    bomb.gift_emoji = "💣";
    bomb.target_emoji = "🍕";
    CHECK(raid_event_reply(bomb) == "Alice la tua bomba esplode in casa di @Bob ma non trova niente da "
                                    "portarsi via. Torni in Alice tra 5 secondi.");
    bomb.blown = {"🥺", "⚡"};
    CHECK(raid_event_reply(bomb) ==
          "Alice la tua bomba esplode in casa di @Bob e si porta via 🥺⚡! Torni in Alice tra 5 secondi.");
    RaidEvent bounced = bomb;
    bounced.sent_back = true;
    CHECK(raid_event_reply(bounced) == "La cassetta di @Bob rispedisce 💣 al mittente: Alice esplode a casa tua e si "
                                       "porta via 🥺⚡! Torni in Alice tra 5 secondi.");
    bounced.gift_emoji = "💩";
    CHECK(raid_event_reply(bounced) == "La cassetta di @Bob rispedisce 💩 al mittente: Alice ora lo smerdato sei tu! "
                                       "Torni in Alice tra 5 secondi.");
    RaidEvent seeded = given;
    seeded.gift_emoji = "💦";
    seeded.expecting = 32400;
    CHECK(raid_event_reply(seeded) ==
          "Alice la tua 💦 è arrivata a casa di @Bob: tra 9 ore si vedrà. Torni in Alice tra 5 secondi.");
    RaidEvent empty_house = given;
    empty_house.gift_emoji = "💦";
    empty_house.nobody_home = true;
    CHECK(raid_event_reply(empty_house) ==
          "Alice a casa di @Bob non c'è nessuno: la 💦 te la riporti a casa. Torni in Alice tra 5 secondi.");
    RaidEvent iced = given;
    iced.gift_emoji = "🧊";
    iced.froze = 300;
    CHECK(raid_event_reply(iced) == "Alice congeli @Bob: per 5 minuti non può entrare in @TheConquister37 né partire. "
                                    "Torni in Alice tra 5 secondi.");
    iced.froze = 0;
    iced.melted = true;
    CHECK(raid_event_reply(iced) == "Alice il 🔥 di @Bob scioglie subito la tua 🧊: non resta congelato. Torni in Alice "
                                    "tra 5 secondi.");
    iced.froze = 120;
    CHECK(raid_event_reply(iced).value_or("").starts_with("Alice congeli @Bob, ma il suo 🔥 accorcia il gelo: per 2 minuti"));
    RaidEvent birth;
    birth.kind = RaidEvent::Kind::born;
    birth.raider = "Alice";
    birth.raider_on_telegram = true;
    birth.target = "Bob";
    birth.target_on_telegram = true;
    birth.target_emoji = "🍕👶";
    birth.gift_emoji = "👶";
    CHECK(raid_event_reply(birth) == "@Bob è nata una femmina 👶: il padre è @Alice.");
    birth.gift = 1;
    birth.target_emoji = "👶";
    birth.blown = {"🍕"};
    CHECK(raid_event_reply(birth) ==
          "@Bob è nato un maschio 👶: il padre è @Alice. Non c'era un posto libero: ha preso quello di 🍕.");
    birth.raider.clear();
    birth.blown.clear();
    CHECK(raid_event_reply(birth) == "@Bob è nato un maschio 👶: i genitori sono il 👨 e la 👩 di casa.");
    RaidEvent left;
    left.kind = RaidEvent::Kind::gone;
    left.target = "Bob";
    left.target_on_telegram = true;
    left.gift_emoji = "👴";
    CHECK(raid_event_reply(left) == "@Bob 👴 ha vissuto la sua vita e se n'è andato: il posto è di nuovo libero.");
    bomb.backfired = true;
    CHECK(raid_event_reply(bomb) ==
          "Alice la bomba era difettosa: ti esplode in mano e si porta via 🥺⚡! Torni in Alice tra 5 secondi.");
    bomb.blown.clear();
    CHECK(raid_event_reply(bomb) == "Alice la bomba era difettosa: ti esplode in mano, ma non avevi niente con te "
                                    "da perdere. Torni in Alice tra 5 secondi.");
    /* Hit, he is "lo smerdato" wherever he is named. */
    RaidEvent robbed;
    robbed.raider = "Carol";
    robbed.target = "Bob";
    robbed.target_smeared = true;
    robbed.target_emoji = "🍕";
    robbed.loot = 3;
    CHECK(raid_event_reply(robbed).value_or("").contains("Bob lo smerdato"));
    RaidEvent quiet = robbed;
    quiet.sneaked = true;
    quiet.seconds = 5;
    CHECK(raid_event_reply(quiet) == "Carol scivoli di nascosto oltre le difese di Bob lo smerdato e rubi 3 palle! "
                                     "Torni in Carol tra 5 secondi.");
    RaidEvent found = robbed;
    found.alarmed = true;
    found.seconds = 5;
    CHECK(raid_event_reply(found).value_or("").starts_with(
        "L'allarme di Bob ti scopre: devi vedertela con le sue difese.\nCarol hai rubato 3 palle a Bob lo smerdato"));
    RaidEvent eaten = robbed;
    eaten.eaten = "⚡";
    CHECK(raid_event_reply(eaten).value_or("").starts_with("Il 🦖 di "));
    CHECK(raid_event_reply(eaten).value_or("").contains(" ti mangia ⚡.\n"));
    RaidEvent boarded = robbed;
    boarded.boarded = "🍕";
    boarded.seconds = 5;
    CHECK(raid_event_reply(boarded).value_or("").ends_with(
        "\nArrembaggio: ti porti via anche 🍕 da casa sua, e ora è con te: non dà nessun bonus."));
    /* What works on him says what it is worth there, all the copies he has on him counted. */
    boarded.boarded = "⚡";
    boarded.raider_emoji = "⚡⚡";
    AppConfig numbers;
    numbers.lightning_percent = 10;
    CHECK(raid_event_reply(boarded, numbers).value_or("").ends_with(
        "\nArrembaggio: ti porti via anche ⚡ da casa sua, e ora è con te: ora ne hai 2 con te: +20% di palle in "
        "@TheConquister37."));
    RaidEvent caught = robbed;
    caught.loot = 0;
    caught.intercepted = true;
    caught.seconds = 5;
    CHECK(raid_event_reply(caught) ==
          "Carol il cane di Bob lo smerdato ti ha intercettato: niente bottino. Torni in Carol tra 5 secondi.");
    robbed.target_emoji = "🥺🥺";
    robbed.spared = 1;
    robbed.pleaded_percent = 10;
    CHECK(raid_event_reply(robbed).value_or("").ends_with(
        "\nBob lo smerdato ti ha impietosito: gli rubi 3 palle invece di 4 palle (10% in meno)."));
    given.no_room = true;
    given.target_emoji = "🐝🐝";
    CHECK(raid_event_reply(given) ==
          "Alice @Bob non ha più posto per 🍕: te la riporti a casa tua. Torni in Alice tra 5 secondi.");

    RaidEvent home;
    home.kind = RaidEvent::Kind::returned;
    home.raider = "Alice";
    home.gift_emoji = "🍕";
    CHECK(raid_event_reply(home) == "Alice torni in Alice con 🍕 ancora in tasca.");
}

TEST_CASE("a 💩 thrown at the place makes the holder \"lo smerdato\" for a day") {
    const TestPaths paths{"smeared-command-test"};
    AppConfig config;
    config.starter_balloon = false;
    config.conquister_path = paths.conquister;
    config.quotes_path = paths.quotes;
    /* No duds here: the bombs must go off where they are thrown. */
    config.bomb_dud_percent = 0;
    Storage storage{config.conquister_path, config.quotes_path};
    const CommandContext alice{.storage = storage, .config = config, .user_id = 1, .username = "Alice"};
    const CommandContext bob{.storage = storage, .config = config, .user_id = 2, .username = "Bob"};

    REQUIRE(command_dispatch(bob, "We @TheConquister37"));
    storage.transaction([](StorageSession &session) {
        session.state().furniture["tg:1"] = "💩💩";
        session.state().furniture["tg:2"] = "💩";
        session.state().scores.emplace("tg:1", 0);
        return 0;
    });
    /* Throwing one at the place from inside hits nobody. */
    CHECK(command_dispatch(bob, "We @TheConquister37 💩") ==
          "@Bob, tiri una palla di cacca a @TheConquister37, bravo hai fatto centro, l'hai completamente smerdato!");
    CHECK(command_dispatch(bob, "/profile").value_or("").starts_with("Bob\n"));

    CHECK(command_dispatch(alice, "We @TheConquister37 💩") ==
          "@Alice, tiri una palla di cacca a @Bob in @TheConquister37, bravo hai fatto centro, "
          "l'hai completamente smerdato!");
    CHECK(command_dispatch(bob, "/profile").value_or("").starts_with("Bob lo smerdato\n"));
    const std::string board = command_dispatch(alice, "/leaderboard").value_or("");
    CHECK(board.contains("In @TheConquister37 ora: Bob lo smerdato"));
    CHECK(board.contains(" Alice — "));

    /* A bomb thrown at the place goes off on him too, among what he carries. */
    storage.transaction([](StorageSession &session) {
        session.state().furniture["tg:1"] = "💣💣";
        session.state().equipped["tg:2"] = "🍕⚡";
        return 0;
    });
    CHECK(command_dispatch(alice, "We @TheConquister37 💣") ==
          "Alice la tua bomba esplode addosso a @Bob in @TheConquister37 e si porta via ⚡!");
    CHECK(command_dispatch(alice, "We @TheConquister37 💣") ==
          "Alice la tua bomba esplode addosso a @Bob in @TheConquister37 ma non trova niente da portarsi via.");

    /* A day later he is clean, and the entry is gone. */
    storage.transaction([](StorageSession &session) {
        session.state().smeared["tg:2"] -= 86400;
        return 0;
    });
    const std::int64_t now = std::chrono::duration_cast<std::chrono::seconds>(
        std::chrono::system_clock::now().time_since_epoch()).count();
    CHECK(raid_due(storage, now, RaidRules{}).empty());
    CHECK(command_dispatch(bob, "/profile").value_or("").starts_with("Bob\nCon te: 🍕\n"));
    CHECK(storage.transaction([](StorageSession &session) { return session.state().smeared.empty(); }));
}

TEST_CASE("a 🇷🇺 or a 🇮🇱 is a 💩 in all but looks") {
    for (const std::string_view flag : {"🇷🇺", "🇮🇱"}) {
        const Power *power = power_of(flag);
        REQUIRE(power != nullptr);
        CHECK(power->emoji == power::poo.emoji);
    }
    CHECK(power_of("🇮🇹") == nullptr);

    const TestPaths paths{"flag-poo-command-test"};
    AppConfig config;
    config.starter_balloon = false;
    config.conquister_path = paths.conquister;
    config.quotes_path = paths.quotes;
    Storage storage{config.conquister_path, config.quotes_path};
    const CommandContext alice{.storage = storage, .config = config, .user_id = 1, .username = "Alice"};
    const CommandContext bob{.storage = storage, .config = config, .user_id = 2, .username = "Bob"};

    REQUIRE(command_dispatch(bob, "We @TheConquister37"));
    storage.transaction([](StorageSession &session) {
        session.state().furniture["tg:1"] = "🇷🇺🇮🇱";
        session.state().scores.emplace("tg:1", 0);
        return 0;
    });
    CHECK(command_dispatch(alice, "We @TheConquister37 🇷🇺") ==
          "@Alice, tiri una palla di cacca a @Bob in @TheConquister37, bravo hai fatto centro, "
          "l'hai completamente smerdato!");
    CHECK(command_dispatch(bob, "/profile").value_or("").starts_with("Bob lo smerdato\n"));
    CHECK(command_dispatch(alice, "/emoji").value_or("").contains("💩 🇷🇺 🇮🇱 chi la prende"));
}

TEST_CASE("the ☢️ costs a million and starts the game over from the place") {
    const TestPaths paths{"nuke-command-test"};
    AppConfig config;
    config.starter_balloon = false;
    config.conquister_path = paths.conquister;
    config.quotes_path = paths.quotes;
    Storage storage{config.conquister_path, config.quotes_path};
    const CommandContext alice{.storage = storage, .config = config, .user_id = 1, .username = "Alice"};
    const CommandContext bob{.storage = storage, .config = config, .user_id = 2, .username = "Bob"};
    REQUIRE(command_dispatch(bob, "We @TheConquister37"));
    REQUIRE(command_dispatch(alice, "/profile"));
    storage.transaction([](StorageSession &session) {
        session.state().scores["tg:1"] = 999'999;
        session.state().furniture["tg:2"] = "☢️";
        return 0;
    });
    /* A palla short of a million, whatever copies are around. */
    CHECK(command_dispatch(alice, "We @Alice ☢️").value_or("").contains("1000000"));
    CHECK(storage.transaction([](StorageSession &session) { return session.state().furniture.count("tg:1"); }) == 0);
    storage.transaction([](StorageSession &session) {
        session.state().scores["tg:1"] = 1'000'007;
        return 0;
    });
    REQUIRE(command_dispatch(alice, "We @Alice ☢️"));
    CHECK(house_of(storage, "tg:1") == "☢️");
    CHECK(conquister_user(storage, "Alice", RaidTargetKind::telegram)->score == 7);

    RaidEvent landed;
    landed.kind = RaidEvent::Kind::delivered;
    landed.raider = "Alice";
    landed.target = "Bob";
    landed.target_on_telegram = true;
    landed.gift_emoji = "☢️";
    landed.reset = true;
    landed.seconds = 5;
    CHECK(raid_event_reply(landed) == "Alice ha sganciato la bomba nucleare su casa di @Bob: riparte da zero, senza "
                                      "palle e con il solo 🎈 di partenza. Torni in Alice tra 5 secondi.");
    CHECK(command_dispatch(alice, "We @TheConquister37 ☢️") ==
          "Alice ha sganciato la bomba nucleare su @TheConquister37: il gioco riparte da zero. Tutti senza palle e "
          "con il solo 🎈 di partenza, @TheConquister37 è vuoto e nessuno è in viaggio.");
    CHECK(furniture_all(storage).at("tg:1") == "🎈");
    CHECK(furniture_all(storage).at("tg:2") == "🎈");
    CHECK(conquister_user(storage, "Alice", RaidTargetKind::telegram)->score == 0);
    CHECK_FALSE(storage.transaction([](StorageSession &session) { return session.state().current.has_value(); }));
    for (const auto &entry : std::filesystem::directory_iterator{"."}) {
        if (entry.path().filename().string().starts_with(paths.conquister + ".before-reset-")) {
            std::filesystem::remove(entry.path());
        }
    }
}

TEST_CASE("everybody starts with a 🎈, which is what defends him and can be bought again") {
    const TestPaths paths{"starter-balloon-test"};
    AppConfig config;
    config.conquister_path = paths.conquister;
    config.quotes_path = paths.quotes;
    Storage storage{config.conquister_path, config.quotes_path};
    const CommandContext alice{.storage = storage, .config = config, .user_id = 1, .username = "Alice"};
    const CommandContext bob{.storage = storage, .config = config, .user_id = 2, .username = "Bob"};

    /* Seen for the first time, she is handed one; never a second time. */
    CHECK(command_dispatch(alice, "/profile").value_or("").starts_with("Alice\nCon te: 🎈\n"));
    storage.transaction([](StorageSession &session) {
        session.state().equipped.erase("tg:1");
        session.state().scores["tg:1"] = 1500;
        return 0;
    });
    CHECK(command_dispatch(alice, "/profile").value_or("").starts_with("Alice\n"));

    /* Without one she holds nothing off: bob walks in. */
    REQUIRE(command_dispatch(alice, "We @TheConquister37"));
    const std::string walked = command_dispatch(bob, "We @TheConquister37").value_or("");
    CHECK(walked.contains("hai cacciato @Alice da @TheConquister37."));
    CHECK_FALSE(walked.contains("palloncino"));

    /* A new one has its own price, whatever copies are around. */
    CHECK(command_dispatch(alice, "We @Alice 🎈").value_or("").contains("🎈 Alice hai speso 1000 palle: 🎈 ora è con te"));
    CHECK(conquister_user(storage, "Alice", RaidTargetKind::telegram)->score >= 500);
}

TEST_CASE("a popped 🎈 stays on the name, and a player is handed only one") {
    const TestPaths paths{"balloon-regen-test"};
    AppConfig config;
    config.conquister_path = paths.conquister;
    config.quotes_path = paths.quotes;
    config.cooldown_seconds = 0;
    const auto furniture_of_alice = [](Storage &storage) {
        const Authors all = furniture_all(storage);
        return all.count("tg:1") == 0 ? std::string{} : all.at("tg:1");
    };
    {
        Storage storage{config.conquister_path, config.quotes_path};
        const CommandContext alice{.storage = storage, .config = config, .user_id = 1, .username = "Alice"};
        const CommandContext bob{.storage = storage, .config = config, .user_id = 2, .username = "Bob"};
        REQUIRE(command_dispatch(alice, "We @TheConquister37"));
        CHECK(furniture_of_alice(storage) == "🎈");
        /* bob keeps at it until her balloon pops and he is in: it is still hers, as good as new. */
        std::string reply;
        for (int attempt = 0; attempt < 8 && !reply.contains("hai cacciato"); ++attempt) {
            reply = command_dispatch(bob, "We @TheConquister37").value_or("");
        }
        CHECK(reply.contains("hai bucato il palloncino di @Alice"));
        CHECK(furniture_of_alice(storage) == "🎈");
        /* Burnt, it is gone for good: nothing she does hands her another. */
        REQUIRE(command_dispatch(alice, "/burn 🎈"));
        CHECK(furniture_of_alice(storage).empty());
        REQUIRE(command_dispatch(alice, "/profile"));
        CHECK(furniture_of_alice(storage).empty());
    }
    /* Nor does a restart. */
    Storage storage{config.conquister_path, config.quotes_path};
    balloons_hand_out(storage, 10);
    CHECK(furniture_of_alice(storage).empty());
}

TEST_CASE("a 🥷 takes the claimer past the holder's 🎈") {
    const TestPaths paths{"ninja-claim-test"};
    {
        std::ofstream file{paths.conquister, std::ios::binary};
        file << R"({"current":{"user_id":0,"username":"alice","since":0},"scores":{},"quotes_added":{},)"
             << R"("equipped":{"alice":"🎈"}})";
    }
    AppConfig config;
    config.starter_balloon = false;
    config.conquister_path = paths.conquister;
    config.quotes_path = paths.quotes;
    config.ninja_percent = 100;
    Storage storage{config.conquister_path, config.quotes_path};
    const CommandContext bob{.storage = storage, .config = config, .user_id = 2, .username = "bob"};
    REQUIRE(command_dispatch(bob, "/profile"));
    storage.transaction([](StorageSession &session) {
        session.state().equipped["tg:2"] = "🥷🏿";
        return 0;
    });
    const std::string reply = command_dispatch(bob, "We @TheConquister37").value_or("");
    CHECK(reply.starts_with("bob scivoli di nascosto oltre il palloncino di alice!\n"));
    CHECK(reply.contains("bob hai cacciato alice da @TheConquister37.\n"));
    CHECK_FALSE(reply.contains("bucato"));
}

TEST_CASE("the 🌀 is an emoji like any other: anybody hangs it, and /richiama is gone") {
    const TestPaths paths{"vortex-gone-test"};
    AppConfig config;
    config.starter_balloon = false;
    config.conquister_path = paths.conquister;
    config.quotes_path = paths.quotes;
    Storage storage{config.conquister_path, config.quotes_path};
    CommandContext alice{.storage = storage, .config = config, .user_id = 1, .username = "Alice"};
    storage.transaction([](StorageSession &session) {
        session.state().scores["tg:1"] = 1'000'000;
        return 0;
    });
    CHECK(command_dispatch(alice, "We @Alice 🌀").value_or("").contains("hai speso"));
    alice.owner = true;
    CHECK_FALSE(command_dispatch(alice, "/richiama @Alice").has_value());
}

TEST_CASE("/emoji tells every emoji with a power, where it is and what can hit it") {
    const TestPaths paths{"emoji-help-test"};
    AppConfig config;
    config.starter_balloon = false;
    config.conquister_path = paths.conquister;
    config.quotes_path = paths.quotes;
    Storage storage{config.conquister_path, config.quotes_path};
    const CommandContext alice{.storage = storage, .config = config, .user_id = 1, .username = "Alice"};
    const std::string help = command_dispatch(alice, "/emoji").value_or("");
    /* None is left out, and none is listed without its words. */
    for (const Power &power : powers) {
        const std::size_t at = help.find(std::format("\n{} ", power.emoji));
        REQUIRE(at != std::string::npos);
        CHECK(help.at(at + power.emoji.size() + 2) != '\n');
    }
    CHECK(help.contains("\n🥺 chi ti razzia ruba il 10% in meno per ognuna\n"));
    CHECK(help.contains("\n⚡ +10% di palle in @TheConquister37 per ognuno\n"));
    CHECK(help.contains("🎈 il palloncino: con te difende te, casa tua quando ci sei e @TheConquister37 quando lo "
                        "tieni; in casa difende la casa anche quando sei fuori; bucato torna nuovo; uno nuovo costa "
                        "1000 palle. Invincibile: né bombe né furti\n"));
    CHECK(help.contains("\n💩 🇷🇺 🇮🇱 chi la prende è \"lo smerdato\" per 1 giorno\n"));
    CHECK(help.starts_with("Emoji con un potere\n\nHai due posti per le emoji: con te (5 posti), accanto al nome, e la "
                           "casa (10 posti), con /house."));
    CHECK(help.contains("Funzionano in casa"));
    CHECK(help.contains("Funzionano con te"));
    CHECK(help.contains("Si lanciano"));
    CHECK(command_dispatch(alice, "/help").value_or("").contains("\n/emoji — "));
    /* It fits in one Telegram message. */
    CHECK(help.size() < 4096);
}

TEST_CASE("the quotes are open to the admins as well as to the owner") {
    const TestPaths paths{"admin-command-test"};
    AppConfig config;
    config.starter_balloon = false;
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

TEST_CASE("We with a number for yourself does nothing") {
    const TestPaths paths{"self-transfer-command-test"};
    AppConfig config;
    config.starter_balloon = false;
    config.conquister_path = paths.conquister;
    config.quotes_path = paths.quotes;
    Storage storage{paths.conquister, paths.quotes};
    const CommandContext alice{.storage = storage, .config = config, .user_id = 1, .username = "Alice"};
    REQUIRE(command_dispatch(alice, "/leaderboard"));
    storage.transaction([](StorageSession &session) {
        session.state().scores["tg:1"] = 2000;
        return 0;
    });
    CHECK(command_dispatch(alice, "We @Alice 1000") == "Alice non puoi portare palle a te stesso.");
    CHECK(conquister_user(storage, "Alice", RaidTargetKind::telegram)->score == 2000);
    /* Not even from the place, which she does not leave for it. */
    REQUIRE(command_dispatch(alice, "We @TheConquister37"));
    CHECK(command_dispatch(alice, "We @Alice 1000") == "Alice non puoi portare palle a te stesso.");
    CHECK(conquister_user(storage, "Alice", RaidTargetKind::telegram)->in_conquister);
}

TEST_CASE("/burn with a number destroys the palle, which are no longer brought to the place") {
    const TestPaths paths{"burn-command-test"};
    AppConfig config;
    config.starter_balloon = false;
    config.conquister_path = paths.conquister;
    config.quotes_path = paths.quotes;
    Storage storage{paths.conquister, paths.quotes};
    const CommandContext alice{.storage = storage, .config = config, .user_id = 1, .username = "Alice"};
    REQUIRE(command_dispatch(alice, "/leaderboard"));
    storage.transaction([](StorageSession &session) {
        session.state().scores["tg:1"] = 1000;
        return 0;
    });

    CHECK(command_is_for_bot("/burn 400"));
    CHECK(command_dispatch(alice, "/burn 0")->contains("maggiore di zero"));
    CHECK(command_dispatch(alice, "/burn 1001")->contains("hai solo 1000 palle a disposizione"));
    CHECK(conquister_user(storage, "Alice", RaidTargetKind::telegram)->score == 1000);

    CHECK(command_dispatch(alice, "/burn 400") == "Alice hai bruciato 400 palle: sono uscite dal gioco. Te ne restano 600.");
    CHECK(conquister_user(storage, "Alice", RaidTargetKind::telegram)->score == 600);
    /* Brought to the place, however it is written, they stay where they are; taking it is still a claim. */
    CHECK(command_dispatch(alice, "We @TheConquister37 400") ==
          "Alice a @TheConquister37 non si portano palle: per bruciarle scrivi /burn 400.");
    CHECK(command_dispatch(alice, "We theconquister37 100")->contains("non si portano palle"));
    CHECK(conquister_user(storage, "Alice", RaidTargetKind::telegram)->score == 600);
    CHECK(command_dispatch(alice, "We @TheConquister37")->contains("Alice"));
}

TEST_CASE("We with a number for somebody else sends the palle to them") {
    const TestPaths paths{"gift-command-test"};
    AppConfig config;
    config.starter_balloon = false;
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
    CHECK(command_dispatch(alice, "We @Bob 1001")->contains("hai solo 1000 palle a disposizione"));
    CHECK(conquister_user(storage, "Alice", RaidTargetKind::telegram)->score == 1000);

    const std::string leaving = command_dispatch(alice, "We @Bob 400").value_or("");
    CHECK(leaving.contains("Alice parti per Bob con 400 palle da consegnare"));
    CHECK(leaving.contains("Casa tua resta scoperta"));
    CHECK(conquister_user(storage, "Alice", RaidTargetKind::telegram)->score == 600);
    /* Handed over only when he gets there, not when he sets off. */
    CHECK(conquister_user(storage, "Bob", RaidTargetKind::telegram)->score == 50);
}

TEST_CASE("a quote about what the owner has banned is turned away") {
    const TestPaths paths{"banned-quote-test"};
    AppConfig config;
    config.starter_balloon = false;
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
    config.starter_balloon = false;
    config.conquister_path = paths.conquister;
    config.quotes_path = paths.quotes;
    config.furniture_cost = 1000;
    Storage storage{config.conquister_path, config.quotes_path};

    const CommandContext context{
        .storage = storage,
        .config = config,
        .user_id = 1,
        .username = "alice",
        .claims_allowed = true,
        .owner = false,
    };

    const auto reply = [&](std::string_view text) { return command_dispatch(context, text).value_or(""); };

    /* No slot named: one that works on her goes on her, and the reply already shows her name dressed. */
    CHECK(reply("We @alice 🎈").starts_with("🎈 alice hai speso 1000 palle: 🎈 ora è con te. Con te difende te"));
    /* The emoji first, then the slot of the house: the ones before it stay holes. */
    const std::string third = reply("We @alice 🍕 3");
    CHECK(third.starts_with("🎈 alice hai speso "));
    CHECK(third.ends_with(": 🍕 in casa nel posto 3."));
    CHECK(reply("We @alice 🐟 3").ends_with(": 🐟 in casa nel posto 3, al posto di 🍕."));

    /* Anything else is turned away without a charge. */
    const std::int64_t before = conquister_user(storage, "alice")->score;
    CHECK_FALSE(command_is_for_bot("We @alice ciao"));
    CHECK(reply("We @alice 🍕🎈 3") == "alice una emoji per volta.");
    CHECK(reply("We @alice 🍕 11") == "alice i posti della casa vanno da 1 a 10: nessun addebito.");
    CHECK(reply("We @alice 🍕 0") == "alice i posti della casa vanno da 1 a 10: nessun addebito.");
    CHECK(reply("We @alice 🐟 3") == "alice nel posto 3 c'è già 🐟: nessun addebito.");
    CHECK(conquister_user(storage, "alice")->score == before);

    /* The leaderboard shows the dressed name, and whoever bought nothing stays bare. */
    const std::optional<std::string> board = command_dispatch(context, "/leaderboard");
    REQUIRE(board);
    CHECK(board->contains("🎈 alice —"));
    CHECK(board->contains("bob —"));

    /* And taking the seat, too. */
    const std::optional<std::string> claimed = command_dispatch(context, conquister_trigger);
    REQUIRE(claimed);
    CHECK(claimed->contains("alice sei in"));

    /* From the place the purchase takes her home first; a keycap is an emoji like any other and fills
       the first hole of the house. */
    const std::string keycap = reply("We @alice 3️⃣");
    CHECK(keycap.starts_with("alice torni da @TheConquister37 in @alice con "));
    CHECK(keycap.contains("\n🎈 alice hai speso "));
    CHECK(keycap.ends_with(": 3️⃣ in casa nel posto 1."));
    CHECK_FALSE(conquister_user(storage, "alice")->in_conquister);
}

TEST_CASE("the owner turns the prices off for himself, not for everyone") {
    const TestPaths paths{"debug-test"};
    {
        std::ofstream file{paths.conquister, std::ios::binary};
        file << R"({"current":null,"scores":{"alice":500,"norelec":500},"quotes_added":{},)"
                R"("telegram_ids":{"alice":1,"norelec":2}})";
    }
    AppConfig config;
    config.starter_balloon = false;
    config.conquister_path = paths.conquister;
    config.quotes_path = paths.quotes;
    config.furniture_cost = 1000;
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

    /* Five hundred palle do not buy an emoji that costs a thousand. */
    const std::optional<std::string> broke = command_dispatch(player, "We @alice 🍕");
    REQUIRE(broke);
    CHECK(broke->contains("ti servono"));

    /* The owner turns it on for himself: he buys for nothing. */
    const std::optional<std::string> on = command_dispatch(owner, "/debug 1");
    REQUIRE(on);
    CHECK(on->contains("per te"));
    CHECK(debug_on(storage, "norelec"));
    const std::optional<std::string> bought = command_dispatch(owner, "We @norelec 🍕");
    REQUIRE(bought);
    CHECK(bought->contains("hai speso 0 palle"));
    CHECK(conquister_user(storage, "norelec")->score == 500);

    /* Everybody else goes on paying as before. */
    CHECK_FALSE(debug_on(storage, "alice"));
    const std::optional<std::string> still_broke = command_dispatch(player, "We @alice 🍕");
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

TEST_CASE("everything costs its list price, however rich the group is") {
    const TestPaths paths{"prices-test"};
    {
        std::ofstream file{paths.conquister, std::ios::binary};
        file << R"({"current":null,"scores":{"a":100,"b":5000,"c":10000,"d":40000,"e":900000},)"
                R"("quotes_added":{},"telegram_ids":{"d":1}})";
    }
    AppConfig config;
    config.starter_balloon = false;
    config.conquister_path = paths.conquister;
    config.quotes_path = paths.quotes;
    config.furniture_cost = 1000;
    config.quote_cost = 1000;
    Storage storage{config.conquister_path, config.quotes_path};
    const CommandContext context{.storage = storage, .config = config, .user_id = 1, .username = "d"};

    REQUIRE(command_dispatch(context, "We @d 🍕"));
    CHECK(conquister_user(storage, "d")->score == 39000);
    REQUIRE(command_dispatch(context, "/addquote una citazione qualunque"));
    CHECK(conquister_user(storage, "d")->score == 38000);
}

TEST_CASE("/avventura is another way to write We @TheConquister37") {
    const TestPaths paths{"adventure-command-test"};
    AppConfig config;
    config.starter_balloon = false;
    config.conquister_path = paths.conquister;
    config.quotes_path = paths.quotes;
    Storage storage{config.conquister_path, config.quotes_path};
    const CommandContext alice{.storage = storage, .config = config, .user_id = 1, .username = "Alice",
                               .claims_allowed = true};

    CHECK(command_is_for_bot("/avventura"));
    CHECK(command_is_for_bot("/Avventura 💩"));
    CHECK(command_dispatch(alice, "/avventura").value_or("").contains("Alice sei in @TheConquister37!"));
    /* Whatever follows it follows the place, as it would after We @TheConquister37. */
    CHECK(command_dispatch(alice, "/avventura 💩") == command_dispatch(alice, "We @TheConquister37 💩"));
}

TEST_CASE("/buy is another way to write We yourname emoji, on Telegram and on IRC") {
    const TestPaths paths{"buy-command-test"};
    AppConfig config;
    config.starter_balloon = false;
    config.conquister_path = paths.conquister;
    config.quotes_path = paths.quotes;
    Storage storage{config.conquister_path, config.quotes_path};
    const CommandContext alice{.storage = storage, .config = config, .user_id = 1, .username = "Alice"};
    const CommandContext kio{.storage = storage, .config = config, .user_id = 0, .username = "Kio"};
    REQUIRE(command_dispatch(alice, "/profile"));
    REQUIRE(command_dispatch(kio, "/profile"));
    storage.transaction([](StorageSession &session) {
        session.state().scores["tg:1"] = 100'000;
        session.state().scores["irc:kio"] = 100'000;
        return 0;
    });

    CHECK(command_is_for_bot("/buy 🍕"));
    CHECK(command_dispatch(alice, "/buy").value_or("").starts_with("Uso: /buy <emoji> [posto]"));
    CHECK(command_dispatch(alice, "/buy 🍕").value_or("").contains("hai speso"));
    CHECK(command_dispatch(alice, "/buy 🍩 3").value_or("").contains("hai speso"));
    CHECK(house_of(storage, "tg:1") == "🍕[]🍩");
    CHECK(command_dispatch(kio, "/buy 🍕").value_or("").contains("hai speso"));
    CHECK(house_of(storage, "irc:kio") == "🍕");
}

TEST_CASE("/raid robs, /give makes a present of anything, /throw lands what is thrown") {
    const TestPaths paths{"verbs-test"};
    AppConfig config;
    config.starter_balloon = false;
    config.conquister_path = paths.conquister;
    config.quotes_path = paths.quotes;
    Storage storage{config.conquister_path, config.quotes_path};
    const CommandContext alice{.storage = storage, .config = config, .user_id = 1, .username = "Alice"};
    const CommandContext bob{.storage = storage, .config = config, .user_id = 2, .username = "Bob"};
    REQUIRE(command_dispatch(alice, "/profile"));
    REQUIRE(command_dispatch(bob, "/profile"));
    storage.transaction([](StorageSession &session) {
        session.state().scores["tg:1"] = 1000;
        session.state().scores["tg:2"] = 1000;
        session.state().furniture["tg:1"] = "💣🍕💣";
        session.state().furniture["tg:2"] = "🍕";
        return 0;
    });
    const auto home_again = [&storage] {
        static_cast<void>(raid_due(storage, std::int64_t{4'000'000'000}, RaidRules{}));
    };

    for (const std::string_view verb : {"/raid", "/give", "/throw"}) {
        CHECK(command_is_for_bot(std::format("{} @Bob 🍕", verb)));
        CHECK(command_dispatch(alice, verb).value_or("").starts_with(std::format("Uso: {} <giocatore>", verb)));
    }
    /* Each verb holds to its meaning. */
    CHECK(command_dispatch(alice, "/raid @Bob 🍕").value_or("").starts_with("Uso: /raid"));
    CHECK(command_dispatch(alice, "/give @Bob").value_or("").starts_with("Uso: /give"));
    CHECK(command_dispatch(alice, "/throw @Bob").value_or("").starts_with("Uso: /throw"));
    CHECK(command_dispatch(alice, "/raid @Alice") == "Alice non puoi razziare te stesso.");
    CHECK(command_dispatch(alice, "/give @Alice 🍕") == "Alice non puoi regalare a te stesso.");
    CHECK(command_dispatch(alice, "/throw @Alice 💣") == "Alice non puoi lanciare a te stesso.");
    CHECK(command_dispatch(alice, "/throw @Bob 🍕") == "Alice 🍕 non si lancia: per regalarla scrivi /give @Bob 🍕.");
    CHECK(command_dispatch(alice, "/give @TheConquister37 500").value_or("").starts_with(
        "Alice a @TheConquister37 non si portano palle: per bruciarle scrivi /burn 500."));

    /* Given, the 💣 arrives as it is and goes in his house. */
    CHECK(command_dispatch(alice, "/give @Bob 💣").value_or("").starts_with("Alice parti per Bob con 💣 da regalare"));
    home_again();
    CHECK(house_of(storage, "tg:2") == "🍕💣");
    /* Thrown, the other one goes off. */
    CHECK(command_dispatch(alice, "/throw @Bob 💣").value_or("").starts_with("Alice parti per Bob con 💣 da consegnare"));
    home_again();
    CHECK(house_of(storage, "tg:2") != "🍕💣💣");
    /* And palle are given as before. */
    CHECK(command_dispatch(alice, "/give @Bob 500").value_or("").starts_with("Alice parti per Bob con 500 palle"));
    home_again();
    CHECK(command_dispatch(alice, "/raid @Bob").value_or("").starts_with("Alice parti per Bob:"));
}

TEST_CASE("/burn takes emoji and palle out of the game, and what is thrown hits nobody") {
    const TestPaths paths{"burn-command-test"};
    AppConfig config;
    config.starter_balloon = false;
    config.conquister_path = paths.conquister;
    config.quotes_path = paths.quotes;
    Storage storage{config.conquister_path, config.quotes_path};
    const CommandContext alice{.storage = storage, .config = config, .user_id = 1, .username = "Alice"};
    const CommandContext bob{.storage = storage, .config = config, .user_id = 2, .username = "Bob"};
    REQUIRE(command_dispatch(alice, "/profile"));
    REQUIRE(command_dispatch(bob, "/profile"));
    storage.transaction([](StorageSession &session) {
        session.state().scores["tg:1"] = 1000;
        session.state().furniture["tg:1"] = "🍕☢️💣";
        session.state().equipped["tg:2"] = "⚡";
        return 0;
    });
    /* Bob holds the place: a 💣 thrown there would go off on him. */
    REQUIRE(command_dispatch(bob, "We @TheConquister37").value_or("").contains("@TheConquister37"));

    CHECK(command_is_for_bot("/burn 🍕"));
    CHECK(command_dispatch(alice, "/burn").value_or("").starts_with("Uso: /burn <emoji o palle>"));
    CHECK(command_dispatch(alice, "/burn 🍕") == "Alice hai bruciato 🍕: è uscita dal gioco.");
    CHECK(command_dispatch(alice, "/burn 💣") == "Alice hai bruciato 💣: è uscita dal gioco.");
    CHECK(furniture_all(storage).at("tg:2") == "⚡");
    /* Not even a ☢️ starts the game over. */
    CHECK(command_dispatch(alice, "/burn ☢️") == "Alice hai bruciato ☢️: è uscita dal gioco.");
    CHECK(furniture_all(storage).at("tg:2") == "⚡");
    CHECK(command_dispatch(alice, "/burn 100").value_or("").starts_with("Alice hai bruciato 100 palle: sono uscite dal gioco."));
    CHECK(command_dispatch(alice, "/burn 🍩") == "Alice non hai 🍩, né con te né in casa.");
    storage.transaction([](StorageSession &session) {
        session.state().furniture["tg:1"] = "👧";
        session.state().children.push_back(
            Child{.owner = "tg:1", .slot = 0, .male = false, .born = 0, .paid = 0, .courted = false});
        return 0;
    });
    CHECK(command_dispatch(alice, "/burn 👧") ==
          "Alice i bambini non si bruciano: 👧 resta in casa finché non se ne va da solo.");
}

TEST_CASE("/back takes him home, /move swaps two slots and nothing else") {
    const TestPaths paths{"home-move-test"};
    AppConfig config;
    config.starter_balloon = false;
    config.conquister_path = paths.conquister;
    config.quotes_path = paths.quotes;
    Storage storage{config.conquister_path, config.quotes_path};
    const CommandContext alice{.storage = storage, .config = config, .user_id = 1, .username = "Alice"};
    const CommandContext bob{.storage = storage, .config = config, .user_id = 0, .username = "Bob"};
    REQUIRE(command_dispatch(alice, "/profile"));
    storage.transaction([](StorageSession &session) {
        session.state().scores["tg:1"] = 1000;
        session.state().furniture["tg:1"] = "🍕🍩";
        return 0;
    });

    CHECK(command_is_for_bot("/back"));
    CHECK(command_is_for_bot("/move 1 2"));
    CHECK(command_dispatch(alice, "/back") == "Alice sei già in @Alice!");
    REQUIRE(command_dispatch(alice, "We @TheConquister37").value_or("").contains("@TheConquister37"));
    CHECK(command_dispatch(alice, "/back").value_or("").starts_with("Alice torni da @TheConquister37 in @Alice"));
    CHECK(command_dispatch(alice, "/move 1 2") == "Alice hai scambiato 🍕 e 🍩: ora 🍕 è nel posto 2 e 🍩 nel posto 1. Casa: 🍩🍕");
    /* An emoji after /move is never bought. */
    CHECK(command_dispatch(alice, "/move 🍔 3").value_or("").starts_with("Uso: /move <da> <a>"));
    CHECK(command_dispatch(alice, "/move").value_or("").starts_with("Uso: /move <da> <a>"));
    CHECK(house_of(storage, "tg:1") == "🍩🍕");
    CHECK(command_dispatch(bob, "/move").value_or("").starts_with("Uso: !move <da> <a>"));
}

TEST_CASE("/take puts an emoji on him, /store back in the house, and both tell what it is worth") {
    const TestPaths paths{"take-store-test"};
    AppConfig config;
    config.starter_balloon = false;
    config.conquister_path = paths.conquister;
    config.quotes_path = paths.quotes;
    config.lightning_percent = 10;
    config.hen_per_minute = 1;
    config.ninja_percent = 40;
    Storage storage{config.conquister_path, config.quotes_path};
    const CommandContext alice{.storage = storage, .config = config, .user_id = 1, .username = "Alice"};
    const CommandContext kio{.storage = storage, .config = config, .user_id = 0, .username = "Kio"};
    REQUIRE(command_dispatch(alice, "/profile"));
    REQUIRE(command_dispatch(kio, "/profile"));
    storage.transaction([](StorageSession &session) {
        session.state().furniture["tg:1"] = "⚡🐔🐔🍕💣🥷🥷🥷👧";
        session.state().equipped["tg:1"] = "⚡⚡🦞[]🦞";
        session.state().children.push_back(
            Child{.owner = "tg:1", .slot = 8, .male = false, .born = 0, .paid = 0, .courted = false});
        return 0;
    });

    CHECK(command_is_for_bot("/take ⚡"));
    CHECK(command_is_for_bot("/store ⚡"));
    CHECK(command_dispatch(alice, "/take").value_or("").starts_with("Uso: /take <emoji>"));
    CHECK(command_dispatch(kio, "/store").value_or("").starts_with("Uso: !store <emoji>"));
    CHECK(command_dispatch(alice, "/take ⚡") ==
          "⚡⚡🦞⚡🦞 Alice ⚡ ora è con te: ora ne hai 3 con te: +30% di palle in @TheConquister37.");
    CHECK(command_dispatch(alice, "/take 🍕") ==
          "Alice hai già 5 emoji con te (⚡⚡🦞⚡🦞): scrivi /take 🍕 <emoji> per mettere 🍕 al posto di una di quelle, "
          "che torna in casa.");
    CHECK(command_dispatch(alice, "/take 🥷 ⚡") ==
          "🥷⚡🦞⚡🦞 Alice 🥷 ora è con te al posto di ⚡, che torna in casa: ora ne hai 1 con te: 40% di passare "
          "oltre 🎈 e 🐶 senza toccarli.");
    CHECK(command_dispatch(alice, "/take 🥷 ⚡").value_or("").ends_with("ora ne hai 2 con te: 80% di passare oltre 🎈 e "
                                                                      "🐶 senza toccarli."));
    CHECK(command_dispatch(alice, "/take 🥷🦞").value_or("").ends_with(
        "ora ne hai 3 con te: 100%, il massimo di passare oltre 🎈 e 🐶 senza toccarli."));
    CHECK(command_dispatch(alice, "/store 🦞") ==
          "🥷🥷🥷⚡ Alice 🦞 ora è in casa: non ne hai più con te, nessun bonus.");
    CHECK(command_dispatch(alice, "/take 🐔") ==
          "🥷🥷🥷⚡🐔 Alice 🐔 ora è con te: con te non fa niente, funziona solo in casa, dove ne hai ancora 1: fanno "
          "60 palle all'ora.");
    CHECK(command_dispatch(alice, "/store 🐔") == "🥷🥷🥷⚡ Alice 🐔 ora è in casa: ora ne hai 2 in casa: fanno 120 "
                                                  "palle all'ora.");
    CHECK(command_dispatch(alice, "/take 💣").value_or("").ends_with(
        "💣 ora è con te: non dà nessun bonus: quando la lanci con /throw parte da qui."));
    CHECK(command_dispatch(alice, "/take 👧") == "Alice i bambini restano in casa.");
    CHECK(command_dispatch(alice, "/take 🎺") == "Alice non hai 🎺 in casa.");
    CHECK(command_dispatch(alice, "/take 💣") == "Alice 💣 è già con te.");
    CHECK(command_dispatch(alice, "/store 🎺") == "Alice non hai 🎺 con te.");
    CHECK(command_dispatch(alice, "/take 🍕 🎺") == "Alice non hai 🎺 con te.");
    CHECK(command_dispatch(alice, "/take 🍕 🍕 🍕").value_or("").starts_with("Uso: /take"));

    /* Not from the road. */
    REQUIRE(command_dispatch(alice, "/raid Kio").value_or("").starts_with("Alice parti per Kio"));
    CHECK(command_dispatch(alice, "/take ⚡") ==
          "Alice sei in viaggio: le emoji si prendono da casa tua. Per tornare indietro scrivi /back.");
}

TEST_CASE("/house shows the house slot by slot, with its numbers") {
    const TestPaths paths{"house-command-test"};
    AppConfig config;
    config.starter_balloon = false;
    config.conquister_path = paths.conquister;
    config.quotes_path = paths.quotes;
    Storage storage{config.conquister_path, config.quotes_path};
    const CommandContext alice{.storage = storage, .config = config, .user_id = 1, .username = "Alice"};
    const CommandContext kio{.storage = storage, .config = config, .user_id = 0, .username = "Kio"};
    REQUIRE(command_dispatch(alice, "/profile"));
    REQUIRE(command_dispatch(kio, "/profile"));
    storage.transaction([](StorageSession &session) {
        session.state().furniture["tg:1"] = "👶[][]🧂👶";
        session.state().equipped["tg:1"] = "🎈⚡";
        return 0;
    });

    CHECK(command_is_for_bot("/house"));
    CHECK(command_dispatch(alice, "/house") ==
          "Casa di Alice (3/10):\n1 👶  2 ·  3 ·  4 🧂  5 👶  6 ·  7 ·  8 ·  9 ·  10 ·");
    /* Somebody else's, named as on that platform. */
    CHECK(command_dispatch(kio, "/house @Alice") == command_dispatch(alice, "/house"));
    CHECK(command_dispatch(alice, "/house Kio") ==
          "Casa di Kio (0/10):\n1 ·  2 ·  3 ·  4 ·  5 ·  6 ·  7 ·  8 ·  9 ·  10 ·");
    CHECK(command_dispatch(alice, "/house @Nessuno") == "Alice non conosco nessun giocatore di nome @Nessuno.");
    /* /home is gone: it is not a command any more. */
    CHECK_FALSE(command_is_for_bot("/home"));
    CHECK_FALSE(command_dispatch(alice, "/home").has_value());
}
