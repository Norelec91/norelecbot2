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
