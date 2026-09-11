#ifndef NORELECBOT_ARENA_H
#define NORELECBOT_ARENA_H

#include <stddef.h>

typedef struct ArenaBlock ArenaBlock;

/* Start from {}; allocations are zeroed and released together by arena_free. */
typedef struct {
    ArenaBlock *blocks;
} Arena;

[[nodiscard]] void *arena_alloc(Arena *arena, size_t size);
[[nodiscard]] void *arena_alloc_array(Arena *arena, size_t count, size_t size);
[[nodiscard]] char *arena_strdup(Arena *arena, const char *text);
void arena_free(Arena *arena);

#endif
