#ifdef NDEBUG
#undef NDEBUG
#endif

#include "arena.h"

#include <assert.h>
#include <stdalign.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

int main(void) {
    Arena arena = {0};
    char *small = arena_alloc(&arena, 3U);
    assert(small != NULL && small[0] == '\0' && small[2] == '\0');

    for (size_t index = 0U; index < 1000U; ++index) {
        uint64_t *number = arena_alloc(&arena, sizeof(*number));
        assert(number != NULL && *number == 0U);
        assert((uintptr_t)number % alignof(max_align_t) == 0U);
        *number = index;
    }

    unsigned char *large = arena_alloc(&arena, 100000U);
    assert(large != NULL && large[99999] == 0U);
    memset(large, 0xff, 100000U);

    char *copy = arena_strdup(&arena, "citazione");
    assert(copy != NULL && strcmp(copy, "citazione") == 0);
    assert(arena_strdup(&arena, NULL) == NULL);
    assert(arena_alloc(&arena, SIZE_MAX) == NULL);

    arena_free(&arena);
    assert(arena.blocks == NULL);
    assert(arena_strdup(&arena, "di nuovo") != NULL);
    arena_free(&arena);

    puts("arena tests: ok");
    return 0;
}
