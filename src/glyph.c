#include "glyph.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include <fileioc.h>
#include <graphx.h>

#define GLYPH_MAGIC "CFNT"
#define GLYPH_VERSION 1u
#define GLYPH_HEADER_SIZE 12u
#define GLYPH_BITMAP_SIZE 32u
#define GLYPH_ENTRY_SIZE (4u + GLYPH_BITMAP_SIZE)

static uint8_t *g_font_blob = NULL;
static size_t g_font_blob_size = 0u;
static const uint8_t *g_entries = NULL;
static uint32_t g_entry_count = 0u;
static uint8_t g_font_handle = 0u;

static uint32_t read_u32_le(const uint8_t *p)
{
    return ((uint32_t)p[0])
        | ((uint32_t)p[1] << 8)
        | ((uint32_t)p[2] << 16)
        | ((uint32_t)p[3] << 24);
}

static void draw_hline(uint16_t x, uint8_t y, uint8_t len)
{
    if (len == 0u) {
        return;
    }
    gfx_Line((int)x, (int)y, (int)(x + (uint16_t)len - 1u), (int)y);
}

static void draw_vline(uint16_t x, uint8_t y, uint8_t len)
{
    if (len == 0u) {
        return;
    }
    gfx_Line((int)x, (int)y, (int)x, (int)(y + len - 1u));
}

static const uint8_t *glyph_find_bitmap(uint32_t codepoint)
{
    uint32_t low = 0u;
    uint32_t high = g_entry_count;

    while (low < high) {
        uint32_t mid = low + (high - low) / 2u;
        const uint8_t *entry = g_entries + (size_t)mid * GLYPH_ENTRY_SIZE;
        uint32_t cp = read_u32_le(entry);

        if (cp == codepoint) {
            return entry + 4u;
        }

        if (cp < codepoint) {
            low = mid + 1u;
        } else {
            high = mid;
        }
    }

    return NULL;
}

static void glyph_draw_bitmap16(const uint8_t *bitmap, uint16_t x, uint8_t y)
{
    uint8_t row;

    for (row = 0u; row < 16u; row++) {
        uint16_t bits = ((uint16_t)bitmap[(size_t)row * 2u] << 8)
            | bitmap[(size_t)row * 2u + 1u];
        uint8_t col;

        for (col = 0u; col < 16u; col++) {
            if ((bits & ((uint16_t)1u << (15u - col))) != 0u) {
                gfx_SetPixel((int)(x + col), (int)(y + row));
            }
        }
    }
}

uint8_t glyph_display_width(uint32_t codepoint)
{
    if (codepoint < 0x80u) {
        return 1u;
    }
    return 2u;
}

bool glyph_load_appvar(const char *name)
{
    uint8_t handle;
    size_t blob_size;
    uint32_t entry_count;
    size_t required_size;
    const char *var_name;
    const uint8_t *blob;

    glyph_unload();

    var_name = (name != NULL) ? name : GLYPH_APPVAR_NAME;
    handle = ti_OpenVar(var_name, "r", OS_TYPE_APPVAR);
    if (handle == 0u) {
        return false;
    }

    blob_size = ti_GetSize(handle);
    if (blob_size < GLYPH_HEADER_SIZE) {
        ti_Close(handle);
        return false;
    }

    blob = (const uint8_t *)ti_GetDataPtr(handle);
    if (blob == NULL) {
        ti_Close(handle);
        return false;
    }

    if (memcmp(blob, GLYPH_MAGIC, 4u) != 0 || blob[4] != GLYPH_VERSION) {
        ti_Close(handle);
        return false;
    }

    entry_count = read_u32_le(blob + 8u);
    required_size = GLYPH_HEADER_SIZE + (size_t)entry_count * GLYPH_ENTRY_SIZE;
    if (required_size > blob_size) {
        ti_Close(handle);
        return false;
    }

    g_font_blob = (uint8_t *)blob;
    g_font_blob_size = blob_size;
    g_entries = blob + GLYPH_HEADER_SIZE;
    g_entry_count = entry_count;
    g_font_handle = handle;

    return true;
}

void glyph_unload(void)
{
    if (g_font_blob != NULL) {
        g_font_blob = NULL;
    }
    if (g_font_handle != 0u) {
        ti_Close(g_font_handle);
        g_font_handle = 0u;
    }
    g_font_blob_size = 0u;
    g_entries = NULL;
    g_entry_count = 0u;
}

bool glyph_is_loaded(void)
{
    return g_font_blob != NULL && g_font_blob_size >= GLYPH_HEADER_SIZE;
}

bool glyph_draw_cjk(uint32_t codepoint, uint16_t x, uint8_t y)
{
    const uint8_t *bitmap;

    if (!glyph_is_loaded()) {
        return false;
    }

    bitmap = glyph_find_bitmap(codepoint);
    if (bitmap == NULL) {
        return false;
    }

    glyph_draw_bitmap16(bitmap, x, y);
    return true;
}

void glyph_draw_unknown_box(uint16_t x, uint8_t y)
{
    draw_hline((uint16_t)(x + 1u), (uint8_t)(y + 1u), 14u);
    draw_hline((uint16_t)(x + 1u), (uint8_t)(y + 14u), 14u);
    draw_vline((uint16_t)(x + 1u), (uint8_t)(y + 1u), 14u);
    draw_vline((uint16_t)(x + 14u), (uint8_t)(y + 1u), 14u);
    gfx_Line((int)(x + 3u), (int)(y + 3u), (int)(x + 12u), (int)(y + 12u));
    gfx_Line((int)(x + 12u), (int)(y + 3u), (int)(x + 3u), (int)(y + 12u));
}

char glyph_fallback_ascii(uint32_t codepoint)
{
    if (codepoint < 0x80u) {
        return (char)codepoint;
    }

    switch (codepoint) {
    case 0x3002u:
        return '.';
    case 0xFF0Cu:
        return ',';
    case 0xFF1Au:
        return ':';
    case 0xFF1Bu:
        return ';';
    case 0xFF01u:
        return '!';
    case 0xFF1Fu:
        return '?';
    case 0x2014u:
        return '-';
    case 0x2018u:
    case 0x2019u:
        return '\'';
    case 0x201Cu:
    case 0x201Du:
        return '"';
    default:
        return '#';
    }
}
