#include "dynamic_string.h"

#ifdef NDEBUG
#undef NDEBUG
#endif

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int main(void) {
    DynamicString value = {};
    assert(dynamic_string_init(&value, 2U));
    assert(dynamic_string_append(&value, "abc"));
    assert(dynamic_string_appendf(&value, "-%d", 42));
    assert(strcmp(value.data, "abc-42") == 0);
    assert(value.length == 6U);

    dynamic_string_reset(&value);
    assert(dynamic_string_append_n(&value, "abcdef", 3U));
    assert(strcmp(value.data, "abc") == 0);
    dynamic_string_free(&value);

    puts("dynamic string tests: ok");
    return 0;
}
