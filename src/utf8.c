#include "utf8.h"

utf8_char_t utf8_decode_one(const uint8_t *s, size_t len)
{
    utf8_char_t result;
    uint8_t b0;

    result.codepoint = 0xFFFDu;
    result.bytes = 1u;

    if (s == NULL || len == 0u) {
        return result;
    }

    b0 = s[0];

    if (b0 < 0x80u) {
        result.codepoint = b0;
        result.bytes = 1u;
        return result;
    }

    if ((b0 & 0xE0u) == 0xC0u) {
        uint8_t b1;
        if (len < 2u) {
            return result;
        }
        b1 = s[1];
        if ((b1 & 0xC0u) != 0x80u) {
            return result;
        }
        result.codepoint = ((uint32_t)(b0 & 0x1Fu) << 6) | (uint32_t)(b1 & 0x3Fu);
        if (result.codepoint < 0x80u) {
            result.codepoint = 0xFFFDu;
            result.bytes = 1u;
            return result;
        }
        result.bytes = 2u;
        return result;
    }

    if ((b0 & 0xF0u) == 0xE0u) {
        uint8_t b1;
        uint8_t b2;
        if (len < 3u) {
            return result;
        }
        b1 = s[1];
        b2 = s[2];
        if ((b1 & 0xC0u) != 0x80u || (b2 & 0xC0u) != 0x80u) {
            return result;
        }
        result.codepoint = ((uint32_t)(b0 & 0x0Fu) << 12)
            | ((uint32_t)(b1 & 0x3Fu) << 6)
            | (uint32_t)(b2 & 0x3Fu);
        if (result.codepoint < 0x800u) {
            result.codepoint = 0xFFFDu;
            result.bytes = 1u;
            return result;
        }
        result.bytes = 3u;
        return result;
    }

    if ((b0 & 0xF8u) == 0xF0u) {
        uint8_t b1;
        uint8_t b2;
        uint8_t b3;
        if (len < 4u) {
            return result;
        }
        b1 = s[1];
        b2 = s[2];
        b3 = s[3];
        if ((b1 & 0xC0u) != 0x80u || (b2 & 0xC0u) != 0x80u || (b3 & 0xC0u) != 0x80u) {
            return result;
        }
        result.codepoint = ((uint32_t)(b0 & 0x07u) << 18)
            | ((uint32_t)(b1 & 0x3Fu) << 12)
            | ((uint32_t)(b2 & 0x3Fu) << 6)
            | (uint32_t)(b3 & 0x3Fu);
        if (result.codepoint < 0x10000u || result.codepoint > 0x10FFFFu) {
            result.codepoint = 0xFFFDu;
            result.bytes = 1u;
            return result;
        }
        result.bytes = 4u;
        return result;
    }

    return result;
}
