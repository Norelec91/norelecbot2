#include "dynamic_string.h"

#include <stdint.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define DYNAMIC_STRING_DEFAULT_CAPACITY 64U

static bool dynamic_string_reserve(DynamicString *string, size_t required) {
    if (required <= string->capacity) {
        return true;
    }
    size_t capacity = string->capacity == 0U
        ? DYNAMIC_STRING_DEFAULT_CAPACITY
        : string->capacity;
    while (capacity < required) {
        if (capacity > SIZE_MAX / 2U) {
            return false;
        }
        capacity *= 2U;
    }
    char *data = realloc(string->data, capacity);
    if (data == NULL) {
        return false;
    }
    string->data = data;
    string->capacity = capacity;
    return true;
}

bool dynamic_string_init(DynamicString *string, size_t initial_capacity) {
    if (string == NULL) {
        return false;
    }
    *string = (DynamicString){0};
    if (!dynamic_string_reserve(string, initial_capacity > 0U ? initial_capacity : 1U)) {
        return false;
    }
    string->data[0] = '\0';
    return true;
}

void dynamic_string_free(DynamicString *string) {
    if (string == NULL) {
        return;
    }
    free(string->data);
    *string = (DynamicString){0};
}

void dynamic_string_reset(DynamicString *string) {
    if (string == NULL || string->data == NULL) {
        return;
    }
    string->length = 0U;
    string->data[0] = '\0';
}

bool dynamic_string_append_n(DynamicString *string, const char *text, size_t length) {
    if (string == NULL || text == NULL || length > SIZE_MAX - string->length - 1U) {
        return false;
    }
    size_t required = string->length + length + 1U;
    if (!dynamic_string_reserve(string, required)) {
        return false;
    }
    memcpy(string->data + string->length, text, length);
    string->length += length;
    string->data[string->length] = '\0';
    return true;
}

bool dynamic_string_append(DynamicString *string, const char *text) {
    return text != NULL && dynamic_string_append_n(string, text, strlen(text));
}

bool dynamic_string_appendf(DynamicString *string, const char *format, ...) {
    if (string == NULL || format == NULL) {
        return false;
    }
    va_list arguments;
    va_start(arguments, format);
    va_list copy;
    va_copy(copy, arguments);
    int length = vsnprintf(NULL, 0U, format, copy);
    va_end(copy);
    if (length < 0) {
        va_end(arguments);
        return false;
    }
    size_t size = (size_t)length;
    if (size > SIZE_MAX - string->length - 1U ||
        !dynamic_string_reserve(string, string->length + size + 1U)) {
        va_end(arguments);
        return false;
    }
    (void)vsnprintf(string->data + string->length, size + 1U, format, arguments);
    va_end(arguments);
    string->length += size;
    return true;
}

char *string_duplicate(const char *text) {
    if (text == NULL) {
        return NULL;
    }
    size_t length = strlen(text);
    char *copy = malloc(length + 1U);
    if (copy != NULL) {
        memcpy(copy, text, length + 1U);
    }
    return copy;
}
