#include "irc_protocol.hpp"

#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest/doctest.h>

#include <optional>

using namespace norelecbot;

TEST_CASE("the lines Azzurra sends are parsed") {
    auto message = irc::parse(":Marco189!~marco@azzurra.chat PRIVMSG #regno :We @TheConquister37\r\n");
    REQUIRE(message);
    CHECK(message->prefix == "Marco189!~marco@azzurra.chat");
    CHECK(irc::nick_of(message->prefix) == "Marco189");
    CHECK(message->command == "PRIVMSG");
    REQUIRE(message->params.size() == 2);
    CHECK(message->param(0) == "#regno");
    CHECK(message->param(1) == "We @TheConquister37");
    CHECK(message->param(2).empty());

    message = irc::parse(":raccooncity.azzurra.chat 307 nb Sonic :has identified for this nick");
    REQUIRE(message);
    CHECK(message->command == "307");
    CHECK(message->param(1) == "Sonic");

    message = irc::parse(":raccooncity.azzurra.chat 318 nb Sonic :End of /WHOIS list.");
    REQUIRE(message);
    CHECK(message->command == "318");

    message = irc::parse("PING :raccooncity.azzurra.chat");
    REQUIRE(message);
    CHECK(message->prefix.empty());
    CHECK(message->command == "PING");
    CHECK(message->param(0) == "raccooncity.azzurra.chat");

    message = irc::parse("ping  two   :and the rest");
    REQUIRE(message);
    CHECK(message->command == "PING");
    REQUIRE(message->params.size() == 2);
    CHECK(message->param(1) == "and the rest");

    message = irc::parse(":nick!user@host PART #regno :");
    REQUIRE(message);
    REQUIRE(message->params.size() == 2);
    CHECK(message->param(1).empty());
}

TEST_CASE("malformed lines are rejected") {
    CHECK_FALSE(irc::parse(""));
    CHECK_FALSE(irc::parse("\r\n"));
    CHECK_FALSE(irc::parse("   "));
    CHECK_FALSE(irc::parse(":"));
    CHECK_FALSE(irc::parse(":prefix-without-command"));
    CHECK_FALSE(irc::parse(": PRIVMSG #regno :hello"));
}

TEST_CASE("outgoing lines carry the text as the trailing parameter") {
    const auto line = [](std::string_view command,
                         std::initializer_list<std::string_view> params,
                         std::optional<std::string_view> text = std::nullopt) {
        return irc::command_line(command, std::span<const std::string_view>{params.begin(), params.size()}, text);
    };
    CHECK(line("PRIVMSG", {"#regno"}, "ciao a tutti") == "PRIVMSG #regno :ciao a tutti\r\n");
    CHECK(line("PRIVMSG", {"#regno"}, ":-)") == "PRIVMSG #regno ::-)\r\n");
    CHECK(line("PRIVMSG", {"#regno"}, "") == "PRIVMSG #regno :\r\n");
    CHECK(line("WHOIS", {"Sonic"}) == "WHOIS Sonic\r\n");
    CHECK(line("USER", {"norelecbot", "0", "*"}, "NorelecBot") == "USER norelecbot 0 * :NorelecBot\r\n");
    CHECK(line("QUIT", {}, "bye") == "QUIT :bye\r\n");
    CHECK(line("QUIT", {}) == "QUIT\r\n");

    const auto round_trip = irc::parse(line("PRIVMSG", {"#regno"}, "We @TheConquister37"));
    REQUIRE(round_trip);
    CHECK(round_trip->param(0) == "#regno");
    CHECK(round_trip->param(1) == "We @TheConquister37");
}

TEST_CASE("names follow the ascii casemapping the server announces") {
    CHECK(irc::to_lower("Marco189") == "marco189");
    CHECK(irc::same_name("Marco189", "MARCO189"));
    CHECK_FALSE(irc::same_name("Marco189", "Marco18"));
    /* Not a casefolding: accented names must match byte for byte. */
    CHECK_FALSE(irc::same_name("Pippò", "PIPPÒ"));
    CHECK(irc::nick_of("Regno!~regno@host") == "Regno");
    CHECK(irc::nick_of("nick@host") == "nick");
    CHECK(irc::nick_of("raccooncity.azzurra.chat") == "raccooncity.azzurra.chat");
    CHECK(irc::is_channel("#regno"));
    CHECK(irc::is_channel("&local"));
    CHECK_FALSE(irc::is_channel("Marco189"));
    CHECK_FALSE(irc::is_channel(""));
}

TEST_CASE("replies are split into sendable lines") {
    CHECK(irc::split_text("").empty());
    CHECK(irc::split_text("\n\n").empty());

    const auto quote = irc::split_text("\xF0\x9F\x92\xA5 Marco189 hai bucato il palloncino!\n\ncitazione");
    REQUIRE(quote.size() == 2);
    CHECK(quote[0] == "\xF0\x9F\x92\xA5 Marco189 hai bucato il palloncino!");
    CHECK(quote[1] == "citazione");
    CHECK(irc::split_text("una riga\r\naltra riga").size() == 2);

    SUBCASE("a long line breaks on a space") {
        const std::string words = std::string(30, 'a') + " " + std::string(30, 'b');
        const auto lines = irc::split_text(words, 40);
        REQUIRE(lines.size() == 2);
        CHECK(lines[0] == std::string(30, 'a'));
        CHECK(lines[1] == std::string(30, 'b'));
    }

    SUBCASE("a line without spaces is cut at the limit") {
        const auto lines = irc::split_text(std::string(90, 'a'), 40);
        REQUIRE(lines.size() == 3);
        CHECK(lines[0].size() == 40);
        CHECK(lines[2].size() == 10);
    }

    SUBCASE("multi-byte characters are never cut in half") {
        std::string balloons;
        for (int index = 0; index < 60; ++index) {
            balloons += "\xF0\x9F\x8E\x88";
        }
        const auto lines = irc::split_text(balloons, 50);
        std::string joined;
        for (const std::string &line : lines) {
            CHECK(line.size() <= 50);
            CHECK(line.size() % 4 == 0);
            joined += line;
        }
        CHECK(joined == balloons);
    }

    SUBCASE("every line of a real reply fits the line limit") {
        const std::string reply =
            "\xF0\x9F\x8E\x88 Giangiui il palloncino di @Marco189 ha resistito e prendi 5 minuti di penalita. "
            "Ora il palloncino ha il 50% di probabilita di essere bucato.";
        for (const std::string &line : irc::split_text(reply)) {
            CHECK(line.size() <= irc::max_text_bytes);
        }
    }
}
