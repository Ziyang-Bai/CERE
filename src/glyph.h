#ifndef GLYPH_H
#define GLYPH_H

#include <stdbool.h>
#include <stdint.h>

#define GLYPH_APPVAR_NAME "CEREFNT"

uint8_t glyph_display_width(uint32_t codepoint);
bool glyph_load_appvar(const char *name);
void glyph_unload(void);
bool glyph_is_loaded(void);
bool glyph_draw_cjk(uint32_t codepoint, uint16_t x, uint8_t y);
void glyph_draw_unknown_box(uint16_t x, uint8_t y);
char glyph_fallback_ascii(uint32_t codepoint);

#endif
