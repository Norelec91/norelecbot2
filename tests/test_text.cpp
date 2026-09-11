#ifdef NDEBUG
#undef NDEBUG
#endif

#include "text.hpp"

#include <cassert>
#include <print>
#include <string_view>

int main() {
    namespace text = norelecbot::text;

    assert(text::equals_ignore_case("Lord_Possum", "lord_POSSUM"));
    assert(!text::equals_ignore_case("alice", "alic"));
    assert(!text::equals_ignore_case("alice", "bob"));

    assert(text::trim("  ciao mondo \t\n") == "ciao mondo");
    assert(text::trim(" \t ").empty());

    assert(text::parse_int64(" 42 ") == 42);
    assert(text::parse_int64("-7") == -7);
    assert(text::parse_int64("+5") == 5);
    for (const std::string_view invalid : {"", "  ", "12a", "9223372036854775808", "+-5", "+"}) {
        assert(!text::parse_int64(invalid));
    }

    assert(text::utf8_prefix_bytes("abc", 2) == 2);
    assert(text::utf8_prefix_bytes("citt\xC3\xA0", 5) == 6);
    assert(text::utf8_prefix_bytes("citt\xC3\xA0", 4) == 4);
    assert(text::utf8_prefix_bytes("\xC3", 1) == 1);

    std::println("text tests: ok");
    return 0;
}
