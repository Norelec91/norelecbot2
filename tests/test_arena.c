#ifdef NDEBUG
#undef NDEBUG
#endif

#include "arena.h"

#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

int main(void) {
    Arena arena = {};
    const char *small = arena_alloc(&arena, 3U);
    assert(small != nullptr && small[0] == '\0' && small[2] == '\0');

    for (size_t index = 0U; index < 1000U; ++index) {
        uint64_t *number = arena_alloc(&arena, sizeof(*number));
        assert(number != nullptr && *number == 0U);
        assert((uintptr_t)number % alignof(max_align_t) == 0U);
        *number = index;
    }

    unsigned char *large = arena_alloc(&arena, 100000U);
    assert(large != nullptr && large[99999] == 0U);
    memset(large, 0xff, 100000U);

    const uint32_t *values = arena_alloc_array(&arena, 10U, sizeof(*values));
    assert(values != nullptr && values[9] == 0U);
    assert(arena_alloc_array(&arena, SIZE_MAX, 2U) == nullptr);

    const char *copy = arena_strdup(&arena, "citazione");
    assert(copy != nullptr && strcmp(copy, "citazione") == 0);
    assert(arena_strdup(&arena, nullptr) == nullptr);
    assert(arena_alloc(&arena, SIZE_MAX) == nullptr);

    arena_free(&arena);
    assert(arena.blocks == nullptr);
    assert(arena_strdup(&arena, "di nuovo") != nullptr);
    arena_free(&arena);

    puts("arena tests: ok");
    return 0;
}
