#include "irc_session.hpp"

#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest/doctest.h>

#include <string>
#include <vector>

using namespace norelecbot;

namespace {

struct Call {
    std::string nick;
    bool owner = false;
    std::string text;
};

struct Fixture {
    std::vector<Call> calls;
    irc::Session session;

    Fixture()
        : session{
              irc::SessionConfig{
                  .nick = "NorelecBot",
                  .user = "norelecbot",
                  .realname = "NorelecBot",
                  .nickserv_password = "segreto",
                  .channel = "#regno",
                  .no_forward_prefix = "\xE2\x80\x8B",
                  .owner_nick = "Norelec",
              },
              [this](std::string_view nick, bool owner, std::string_view text) -> std::optional<std::string> {
                  calls.push_back({std::string{nick}, owner, std::string{text}});
                  return text == "/leaderboard" ? std::optional<std::string>{"Classifica vuota."} : std::nullopt;
              }
          } {}

    std::vector<std::string> feed(std::string_view line, std::int64_t now = 1000) {
        const auto message = irc::parse(line);
        REQUIRE(message);
        return session.handle(*message, now);
    }
};

bool contains(const std::vector<std::string> &lines, std::string_view wanted) {
    return std::ranges::any_of(lines, [wanted](const std::string &line) { return line == wanted; });
}

}

TEST_CASE("the session logs in, identifies and joins") {
    Fixture fixture;
    const auto start = fixture.session.connected();
    REQUIRE(start.size() == 2);
    CHECK(start[0] == "NICK NorelecBot\r\n");
    CHECK(start[1] == "USER norelecbot 0 * :NorelecBot\r\n");

    CHECK(fixture.feed("PING :raccooncity.azzurra.chat")[0] == "PONG :raccooncity.azzurra.chat\r\n");

    const auto welcome = fixture.feed(":raccooncity.azzurra.chat 001 NorelecBot :Welcome to the Azzurra IRC Network");
    REQUIRE(welcome.size() == 1);
    CHECK(welcome[0] == "PRIVMSG NickServ :IDENTIFY segreto\r\n");
    CHECK_FALSE(fixture.session.joined());

    const auto motd = fixture.feed(":raccooncity.azzurra.chat 376 NorelecBot :End of /MOTD command.");
    REQUIRE(motd.size() == 1);
    CHECK(motd[0] == "JOIN #regno\r\n");
    CHECK(fixture.feed(":NorelecBot!~n@host JOIN :#regno").empty());
    CHECK(fixture.session.joined());

    SUBCASE("a taken nick is retried with an underscore") {
        const auto taken = fixture.feed(":raccooncity.azzurra.chat 433 * NorelecBot :Nickname is already in use.");
        REQUIRE(taken.size() == 1);
        CHECK(taken[0] == "NICK NorelecBot_\r\n");
        CHECK(fixture.session.nick() == "NorelecBot_");
    }

    SUBCASE("a kick is answered with a new join") {
        const auto kicked = fixture.feed(":Sonic!~s@host KICK #regno NorelecBot :via");
        CHECK_FALSE(fixture.session.joined());
        REQUIRE(kicked.size() == 1);
        CHECK(kicked[0] == "JOIN #regno\r\n");
    }
}

TEST_CASE("only a nick identified with NickServ plays") {
    Fixture fixture;
    static_cast<void>(fixture.session.connected());

    const auto asked = fixture.feed(":Marco189!~m@host PRIVMSG #regno :We @TheConquister37", 1000);
    REQUIRE(asked.size() == 1);
    CHECK(asked[0] == "WHOIS Marco189\r\n");
    CHECK(fixture.calls.empty());

    SUBCASE("numeric 307 lets the queued command through") {
        CHECK(fixture.feed(":server 307 NorelecBot Marco189 :has identified for this nick", 1001).empty());
        const auto released = fixture.feed(":server 318 NorelecBot Marco189 :End of /WHOIS list.", 1001);
        CHECK(released.empty());
        REQUIRE(fixture.calls.size() == 1);
        CHECK(fixture.calls[0].nick == "Marco189");
        CHECK(fixture.calls[0].text == "We @TheConquister37");
        CHECK_FALSE(fixture.calls[0].owner);

        SUBCASE("the answer is cached, so the next command costs no WHOIS") {
            const auto again = fixture.feed(":Marco189!~m@host PRIVMSG #regno :!leaderboard", 1100);
            REQUIRE(again.size() == 1);
            /* An answer born on IRC is not marked: the bridge must carry it to Telegram. */
            CHECK(again[0] == "PRIVMSG #regno :Classifica vuota.\r\n");
            REQUIRE(fixture.calls.size() == 2);
            CHECK(fixture.calls[1].text == "/leaderboard");
        }

        SUBCASE("a nick change throws the answer away") {
            CHECK(fixture.feed(":Marco189!~m@host NICK :Marco189_", 1100).empty());
            const auto after = fixture.feed(":Marco189!~m@host PRIVMSG #regno :We @TheConquister37", 1100);
            REQUIRE(after.size() == 1);
            CHECK(after[0] == "WHOIS Marco189\r\n");
        }

        SUBCASE("the answer is asked again once it expires") {
            fixture.session.tick(1400);
            const auto after = fixture.feed(":Marco189!~m@host PRIVMSG #regno :We @TheConquister37", 1400);
            REQUIRE(after.size() == 1);
            CHECK(after[0] == "WHOIS Marco189\r\n");
        }
    }

    SUBCASE("without 307 the player is told to register, once") {
        const auto refused = fixture.feed(":server 318 NorelecBot Marco189 :End of /WHOIS list.", 1001);
        REQUIRE(refused.size() == 1);
        CHECK(refused[0] ==
              "PRIVMSG #regno :Marco189 devi essere registrato e identificato con NickServ per giocare.\r\n");
        CHECK(fixture.calls.empty());

        CHECK(fixture.feed(":Marco189!~m@host PRIVMSG #regno :We @TheConquister37", 1002).empty());
        CHECK(fixture.calls.empty());

        const auto retry = fixture.feed(":Marco189!~m@host PRIVMSG #regno :We @TheConquister37", 1400);
        REQUIRE(retry.size() == 1);
        CHECK(retry[0] == "WHOIS Marco189\r\n");
        const auto told = fixture.feed(":server 318 NorelecBot Marco189 :End of /WHOIS list.", 1400);
        REQUIRE(told.size() == 1);
        CHECK(told[0].contains("devi essere registrato"));
    }

    SUBCASE("a nick that left before answering is dropped") {
        CHECK(fixture.feed(":server 401 NorelecBot Marco189 :No such nick/channel", 1001).empty());
        CHECK(fixture.feed(":server 318 NorelecBot Marco189 :End of /WHOIS list.", 1001).empty());
        CHECK(fixture.calls.empty());
        const auto retry = fixture.feed(":Marco189!~m@host PRIVMSG #regno :We @TheConquister37", 1002);
        REQUIRE(retry.size() == 1);
        CHECK(retry[0] == "WHOIS Marco189\r\n");
    }

    SUBCASE("a WHOIS that never comes back expires") {
        fixture.session.tick(1011);
        CHECK(fixture.feed(":server 318 NorelecBot Marco189 :End of /WHOIS list.", 1011).empty());
        CHECK(fixture.calls.empty());
        const auto retry = fixture.feed(":Marco189!~m@host PRIVMSG #regno :We @TheConquister37", 1012);
        REQUIRE(retry.size() == 1);
        CHECK(retry[0] == "WHOIS Marco189\r\n");
    }

    SUBCASE("two commands from the same nick share one WHOIS") {
        const auto second = fixture.feed(":Marco189!~m@host PRIVMSG #regno :!leaderboard", 1001);
        CHECK(second.empty());
        CHECK(fixture.feed(":server 307 NorelecBot marco189 :has identified for this nick", 1002).empty());
        const auto released = fixture.feed(":server 318 NorelecBot MARCO189 :End of /WHOIS list.", 1002);
        CHECK(fixture.calls.size() == 2);
        CHECK(contains(released, "PRIVMSG #regno :Classifica vuota.\r\n"));
    }
}

TEST_CASE("messages that are not for the bot cost nothing") {
    Fixture fixture;
    static_cast<void>(fixture.session.connected());
    static_cast<void>(fixture.feed(":raccooncity 001 NorelecBot :Welcome"));

    CHECK(fixture.feed(":Marco189!~m@host PRIVMSG #regno :ciao a tutti").empty());
    CHECK(fixture.feed(":Marco189!~m@host PRIVMSG #altro :We @TheConquister37").empty());
    CHECK(fixture.feed(":Marco189!~m@host PRIVMSG #regno :<Marco189> We @TheConquister37").empty());
    CHECK(fixture.feed(":NorelecBot!~n@host PRIVMSG #regno :We @TheConquister37").empty());
    CHECK(fixture.calls.empty());
}

TEST_CASE("the owner is recognised by nick") {
    Fixture fixture;
    static_cast<void>(fixture.session.connected());
    static_cast<void>(fixture.feed(":norelec!~n@host PRIVMSG #regno :!quotes", 1000));
    static_cast<void>(fixture.feed(":server 307 NorelecBot norelec :has identified for this nick", 1000));
    static_cast<void>(fixture.feed(":server 318 NorelecBot norelec :End of /WHOIS list.", 1000));
    REQUIRE(fixture.calls.size() == 1);
    CHECK(fixture.calls[0].owner);
    CHECK(fixture.calls[0].text == "/quotes");
}

TEST_CASE("a reply too long for one line is split") {
    std::vector<std::string> sent;
    irc::Session session{
        irc::SessionConfig{
            .nick = "bot",
            .user = "bot",
            .realname = "bot",
            .nickserv_password = "",
            .channel = "#regno",
            .no_forward_prefix = "",
            .owner_nick = "",
        },
        [](std::string_view, bool, std::string_view) -> std::optional<std::string> {
            return std::string(500, 'a') + "\n\nseconda riga";
        }
    };
    static_cast<void>(session.connected());
    const auto ask = irc::parse(":Marco189!~m@host PRIVMSG #regno :!leaderboard");
    REQUIRE(ask);
    static_cast<void>(session.handle(*ask, 1000));
    const auto identified = irc::parse(":server 307 bot Marco189 :has identified for this nick");
    const auto done = irc::parse(":server 318 bot Marco189 :End of /WHOIS list.");
    REQUIRE(identified);
    REQUIRE(done);
    static_cast<void>(session.handle(*identified, 1000));
    sent = session.handle(*done, 1000);
    REQUIRE(sent.size() == 2);
    for (const std::string &line : sent) {
        CHECK(line.starts_with("PRIVMSG #regno :"));
        CHECK(line.size() <= irc::max_text_bytes + 20);
    }
    CHECK(sent[1].ends_with(" - seconda riga\r\n"));
}

TEST_CASE("what the bot said on Telegram is repeated in the channel, marked for the bridge") {
    Fixture fixture;
    static_cast<void>(fixture.session.connected());
    const auto lines = fixture.session.announce("Norelec hai cacciato @mifaisonno da @TheConquister37.\nmifaisonno hai guadagnato 1471 palle!");
    REQUIRE(lines.size() == 1);
    /* The message carries the mark that keeps the bridge from sending it back to Telegram. */
    CHECK(lines[0] ==
          "PRIVMSG #regno :\xE2\x80\x8BNorelec hai cacciato @mifaisonno da @TheConquister37. - "
          "mifaisonno hai guadagnato 1471 palle!\r\n");
    CHECK(fixture.calls.empty());
}

