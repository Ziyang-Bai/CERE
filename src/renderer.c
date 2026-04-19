#include "renderer.h"

#include <stddef.h>
#include <string.h>

#include <graphx.h>

#include "glyph.h"
#include "utf8.h"

#define CERE_BG_COLOR 255
#define CERE_FG_COLOR 0
#define CERE_MENU_BG 224
#define CERE_STATUS_BG 224
#define CERE_STATUS_FG 0
#define CERE_SCREEN_WIDTH 320u

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

static void draw_line_utf8(const uint8_t *text, size_t len, uint8_t x, uint8_t y)
{
    size_t pos = 0u;
    uint16_t draw_x = x;

    gfx_SetColor(CERE_FG_COLOR);

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

void renderer_init(renderer_layout_t *layout)
{
    gfx_Begin();
    gfx_SetDrawBuffer();
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
    const uint8_t max_visible = 10u;
    uint8_t total_items = 1u;

    gfx_FillScreen(CERE_BG_COLOR);

    gfx_SetColor(CERE_MENU_BG);
    gfx_FillRectangle(0u, 0u, CERE_SCREEN_WIDTH, 18u);
    gfx_SetTextFGColor(CERE_FG_COLOR);
    draw_line_utf8((const uint8_t *)"CERE 文档", sizeof("CERE 文档") - 1u, 8u, 2u);

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
            draw_line_utf8((const uint8_t *)"关于", sizeof("关于") - 1u, 20u, (uint8_t)(y - 2u));
        } else if (catalog != NULL && idx - 1u < catalog->count) {
            gfx_PrintStringXY(catalog->names[idx - 1u], 20u, y);
        } else {
            draw_line_utf8((const uint8_t *)"<无效>", sizeof("<无效>") - 1u, 20u, (uint8_t)(y - 2u));
        }
    }

    gfx_SetColor(CERE_STATUS_BG);
    gfx_FillRectangle(0u, 198u, CERE_SCREEN_WIDTH, 18u);
    gfx_SetTextFGColor(CERE_STATUS_FG);
    if (status != NULL && status[0] != '\0') {
        draw_line_utf8((const uint8_t *)status, strlen(status), 8u, 200u);
    } else {
        draw_line_utf8((const uint8_t *)"就绪", sizeof("就绪") - 1u, 8u, 200u);
    }
    gfx_SetTextFGColor(CERE_FG_COLOR);

    draw_line_utf8((const uint8_t *)"版权所有(c) 2026 ziyangbai保留所有权利",
        sizeof("版权所有(c) 2026 ziyangbai保留所有权利") - 1u, 8u, 222u);

    gfx_SwapDraw();
}

void renderer_draw_about(void)
{
    bool loaded_here = false;

    if (!glyph_is_loaded()) {
        loaded_here = glyph_load_appvar(GLYPH_APPVAR_NAME);
    }

    gfx_FillScreen(CERE_BG_COLOR);
    gfx_SetColor(CERE_MENU_BG);
    gfx_FillRectangle(0u, 0u, CERE_SCREEN_WIDTH, 18u);
    gfx_SetTextFGColor(CERE_FG_COLOR);
    draw_line_utf8((const uint8_t *)"关于 CERE", sizeof("关于 CERE") - 1u, 8u, 2u);
    draw_line_utf8((const uint8_t *)"功能与键位：", sizeof("功能与键位：") - 1u, 8u, 26u);
    draw_line_utf8((const uint8_t *)"主菜单: 上/下选择,2nd/Enter打开", sizeof("主菜单: 上/下选择,2nd/Enter打开") - 1u, 8u, 44u);
    draw_line_utf8((const uint8_t *)"主菜单: Alpha刷新,Clear退出", sizeof("主菜单: Alpha刷新,Clear退出") - 1u, 8u, 60u);
    draw_line_utf8((const uint8_t *)"阅读: 左/右/上/下翻页", sizeof("阅读: 左/右/上/下翻页") - 1u, 8u, 76u);
    draw_line_utf8((const uint8_t *)"阅读: 打开文章五秒内2nd或Enter双击回第一页", sizeof("阅读: 打开文章五秒内2nd或Enter双击回第一页") - 1u, 8u, 92u);
    draw_line_utf8((const uint8_t *)"阅读: VARS键打书签", sizeof("阅读: VARS键打书签") - 1u, 8u, 108u);
    draw_line_utf8((const uint8_t *)"阅读: STAT打开章节/书签面板", sizeof("阅读: STAT打开章节/书签面板") - 1u, 8u, 124u);
    draw_line_utf8((const uint8_t *)"面板: 左右切换,上下选择,2nd跳转", sizeof("面板: 左右切换,上下选择,2nd跳转") - 1u, 8u, 140u);
    draw_line_utf8((const uint8_t *)"阅读返回主菜单: Alpha/Clear", sizeof("阅读返回主菜单: Alpha/Clear") - 1u, 8u, 156u);
    draw_line_utf8((const uint8_t *)"安装: CERE.8xp+文档.8xv+字形.8xv", sizeof("安装: CERE.8xp+文档.8xv+字形.8xv") - 1u, 8u, 172u);
    draw_line_utf8((const uint8_t *)"都要一起传入计算器", sizeof("都要一起传入计算器") - 1u, 8u, 188u);
    draw_line_utf8((const uint8_t *)"返回键: Alpha 或 Clear 版本：v1.0", sizeof("返回键: Alpha 或 Clear 版本：v1.0") - 1u, 8u, 220u);

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
    char header[48];
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
        draw_line_utf8(page->lines[i].bytes, page->lines[i].len, x, y);
        y = (uint8_t)(y + 16u);
    }

    page_idx = (uint24_t)reader_page_index(reader);
    total_bytes = reader_text_length(reader);
    current_bytes = page->page_end;
    percent = (total_bytes > 0u)
        ? (uint24_t)((current_bytes * 100u) / total_bytes)
        : 0u;
    fill = (total_bytes > 0u)
        ? (uint24_t)((current_bytes * 120u) / total_bytes)
        : 0u;
    if (fill > 120u) {
        fill = 120u;
    }

    if (doc_name != NULL && doc_name[0] != '\0') {
        append_text(header, sizeof(header), 0u, doc_name);
        append_text(header, sizeof(header), strlen(header), " - ");
        append_uint(header, sizeof(header), strlen(header), (unsigned int)page_idx);
    } else {
        append_text(header, sizeof(header), 0u, "未知文档 - ");
        append_uint(header, sizeof(header), strlen(header), (unsigned int)page_idx);
    }

    gfx_SetColor(CERE_STATUS_BG);
    gfx_FillRectangle(0u, 0u, CERE_SCREEN_WIDTH, 26u);
    gfx_SetTextFGColor(CERE_STATUS_FG);
    draw_line_utf8((const uint8_t *)header, strlen(header), 6u, 2u);

    gfx_SetColor(CERE_FG_COLOR);
    gfx_Rectangle(6u, 14u, 308u, 8u);
    gfx_SetColor(CERE_FG_COLOR);
    if (fill > 0u) {
        gfx_FillRectangle(7u, 15u, fill, 6u);
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
        draw_line_utf8((const uint8_t *)title, strlen(title), 6u, 0u);
    }
    if (tab_name != NULL && tab_name[0] != '\0') {
        draw_line_utf8((const uint8_t *)tab_name, strlen(tab_name), 240u, 0u);
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
            draw_line_utf8(items[i], item_lens[i], 20u, y);
        }
    }

    gfx_SetColor(CERE_STATUS_BG);
    gfx_FillRectangle(0u, 198u, CERE_SCREEN_WIDTH, 18u);
    gfx_SetTextFGColor(CERE_STATUS_FG);
    if (status != NULL && status[0] != '\0') {
        draw_line_utf8((const uint8_t *)status, strlen(status), 6u, 200u);
    } else {
        draw_line_utf8((const uint8_t *)"2nd/Enter跳转  左右切换  Clear返回", sizeof("2nd/Enter跳转  左右切换  Clear返回") - 1u, 6u, 200u);
    }
    gfx_SetTextFGColor(CERE_FG_COLOR);
    gfx_SwapDraw();
}

void renderer_shutdown(void)
{
    gfx_End();
}
