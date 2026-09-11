#include "dynamic_string.h"

#ifdef NDEBUG
#undef NDEBUG
#endif

#include <assert.h>
#include <stdio.h>
#include <string.h>

int main(void) {
    Arena arena = {};
    DynamicString value = {};
    assert(!dynamic_string_init(&value, nullptr, 2U));
    assert(dynamic_string_init(&value, &arena, 2U));
    assert(dynamic_string_append(&value, "abc"));
    assert(dynamic_string_appendf(&value, "-%d", 42));
    assert(strcmp(value.data, "abc-42") == 0);
    assert(value.length == 6U);

    dynamic_string_reset(&value);
    assert(dynamic_string_append_n(&value, "abcdef", 3U));
    assert(strcmp(value.data, "abc") == 0);
    for (int index = 0; index < 100; ++index) {
        assert(dynamic_string_appendf(&value, "%d", index % 10));
    }
    assert(value.length == 103U && strncmp(value.data, "abc0123456789", 13U) == 0);
    arena_free(&arena);

    puts("dynamic string tests: ok");
    return 0;
}
