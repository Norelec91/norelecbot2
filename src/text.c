#include "text.h"

#include <ctype.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

bool text_equals_ignore_case(const char *left, const char *right) {
    while (*left != '\0' && *right != '\0') {
        if (tolower((unsigned char)*left) != tolower((unsigned char)*right)) {
            return false;
        }
        ++left;
        ++right;
    }
    return *left == *right;
}

char *text_trim(char *text) {
    while (isspace((unsigned char)*text) != 0) {
        ++text;
    }
    char *end = text + strlen(text);
    while (end > text && isspace((unsigned char)end[-1]) != 0) {
        --end;
    }
    *end = '\0';
    return text;
}

bool text_copy(char *destination, size_t capacity, const char *text) {
    int length = snprintf(destination, capacity, "%s", text);
    return length >= 0 && (size_t)length < capacity;
}

bool text_parse_int64(const char *text, int64_t *value) {
    char *end = nullptr;
    errno = 0;
    long long parsed = strtoll(text, &end, 10);
    if (end == text || errno != 0) {
        return false;
    }
    while (isspace((unsigned char)*end) != 0) {
        ++end;
    }
    if (*end != '\0') {
        return false;
    }
    *value = (int64_t)parsed;
    return true;
}

size_t text_utf8_prefix_bytes(const char *text, size_t max_codepoints) {
    size_t bytes = 0U;
    size_t codepoints = 0U;
    while (text[bytes] != '\0' && codepoints < max_codepoints) {
        unsigned char lead = (unsigned char)text[bytes];
        size_t width = 1U;
        if ((lead & 0xE0U) == 0xC0U) {
            width = 2U;
        } else if ((lead & 0xF0U) == 0xE0U) {
            width = 3U;
        } else if ((lead & 0xF8U) == 0xF0U) {
            width = 4U;
        }
        for (size_t index = 1U; index < width; ++index) {
            if ((unsigned char)text[bytes + index] < 0x80U ||
                (unsigned char)text[bytes + index] > 0xBFU) {
                width = 1U;
                break;
            }
        }
        bytes += width;
        ++codepoints;
    }
    return bytes;
}
