#include "arena.h"

#include <stdalign.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#define ARENA_BLOCK_SIZE 4096U

struct ArenaBlock {
    ArenaBlock *next;
    size_t used;
    size_t capacity;
    max_align_t data[];
};

void *arena_alloc(Arena *arena, size_t size) {
    const size_t alignment = alignof(max_align_t);
    if (size == 0U) {
        size = 1U;
    }
    if (size > SIZE_MAX - offsetof(ArenaBlock, data) - alignment) {
        return NULL;
    }
    size = (size + alignment - 1U) / alignment * alignment;

    ArenaBlock *block = arena->blocks;
    if (block == NULL || block->capacity - block->used < size) {
        size_t capacity = size > ARENA_BLOCK_SIZE ? size : ARENA_BLOCK_SIZE;
        block = malloc(offsetof(ArenaBlock, data) + capacity);
        if (block == NULL) {
            return NULL;
        }
        block->next = arena->blocks;
        block->used = 0U;
        block->capacity = capacity;
        arena->blocks = block;
    }
    void *memory = (unsigned char *)block->data + block->used;
    block->used += size;
    memset(memory, 0, size);
    return memory;
}

char *arena_strdup(Arena *arena, const char *text) {
    if (text == NULL) {
        return NULL;
    }
    size_t length = strlen(text);
    char *copy = arena_alloc(arena, length + 1U);
    if (copy != NULL) {
        memcpy(copy, text, length + 1U);
    }
    return copy;
}

void arena_free(Arena *arena) {
    while (arena->blocks != NULL) {
        ArenaBlock *next = arena->blocks->next;
        free(arena->blocks);
        arena->blocks = next;
    }
}
