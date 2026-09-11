#include "arena.h"

#include <stdckdint.h>
#include <stdlib.h>
#include <string.h>

static constexpr size_t ARENA_BLOCK_SIZE = 4096;

struct ArenaBlock {
    ArenaBlock *next;
    size_t used;
    size_t capacity;
    max_align_t data[];
};

void *arena_alloc(Arena *arena, size_t size) {
    const size_t alignment = alignof(max_align_t);
    size_t rounded = 0U;
    if (ckd_add(&rounded, size == 0U ? 1U : size, alignment - 1U)) {
        return nullptr;
    }
    rounded -= rounded % alignment;

    ArenaBlock *block = arena->blocks;
    if (block == nullptr || block->capacity - block->used < rounded) {
        size_t capacity = rounded > ARENA_BLOCK_SIZE ? rounded : ARENA_BLOCK_SIZE;
        size_t bytes = 0U;
        if (ckd_add(&bytes, offsetof(ArenaBlock, data), capacity)) {
            return nullptr;
        }
        block = malloc(bytes);
        if (block == nullptr) {
            return nullptr;
        }
        block->next = arena->blocks;
        block->used = 0U;
        block->capacity = capacity;
        arena->blocks = block;
    }
    void *memory = (unsigned char *)block->data + block->used;
    block->used += rounded;
    memset(memory, 0, rounded);
    return memory;
}

void *arena_alloc_array(Arena *arena, size_t count, size_t size) {
    size_t bytes = 0U;
    return ckd_mul(&bytes, count, size) ? nullptr : arena_alloc(arena, bytes);
}

char *arena_strdup(Arena *arena, const char *text) {
    if (text == nullptr) {
        return nullptr;
    }
    size_t length = strlen(text);
    char *copy = arena_alloc(arena, length + 1U);
    if (copy != nullptr) {
        memcpy(copy, text, length + 1U);
    }
    return copy;
}

void arena_free(Arena *arena) {
    while (arena->blocks != nullptr) {
        ArenaBlock *next = arena->blocks->next;
        free(arena->blocks);
        arena->blocks = next;
    }
}
