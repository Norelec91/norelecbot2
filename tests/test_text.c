#ifdef NDEBUG
#undef NDEBUG
#endif

#include "text.h"

#include <assert.h>
#include <stdio.h>
#include <string.h>

int main(void) {
    assert(text_equals_ignore_case("Lord_Possum", "lord_POSSUM"));
    assert(!text_equals_ignore_case("alice", "alic"));
    assert(!text_equals_ignore_case("alice", "bob"));

    char padded[] = "  ciao mondo \t\n";
    const char *trimmed = text_trim(padded);
    assert(strcmp(trimmed, "ciao mondo") == 0);
    char blank[] = " \t ";
    trimmed = text_trim(blank);
    assert(*trimmed == '\0');

    char small[4];
    bool copied = text_copy(small, sizeof(small), "abc");
    assert(copied && strcmp(small, "abc") == 0);
    copied = text_copy(small, sizeof(small), "abcd");
    assert(!copied);

    int64_t value = 0;
    bool parsed = text_parse_int64(" 42 ", &value);
    assert(parsed && value == 42);
    parsed = text_parse_int64("-7", &value);
    assert(parsed && value == -7);
    const char *const invalid[] = {"", "  ", "12a", "9223372036854775808"};
    for (size_t index = 0U; index < sizeof(invalid) / sizeof(invalid[0]); ++index) {
        parsed = text_parse_int64(invalid[index], &value);
        assert(!parsed);
    }

    assert(text_utf8_prefix_bytes("abc", 2U) == 2U);
    assert(text_utf8_prefix_bytes("citt\xC3\xA0", 5U) == 6U);
    assert(text_utf8_prefix_bytes("citt\xC3\xA0", 4U) == 4U);
    assert(text_utf8_prefix_bytes("\xC3", 1U) == 1U);

    puts("text tests: ok");
    return 0;
}
