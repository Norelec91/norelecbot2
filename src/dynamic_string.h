#ifndef NORELECBOT_DYNAMIC_STRING_H
#define NORELECBOT_DYNAMIC_STRING_H

#include "arena.h"

#include <stdbool.h>
#include <stddef.h>

/* The text lives in the arena passed to dynamic_string_init and is released with it. */
typedef struct {
    Arena *arena;
    char *data;
    size_t length;
    size_t capacity;
} DynamicString;

[[nodiscard]] bool dynamic_string_init(DynamicString *string, Arena *arena, size_t initial_capacity);
void dynamic_string_reset(DynamicString *string);
[[nodiscard]] bool dynamic_string_append(DynamicString *string, const char *text);
[[nodiscard]] bool dynamic_string_append_n(DynamicString *string, const char *text, size_t length);
[[nodiscard]] bool dynamic_string_appendf(DynamicString *string, const char *format, ...);

#endif
