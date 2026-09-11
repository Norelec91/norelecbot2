#ifndef NORELECBOT_TEXT_H
#define NORELECBOT_TEXT_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

bool text_equals_ignore_case(const char *left, const char *right);
char *text_trim(char *text);
bool text_copy(char *destination, size_t capacity, const char *text);
/* Surrounding whitespace is allowed; empty input, trailing characters and overflow are not. */
bool text_parse_int64(const char *text, int64_t *value);
/* Invalid UTF-8 bytes count as one codepoint each. */
size_t text_utf8_prefix_bytes(const char *text, size_t max_codepoints);

#endif
