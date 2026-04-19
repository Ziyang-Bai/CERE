#include "renderer.h"

#include <stddef.h>
#include <string.h>

#include <graphx.h>

#include "glyph.h"
#include "text_resources.h"
#include "utf8.h"

#define CERE_BG_COLOR 255
#define CERE_FG_COLOR 0
#define CERE_TITLE_FG 250u
#define CERE_MENU_BG 224
#define CERE_STATUS_BG 224
#define CERE_STATUS_FG 0
#define CERE_SCREEN_WIDTH 320u
#define CERE_SCREEN_HEIGHT 240u
#define CERE_FOOTER_HEIGHT 18u
#define CERE_FOOTER_Y (CERE_SCREEN_HEIGHT - CERE_FOOTER_HEIGHT)
#define CERE_COPYRIGHT_Y 206u
#define CERE_PROGRESS_X 6u
#define CERE_PROGRESS_Y 14u
#define CERE_PROGRESS_W 308u
#define CERE_PROGRESS_H 8u
#define CERE_PROGRESS_INNER_W (CERE_PROGRESS_W - 2u)

static size_t append_text(char *dst, size_t dst_size, size_t pos, const char *src)
{
    if (dst == NULL || dst_size == 0u || src == NULL || pos >= dst_size) {
        return pos;
    }

    while (*src != '\0' && pos + 1u < dst_size) {
        dst[pos++] = *src++;
    }
    dst[pos] = '\0';
    return pos;
}

static size_t append_uint(char *dst, size_t dst_size, size_t pos, unsigned int value)
{
    char digits[10];
    size_t count = 0u;

    if (dst == NULL || dst_size == 0u || pos >= dst_size) {
        return pos;
    }

    do {
        digits[count++] = (char)('0' + (value % 10u));
        value /= 10u;
    } while (value != 0u && count < sizeof(digits));

    while (count > 0u && pos + 1u < dst_size) {
        dst[pos++] = digits[--count];
    }
    dst[pos] = '\0';
    return pos;
}

static void draw_line_utf8(const uint8_t *text, size_t len, uint8_t x, uint8_t y, uint8_t fg_color)
{
    size_t pos = 0u;
    uint16_t draw_x = x;

    gfx_SetTextFGColor(fg_color);
    gfx_SetColor(fg_color);

    while (pos < len) {
        utf8_char_t ch = utf8_decode_one(text + pos, len - pos);
        char out[2];
        uint8_t char_cols;

        if (ch.bytes == 0u) {
            ch.bytes = 1u;
        }

        char_cols = glyph_display_width(ch.codepoint);

        if (draw_x >= CERE_SCREEN_WIDTH) {
            break;
        }

        if (ch.codepoint < 0x80u) {
            out[0] = glyph_fallback_ascii(ch.codepoint);
            out[1] = '\0';
            gfx_PrintStringXY(out, draw_x, (uint8_t)(y + 4u));
        } else if (!glyph_draw_cjk(ch.codepoint, draw_x, y)) {
            glyph_draw_unknown_box(draw_x, y);
        }

        draw_x += (uint16_t)char_cols * 8u;
        pos += ch.bytes;
    }
}

static bool is_chapter_title_line(const article_stream_t *stream, size_t line_offset)
{
    uint16_t toc_count;
    uint16_t i;
    article_toc_entry_t entry;

    if (stream == NULL) {
        return false;
    }

    toc_count = article_toc_count(stream);
    for (i = 0u; i < toc_count; i++) {
        if (!article_read_toc_entry(stream, i, &entry)) {
            break;
        }

        if ((size_t)entry.text_offset == line_offset) {
            return true;
        }
    }

    return false;
}

void renderer_init(renderer_layout_t *layout)
{
    gfx_Begin();
    gfx_SetDrawBuffer();
    gfx_palette[CERE_TITLE_FG] = 0xF800u;
    gfx_SetTextFGColor(CERE_FG_COLOR);
    gfx_SetTextBGColor(CERE_BG_COLOR);

    if (layout != NULL) {
        layout->margin_left = 8u;
        layout->margin_top = 28u;
        layout->body_cols = 38u;
        layout->body_lines = 13u;
    }
}

void renderer_draw_main_menu(const article_catalog_t *catalog, uint8_t selected, const char *status)
{
    uint8_t i;
    uint8_t start = 0u;
    const uint8_t max_visible = 11u;
    uint8_t total_items = 1u;

    gfx_FillScreen(CERE_BG_COLOR);

    gfx_SetColor(CERE_MENU_BG);
    gfx_FillRectangle(0u, 0u, CERE_SCREEN_WIDTH, 18u);
    gfx_SetTextFGColor(CERE_FG_COLOR);
    draw_line_utf8((const uint8_t *)RES_TXT_MENU_TITLE, strlen(RES_TXT_MENU_TITLE), 8u, 2u, CERE_FG_COLOR);

    if (catalog != NULL) {
        total_items = (uint8_t)(1u + catalog->count);
    }

    if (selected >= max_visible) {
        start = (uint8_t)(selected - max_visible + 1u);
    }

    for (i = 0u; i < max_visible; i++) {
        uint8_t idx = (uint8_t)(start + i);
        uint8_t y = (uint8_t)(24u + i * 18u);

        if (idx >= total_items) {
            break;
        }

        if (idx == selected) {
            gfx_PrintStringXY(">", 8u, y);
        } else {
            gfx_PrintStringXY(" ", 8u, y);
        }

        if (idx == 0u) {
            draw_line_utf8((const uint8_t *)RES_TXT_MENU_ABOUT, strlen(RES_TXT_MENU_ABOUT), 20u, (uint8_t)(y - 2u), CERE_FG_COLOR);
        } else if (catalog != NULL && idx - 1u < catalog->count) {
            gfx_PrintStringXY(catalog->names[idx - 1u], 20u, y);
        } else {
            draw_line_utf8((const uint8_t *)RES_TXT_MENU_INVALID, strlen(RES_TXT_MENU_INVALID), 20u, (uint8_t)(y - 2u), CERE_FG_COLOR);
        }
    }

    gfx_SetColor(CERE_STATUS_BG);
    gfx_FillRectangle(0u, CERE_FOOTER_Y, CERE_SCREEN_WIDTH, CERE_FOOTER_HEIGHT);
    gfx_SetTextFGColor(CERE_STATUS_FG);
    if (status != NULL && status[0] != '\0') {
        draw_line_utf8((const uint8_t *)status, strlen(status), 8u, (uint8_t)(CERE_FOOTER_Y + 2u), CERE_STATUS_FG);
    } else {
        draw_line_utf8((const uint8_t *)RES_TXT_STATUS_READY, strlen(RES_TXT_STATUS_READY), 8u, (uint8_t)(CERE_FOOTER_Y + 2u), CERE_STATUS_FG);
    }
    gfx_SetTextFGColor(CERE_FG_COLOR);

    draw_line_utf8((const uint8_t *)RES_TXT_COPYRIGHT, strlen(RES_TXT_COPYRIGHT), 8u, CERE_COPYRIGHT_Y, CERE_FG_COLOR);

    gfx_SwapDraw();
}

void renderer_draw_about(void)
{
    bool loaded_here = false;
    uint8_t i;

    if (!glyph_is_loaded()) {
        loaded_here = glyph_load_appvar(GLYPH_APPVAR_NAME);
    }

    gfx_FillScreen(CERE_BG_COLOR);
    gfx_SetColor(CERE_MENU_BG);
    gfx_FillRectangle(0u, 0u, CERE_SCREEN_WIDTH, 18u);
    gfx_SetTextFGColor(CERE_FG_COLOR);
    draw_line_utf8((const uint8_t *)RES_TXT_ABOUT_TITLE, strlen(RES_TXT_ABOUT_TITLE), 8u, 2u, CERE_FG_COLOR);
    for (i = 0u; i < RES_ABOUT_LINE_COUNT; i++) {
        draw_line_utf8(
            (const uint8_t *)RES_TXT_ABOUT_LINES[i],
            strlen(RES_TXT_ABOUT_LINES[i]),
            8u,
            RES_ABOUT_LINE_Y[i],
            CERE_FG_COLOR
        );
    }

    if (loaded_here) {
        glyph_unload();
    }
    gfx_SwapDraw();
}

void renderer_draw_reader(const reader_t *reader, const renderer_layout_t *layout,
    const char *doc_name, const char *status)
{
    const reader_page_t *page;
    uint8_t i;
    uint8_t y;
    uint8_t x;
    char header[96];
    uint24_t page_idx;
    size_t total_bytes;
    size_t current_bytes;
    uint24_t percent;
    uint24_t fill;

    if (reader == NULL || layout == NULL) {
        return;
    }

    (void)status;

    page = reader_get_page(reader);
    if (page == NULL) {
        return;
    }

    gfx_FillScreen(CERE_BG_COLOR);

    x = layout->margin_left;
    y = layout->margin_top;

    for (i = 0u; i < page->line_count; i++) {
        uint8_t line_color = is_chapter_title_line(reader->stream, page->lines[i].start_offset)
            ? CERE_TITLE_FG
            : CERE_FG_COLOR;

        draw_line_utf8(page->lines[i].bytes, page->lines[i].len, x, y, line_color);
        y = (uint8_t)(y + 16u);
    }

    page_idx = (uint24_t)reader_page_index(reader);
    total_bytes = reader_text_length(reader);
    current_bytes = page->page_start;
    if (total_bytes == 0u) {
        percent = 0u;
        fill = 0u;
    } else if (!reader_has_next_page(reader)) {
        percent = 100u;
        fill = CERE_PROGRESS_INNER_W;
    } else {
        percent = (uint24_t)((current_bytes * 100u) / total_bytes);
        fill = (uint24_t)((current_bytes * CERE_PROGRESS_INNER_W) / total_bytes);
    }

    if (doc_name != NULL && doc_name[0] != '\0') {
        append_text(header, sizeof(header), 0u, doc_name);
    } else {
        append_text(header, sizeof(header), 0u, "未知文档");
    }
    append_text(header, sizeof(header), strlen(header), " - ");
    append_uint(header, sizeof(header), strlen(header), (unsigned int)page_idx);

    gfx_SetColor(CERE_STATUS_BG);
    gfx_FillRectangle(0u, 0u, CERE_SCREEN_WIDTH, 26u);
    gfx_SetTextFGColor(CERE_STATUS_FG);
    draw_line_utf8((const uint8_t *)header, strlen(header), 6u, 2u, CERE_STATUS_FG);

    gfx_SetColor(CERE_FG_COLOR);
    gfx_Rectangle(CERE_PROGRESS_X, CERE_PROGRESS_Y, CERE_PROGRESS_W, CERE_PROGRESS_H);
    gfx_SetColor(CERE_FG_COLOR);
    if (fill > 0u) {
        gfx_FillRectangle((uint24_t)(CERE_PROGRESS_X + 1u), (uint8_t)(CERE_PROGRESS_Y + 1u), fill, (uint8_t)(CERE_PROGRESS_H - 2u));
    }

    append_uint(header, sizeof(header), 0u, (unsigned int)percent);
    append_text(header, sizeof(header), strlen(header), "%");
    gfx_PrintStringXY(header, 282u, 2u);

    gfx_SwapDraw();
}

void renderer_draw_panel(const char *title, const char *tab_name,
    const uint8_t *const *items, const uint8_t *item_lens,
    uint8_t visible_count, uint16_t total_count,
    uint16_t window_start, uint16_t selected,
    const char *status)
{
    uint8_t i;
    char counter[24];

    gfx_FillScreen(CERE_BG_COLOR);

    gfx_SetColor(CERE_STATUS_BG);
    gfx_FillRectangle(0u, 0u, CERE_SCREEN_WIDTH, 16u);
    gfx_SetTextFGColor(CERE_STATUS_FG);
    if (title != NULL && title[0] != '\0') {
        draw_line_utf8((const uint8_t *)title, strlen(title), 6u, 0u, CERE_STATUS_FG);
    }
    if (tab_name != NULL && tab_name[0] != '\0') {
        draw_line_utf8((const uint8_t *)tab_name, strlen(tab_name), 240u, 0u, CERE_STATUS_FG);
    }

    append_uint(counter, sizeof(counter), 0u,
        (unsigned int)((total_count == 0u) ? 0u : (selected + 1u)));
    append_text(counter, sizeof(counter), strlen(counter), "/");
    append_uint(counter, sizeof(counter), strlen(counter), (unsigned int)total_count);
    gfx_PrintStringXY(counter, 150u, 4u);

    gfx_SetTextFGColor(CERE_FG_COLOR);
    for (i = 0u; i < visible_count; i++) {
        uint16_t idx = (uint16_t)(window_start + i);
        uint8_t y = (uint8_t)(22u + i * 18u);

        if (idx >= total_count) {
            break;
        }

        if (idx == selected) {
            gfx_PrintStringXY(">", 6u, y + 4u);
        }

        if (items != NULL && item_lens != NULL && items[i] != NULL) {
            draw_line_utf8(items[i], item_lens[i], 20u, y, CERE_FG_COLOR);
        }
    }

    gfx_SetColor(CERE_STATUS_BG);
    gfx_FillRectangle(0u, CERE_FOOTER_Y, CERE_SCREEN_WIDTH, CERE_FOOTER_HEIGHT);
    gfx_SetTextFGColor(CERE_STATUS_FG);
    if (status != NULL && status[0] != '\0') {
        draw_line_utf8((const uint8_t *)status, strlen(status), 6u, (uint8_t)(CERE_FOOTER_Y + 2u), CERE_STATUS_FG);
    } else {
        draw_line_utf8((const uint8_t *)RES_TXT_PANEL_FOOTER_DEFAULT, strlen(RES_TXT_PANEL_FOOTER_DEFAULT), 6u, (uint8_t)(CERE_FOOTER_Y + 2u), CERE_STATUS_FG);
    }
    gfx_SetTextFGColor(CERE_FG_COLOR);
    gfx_SwapDraw();
}

void renderer_shutdown(void)
{
    gfx_End();
}
