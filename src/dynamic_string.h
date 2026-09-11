#ifndef NORELECBOT_DYNAMIC_STRING_H
#define NORELECBOT_DYNAMIC_STRING_H

#include <stdbool.h>
#include <stddef.h>

typedef struct {
    char *data;
    size_t length;
    size_t capacity;
} DynamicString;

bool dynamic_string_init(DynamicString *string, size_t initial_capacity);
void dynamic_string_free(DynamicString *string);
void dynamic_string_reset(DynamicString *string);
bool dynamic_string_append(DynamicString *string, const char *text);
bool dynamic_string_append_n(DynamicString *string, const char *text, size_t length);
bool dynamic_string_appendf(DynamicString *string, const char *format, ...);

#endif
