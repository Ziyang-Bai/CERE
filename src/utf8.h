#ifndef UTF8_H
#define UTF8_H

#include <stddef.h>
#include <stdint.h>

typedef struct {
    uint32_t codepoint;
    uint8_t bytes;
} utf8_char_t;

utf8_char_t utf8_decode_one(const uint8_t *s, size_t len);

#endif
