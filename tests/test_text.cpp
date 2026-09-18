#include "text.hpp"

#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest/doctest.h>

#include <string_view>

namespace text = norelecbot::text;

TEST_CASE("equals_ignore_case ignores case but not length") {
    CHECK(text::equals_ignore_case("Lord_Possum", "lord_POSSUM"));
    CHECK_FALSE(text::equals_ignore_case("alice", "alic"));
    CHECK_FALSE(text::equals_ignore_case("alice", "bob"));
}

TEST_CASE("trim removes surrounding whitespace") {
    CHECK(text::trim("  ciao mondo \t\n") == "ciao mondo");
    CHECK(text::trim(" \t ").empty());
}

TEST_CASE("parse_int64 accepts a whole number and nothing else") {
    CHECK(text::parse_int64(" 42 ") == 42);
    CHECK(text::parse_int64("-7") == -7);
    CHECK(text::parse_int64("+5") == 5);
    for (const std::string_view invalid : {"", "  ", "12a", "9223372036854775808", "+-5", "+"}) {
        CHECK_FALSE(text::parse_int64(invalid));
    }
}

TEST_CASE("utf8_prefix_bytes counts codepoints, not bytes") {
    CHECK(text::utf8_prefix_bytes("abc", 2) == 2);
    CHECK(text::utf8_prefix_bytes("citt\xC3\xA0", 5) == 6);
    CHECK(text::utf8_prefix_bytes("citt\xC3\xA0", 4) == 4);
    CHECK(text::utf8_prefix_bytes("\xC3", 1) == 1);
}

TEST_CASE("a quote does not tag anyone") {
    CHECK(text::strip_mentions("/dai @Decibelg pel top") == "/dai Decibelg pel top");
    CHECK(text::strip_mentions("We @TheConquister37") == "We TheConquister37");
    CHECK(text::strip_mentions("@ultimaora Messer Balocco") == "ultimaora Messer Balocco");
    CHECK(text::strip_mentions("due @tizio e @caio_91 insieme") == "due tizio e caio_91 insieme");

    /* An @ that names nobody stays where it is. */
    CHECK(text::strip_mentions("tizio@posta.example") == "tizio@posta.example");
    CHECK(text::strip_mentions("prezzo @ 5 euro") == "prezzo @ 5 euro");
    CHECK(text::strip_mentions("finisce con una @") == "finisce con una @");
    CHECK(text::strip_mentions("@@doppia") == "doppia");
    CHECK(text::strip_mentions("") == "");
    CHECK(text::strip_mentions("niente da togliere") == "niente da togliere");
}

TEST_CASE("a piece of word is found whatever the spelling") {
    CHECK(text::contains_ignore_case("LA FRODE LA FRODE", "frod"));
    CHECK(text::contains_ignore_case("chi frodava allora", "FROD"));
    CHECK(text::contains_ignore_case("frode", "frode"));
    CHECK_FALSE(text::contains_ignore_case("una citazione", "frod"));
    CHECK_FALSE(text::contains_ignore_case("frod", "frode"));
    CHECK_FALSE(text::contains_ignore_case("qualunque cosa", ""));
    CHECK_FALSE(text::contains_ignore_case("", "frod"));
}

TEST_CASE("punctuation does not hide a word") {
    CHECK(text::squeeze("F.R.O.D.E.") == "frode");
    CHECK(text::squeeze("L A   F R O D E") == "lafrode");
    CHECK(text::squeeze("f-r-o-d-e!") == "frode");
    CHECK(text::squeeze("Perché no?") == "perchéno");
    CHECK(text::squeeze("12 palle") == "12palle");
    CHECK(text::squeeze("") == "");
    CHECK(text::contains_ignore_case(text::squeeze("LA F.R.O.D.E. LA F.R.O.D.E."), "frod"));
}

