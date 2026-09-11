#include "dynamic_string.h"

#include <stdarg.h>
#include <stdckdint.h>
#include <stdio.h>
#include <string.h>

static constexpr size_t DYNAMIC_STRING_DEFAULT_CAPACITY = 64;

static bool dynamic_string_reserve(DynamicString *string, size_t required) {
    if (required <= string->capacity) {
        return true;
    }
    size_t capacity = string->capacity == 0U
        ? DYNAMIC_STRING_DEFAULT_CAPACITY
        : string->capacity;
    while (capacity < required) {
        if (ckd_mul(&capacity, capacity, 2U)) {
            return false;
        }
    }
    char *data = arena_alloc(string->arena, capacity);
    if (data == nullptr) {
        return false;
    }
    if (string->data != nullptr) {
        memcpy(data, string->data, string->length + 1U);
    }
    string->data = data;
    string->capacity = capacity;
    return true;
}

bool dynamic_string_init(DynamicString *string, Arena *arena, size_t initial_capacity) {
    if (string == nullptr || arena == nullptr) {
        return false;
    }
    *string = (DynamicString){.arena = arena};
    if (!dynamic_string_reserve(string, initial_capacity > 0U ? initial_capacity : 1U)) {
        return false;
    }
    string->data[0] = '\0';
    return true;
}

void dynamic_string_reset(DynamicString *string) {
    if (string == nullptr || string->data == nullptr) {
        return;
    }
    string->length = 0U;
    string->data[0] = '\0';
}

bool dynamic_string_append_n(DynamicString *string, const char *text, size_t length) {
    size_t required = 0U;
    if (string == nullptr || text == nullptr || ckd_add(&required, string->length, length) ||
        ckd_add(&required, required, 1U) || !dynamic_string_reserve(string, required)) {
        return false;
    }
    memcpy(string->data + string->length, text, length);
    string->length += length;
    string->data[string->length] = '\0';
    return true;
}

bool dynamic_string_append(DynamicString *string, const char *text) {
    return text != nullptr && dynamic_string_append_n(string, text, strlen(text));
}

bool dynamic_string_appendf(DynamicString *string, const char *format, ...) {
    if (string == nullptr || format == nullptr) {
        return false;
    }
    va_list arguments;
    va_start(arguments, format);
    va_list copy;
    va_copy(copy, arguments);
    int length = vsnprintf(nullptr, 0U, format, copy);
    va_end(copy);
    if (length < 0) {
        va_end(arguments);
        return false;
    }
    size_t size = (size_t)length;
    size_t required = 0U;
    if (ckd_add(&required, string->length, size) || ckd_add(&required, required, 1U) ||
        !dynamic_string_reserve(string, required)) {
        va_end(arguments);
        return false;
    }
    (void)vsnprintf(string->data + string->length, size + 1U, format, arguments);
    va_end(arguments);
    string->length += size;
    return true;
}
