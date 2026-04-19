/*
 *--------------------------------------
 * Program Name: CERE
 * Author: CE Programming User
 * License: MIT
 * Description: TI-84 Plus CE Chinese article reader
 *--------------------------------------
*/

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include "article.h"
#include "glyph.h"
#include "input.h"
#include "reader.h"
#include "renderer.h"
#include "state.h"

typedef enum {
    APP_STATE_MENU = 0,
    APP_STATE_READER = 1,
    APP_STATE_ABOUT = 2,
    APP_STATE_PANEL = 3,
} app_state_t;

#define QUICK_HOME_WINDOW_FRAMES 300u
#define STARTUP_IGNORE_FRAMES 30u
#define PANEL_VISIBLE_ITEMS 10u
#define PANEL_ITEM_MAX_BYTES 64u

/* Keep large runtime buffers out of CE stack to avoid startup overflow. */
static renderer_layout_t layout;
static input_state_t input_state;
static input_events_t events;
static article_catalog_t catalog;
static article_stream_t stream;
static reader_t reader;
static app_state_t state;
static uint8_t selected;
static bool running;
static bool dirty;
static bool doc_open;
static char status[72];
static char reader_status[72];
static char panel_status[72];
static bool panel_toc_tab;
static uint16_t panel_selected;
static uint16_t panel_total;
static uint16_t panel_window_start;
static uint8_t panel_visible_count;
static size_t panel_bookmark_offsets[STATE_MAX_BOOKMARKS_PER_DOC];
static size_t panel_visible_offsets[PANEL_VISIBLE_ITEMS];
static uint8_t panel_labels[PANEL_VISIBLE_ITEMS][PANEL_ITEM_MAX_BYTES];
static uint8_t panel_lens[PANEL_VISIBLE_ITEMS];
static const uint8_t *panel_ptrs[PANEL_VISIBLE_ITEMS];
static uint16_t reader_open_frames;
static uint8_t quick_home_taps;
static uint16_t startup_ignore_frames;

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

static void set_status(char *status, size_t status_size, const char *msg)
{
    size_t i = 0u;

    if (status == NULL || status_size == 0u) {
        return;
    }

    if (msg == NULL) {
        status[0] = '\0';
        return;
    }

    while (i + 1u < status_size && msg[i] != '\0') {
        status[i] = msg[i];
        i++;
    }
    status[i] = '\0';
}

static void set_status_open_error(char *status, size_t status_size,
    const char *doc_name, article_open_error_t error)
{
    if (status == NULL || status_size == 0u) {
        return;
    }

    if (doc_name == NULL || doc_name[0] == '\0') {
        append_text(status, status_size, 0u, "打开失败: ");
        append_text(status, status_size, strlen(status), article_open_error_text(error));
        return;
    }

    append_text(status, status_size, 0u, doc_name);
    append_text(status, status_size, strlen(status), ": ");
    append_text(status, status_size, strlen(status), article_open_error_text(error));
}

static bool open_selected_document(const article_catalog_t *catalog, uint8_t selected,
    article_stream_t *stream, reader_t *reader, const renderer_layout_t *layout,
    char *status, size_t status_size)
{
    article_open_error_t open_error = ARTICLE_OPEN_OK;

    if (catalog == NULL || stream == NULL || reader == NULL || layout == NULL) {
        return false;
    }

    if (selected == 0u || catalog->count == 0u || selected - 1u >= catalog->count) {
        set_status(status, status_size, "没有可打开的文档");
        return false;
    }

    if (!article_open_ex(stream, catalog->names[selected - 1u], &open_error)) {
        set_status_open_error(status, status_size, catalog->names[selected - 1u], open_error);
        return false;
    }

    if (!glyph_load_appvar(article_font_name(stream))) {
        append_text(status, status_size, 0u, article_name(stream));
        append_text(status, status_size, strlen(status), ": 缺少字形 ");
        append_text(status, status_size, strlen(status), article_font_name(stream));
        article_close(stream);
        return false;
    }

    if (!reader_init(reader, stream, layout->body_cols, layout->body_lines)) {
        glyph_unload();
        article_close(stream);
        append_text(status, status_size, 0u, catalog->names[selected - 1u]);
        append_text(status, status_size, strlen(status), ": 阅读器初始化失败");
        return false;
    }

    set_status(status, status_size, "");
    return true;
}

static void restore_reader_position(reader_t *reader, size_t target_offset)
{
    const reader_page_t *page;

    if (reader == NULL) {
        return;
    }

    page = reader_get_page(reader);
    while (page != NULL && page->page_end <= target_offset && reader_has_next_page(reader)) {
        if (!reader_next_page(reader)) {
            break;
        }
        page = reader_get_page(reader);
    }
}

static uint8_t build_bookmark_panel(const article_stream_t *stream, const reader_t *reader,
    size_t *all_offsets, uint16_t *out_total,
    uint16_t selected, uint16_t *out_window_start,
    size_t *visible_offsets, uint8_t labels[][PANEL_ITEM_MAX_BYTES], uint8_t *lens,
    const uint8_t **ptrs, char *status, size_t status_size)
{
    size_t marks[STATE_MAX_BOOKMARKS_PER_DOC] = {0};
    uint8_t count;
    uint8_t i;
    uint8_t visible_count = 0u;
    uint16_t start = 0u;
    size_t total_bytes;

    if (stream == NULL || reader == NULL || all_offsets == NULL || out_total == NULL
        || out_window_start == NULL || visible_offsets == NULL || labels == NULL
        || lens == NULL || ptrs == NULL) {
        return 0u;
    }

    total_bytes = reader_text_length(reader);
    count = state_get_bookmarks(article_name(stream), marks, STATE_MAX_BOOKMARKS_PER_DOC);
    *out_total = count;
    if (count == 0u) {
        *out_window_start = 0u;
        set_status(status, status_size, "当前文档没有书签");
        return 0u;
    }

    if (selected >= count) {
        selected = (uint16_t)(count - 1u);
    }

    if (selected >= PANEL_VISIBLE_ITEMS) {
        start = (uint16_t)(selected - PANEL_VISIBLE_ITEMS + 1u);
    }
    *out_window_start = start;

    for (i = 0u; i < count; i++) {
        all_offsets[i] = marks[i];
    }

    for (i = 0u; i < PANEL_VISIBLE_ITEMS; i++) {
        uint16_t idx = (uint16_t)(start + i);
        uint8_t percent = 0u;
        int written;

        if (idx >= count) {
            break;
        }

        visible_offsets[i] = marks[idx];
        if (total_bytes > 0u) {
            percent = (uint8_t)((visible_offsets[i] * 100u) / total_bytes);
        }

        written = (int)append_text((char *)labels[i], PANEL_ITEM_MAX_BYTES, 0u, "书签");
        written = (int)append_uint((char *)labels[i], PANEL_ITEM_MAX_BYTES, (size_t)written,
            (unsigned int)(idx + 1u));
        written = (int)append_text((char *)labels[i], PANEL_ITEM_MAX_BYTES, (size_t)written, " 进度");
        written = (int)append_uint((char *)labels[i], PANEL_ITEM_MAX_BYTES, (size_t)written,
            (unsigned int)percent);
        written = (int)append_text((char *)labels[i], PANEL_ITEM_MAX_BYTES, (size_t)written, "%");

        lens[i] = (uint8_t)written;
        ptrs[i] = labels[i];
        visible_count++;
    }

    set_status(status, status_size, "2nd/Enter跳转  左右切换  Clear返回");
    return visible_count;
}

static uint8_t build_toc_panel(const article_stream_t *stream,
    uint16_t selected, uint16_t *out_total, uint16_t *out_window_start,
    size_t *visible_offsets, uint8_t labels[][PANEL_ITEM_MAX_BYTES], uint8_t *lens,
    const uint8_t **ptrs, char *status, size_t status_size)
{
    uint16_t toc_total;
    uint8_t i;
    uint8_t visible_count = 0u;
    uint16_t start = 0u;

    if (stream == NULL || out_total == NULL || out_window_start == NULL
        || visible_offsets == NULL || labels == NULL
        || lens == NULL || ptrs == NULL) {
        return 0u;
    }

    toc_total = article_toc_count(stream);
    *out_total = toc_total;
    if (toc_total == 0u) {
        *out_window_start = 0u;
        set_status(status, status_size, "当前文档没有章节");
        return 0u;
    }

    if (selected >= toc_total) {
        selected = (uint16_t)(toc_total - 1u);
    }

    if (selected >= PANEL_VISIBLE_ITEMS) {
        start = (uint16_t)(selected - PANEL_VISIBLE_ITEMS + 1u);
    }
    *out_window_start = start;

    for (i = 0u; i < PANEL_VISIBLE_ITEMS; i++) {
        article_toc_entry_t entry;
        uint8_t title_len;
        uint16_t idx = (uint16_t)(start + i);

        if (idx >= toc_total) {
            break;
        }

        if (!article_read_toc_entry(stream, idx, &entry)) {
            break;
        }

        visible_offsets[i] = entry.text_offset;
        title_len = entry.title_len;
        if (title_len == 0u) {
            static const uint8_t untitled[] = "(untitled)";
            title_len = (uint8_t)(sizeof(untitled) - 1u);
            memcpy(labels[i], untitled, title_len);
        } else {
            if (title_len > PANEL_ITEM_MAX_BYTES - 1u) {
                title_len = PANEL_ITEM_MAX_BYTES - 1u;
            }
            memcpy(labels[i], entry.title, title_len);
        }

        labels[i][title_len] = '\0';
        lens[i] = title_len;
        ptrs[i] = labels[i];
        visible_count++;
    }

    set_status(status, status_size, "2nd/Enter跳转  左右切换  Clear返回");

    return visible_count;
}

static void close_reader_to_menu(app_state_t *state, bool *doc_open,
    article_catalog_t *catalog, uint8_t *selected, article_stream_t *stream,
    reader_t *reader, char *menu_status, size_t status_size)
{
    if (stream != NULL && reader != NULL) {
        (void)state_save_last_position(article_name(stream), reader_current_start(reader));
    }

    glyph_unload();
    article_close(stream);
    (void)glyph_load_appvar(GLYPH_APPVAR_NAME);
    if (doc_open != NULL) {
        *doc_open = false;
    }
    if (state != NULL) {
        *state = APP_STATE_MENU;
    }

    article_scan_documents(catalog);
    if (catalog != NULL && selected != NULL) {
        if (catalog->count == 0u) {
            *selected = 0u;
            set_status(menu_status, status_size, "未找到 CART 文档");
        } else if (*selected > catalog->count) {
            *selected = catalog->count;
            set_status(menu_status, status_size, "已返回主菜单");
        }
    }
}

int main(void)
{
    memset(&stream, 0, sizeof(stream));
    state = APP_STATE_MENU;
    selected = 0u;
    running = true;
    dirty = true;
    doc_open = false;
    status[0] = '\0';
    reader_status[0] = '\0';
    panel_status[0] = '\0';
    panel_toc_tab = false;
    panel_selected = 0u;
    panel_total = 0u;
    panel_window_start = 0u;
    panel_visible_count = 0u;
    memset(panel_bookmark_offsets, 0, sizeof(panel_bookmark_offsets));
    memset(panel_visible_offsets, 0, sizeof(panel_visible_offsets));
    memset(panel_labels, 0, sizeof(panel_labels));
    memset(panel_lens, 0, sizeof(panel_lens));
    memset(panel_ptrs, 0, sizeof(panel_ptrs));
    reader_open_frames = 0u;
    quick_home_taps = 0u;
    startup_ignore_frames = 0u;

    renderer_init(&layout);
    input_init(&input_state);
    (void)glyph_load_appvar(GLYPH_APPVAR_NAME);

    article_scan_documents(&catalog);
    if (catalog.count == 0u) {
        set_status(status, sizeof(status), "未找到 CART 文档");
    } else {
        set_status(status, sizeof(status), "请选择 关于 或文档");
    }

    while (running) {
        input_poll(&input_state, &events);

        if (startup_ignore_frames < STARTUP_IGNORE_FRAMES) {
            memset(&events, 0, sizeof(events));
            startup_ignore_frames++;
        }

        if (state == APP_STATE_MENU) {
            uint8_t total_items = (uint8_t)(1u + catalog.count);

            if (events.exit) {
                running = false;
            }

            if ((events.nav_up || events.page_prev) && selected > 0u) {
                selected--;
                dirty = true;
            }

            if ((events.nav_down || events.page_next)
                && selected + 1u < total_items) {
                selected++;
                dirty = true;
            }

            if (events.menu) {
                article_scan_documents(&catalog);
                if (catalog.count == 0u) {
                    selected = 0u;
                    set_status(status, sizeof(status), "未找到 CART 文档");
                } else {
                    total_items = (uint8_t)(1u + catalog.count);
                    if (selected >= total_items) {
                        selected = (uint8_t)(total_items - 1u);
                    }
                    set_status(status, sizeof(status), "已刷新文档列表");
                }
                dirty = true;
            }

            if (events.confirm) {
                if (selected == 0u) {
                    state = APP_STATE_ABOUT;
                    dirty = true;
                    continue;
                }

                if (open_selected_document(&catalog, selected, &stream, &reader,
                        &layout, status, sizeof(status))) {
                    size_t saved_offset = 0u;

                    if (state_load_last_position(article_name(&stream), &saved_offset)
                        && saved_offset < reader_text_length(&reader)) {
                        restore_reader_position(&reader, saved_offset);
                        append_text(reader_status, sizeof(reader_status), 0u, "已恢复到 ");
                        append_uint(reader_status, sizeof(reader_status), strlen(reader_status),
                            (unsigned int)((saved_offset * 100u)
                                / (reader_text_length(&reader) == 0u ? 1u : reader_text_length(&reader))));
                        append_text(reader_status, sizeof(reader_status), strlen(reader_status), "%");
                    } else {
                        reader_status[0] = '\0';
                    }

                    reader_open_frames = 0u;
                    quick_home_taps = 0u;
                    doc_open = true;
                    state = APP_STATE_READER;
                    dirty = true;
                    continue;
                } else {
                    dirty = true;
                }
            }

            if (dirty) {
                renderer_draw_main_menu(&catalog, selected, status);
                dirty = false;
            }
        } else if (state == APP_STATE_READER) {
            bool page_changed = false;

            if (reader_open_frames < 0xFFFFu) {
                reader_open_frames++;
            }

            if (events.panel) {
                panel_toc_tab = false;
                panel_selected = 0u;
                panel_total = 0u;
                panel_status[0] = '\0';
                state = APP_STATE_PANEL;
                dirty = true;
                continue;
            }

            if (events.bookmark) {
                bool added = false;
                if (state_toggle_bookmark(article_name(&stream), reader_current_start(&reader), &added)) {
                    set_status(reader_status, sizeof(reader_status),
                        added ? "已添加书签" : "已取消书签");
                } else {
                    set_status(reader_status, sizeof(reader_status), "书签保存失败");
                }
                dirty = true;
            }

            if (events.confirm) {
                if (reader_open_frames <= QUICK_HOME_WINDOW_FRAMES) {
                    quick_home_taps++;
                    if (quick_home_taps >= 2u) {
                        (void)reader_jump_to_offset(&reader, 0u, true);
                        set_status(reader_status, sizeof(reader_status), "已跳转到第一页");
                        quick_home_taps = 0u;
                    } else {
                        set_status(reader_status, sizeof(reader_status), "打开后5秒内再按2nd/Enter回第一页");
                    }
                    dirty = true;
                    continue;
                }
            }

            if (events.exit || events.menu) {
                close_reader_to_menu(&state, &doc_open, &catalog, &selected,
                    &stream, &reader, status, sizeof(status));
                dirty = true;
                continue;
            }

            if ((events.page_next || events.nav_down) && reader_next_page(&reader)) {
                page_changed = true;
                reader_status[0] = '\0';
            }

            if ((events.page_prev || events.nav_up) && reader_prev_page(&reader)) {
                page_changed = true;
                reader_status[0] = '\0';
            }

            if (page_changed) {
                dirty = true;
            }

            if (dirty) {
                renderer_draw_reader(&reader, &layout, article_name(&stream), reader_status);
                dirty = false;
            }
        } else if (state == APP_STATE_PANEL) {
            if (events.exit || events.menu) {
                state = APP_STATE_READER;
                dirty = true;
                continue;
            }

            if (events.page_next || events.page_prev) {
                panel_toc_tab = !panel_toc_tab;
                panel_selected = 0u;
                dirty = true;
            }

            if (events.nav_up && panel_selected > 0u) {
                panel_selected--;
                dirty = true;
            }

            if (events.nav_down && panel_selected + 1u < panel_total) {
                panel_selected++;
                dirty = true;
            }

            if (events.confirm && panel_total > 0u) {
                size_t jump_offset = 0u;

                if (panel_toc_tab) {
                    article_toc_entry_t entry;
                    if (!article_read_toc_entry(&stream, panel_selected, &entry)) {
                        set_status(panel_status, sizeof(panel_status), "章节读取失败");
                        dirty = true;
                        continue;
                    }
                    jump_offset = entry.text_offset;
                } else if (panel_selected < STATE_MAX_BOOKMARKS_PER_DOC) {
                    jump_offset = panel_bookmark_offsets[panel_selected];
                } else {
                    set_status(panel_status, sizeof(panel_status), "书签索引无效");
                    dirty = true;
                    continue;
                }

                if (reader_jump_to_offset(&reader, jump_offset, false)) {
                    set_status(reader_status, sizeof(reader_status), "已跳转到选中位置");
                    state = APP_STATE_READER;
                    dirty = true;
                    continue;
                }
                set_status(panel_status, sizeof(panel_status), "跳转失败");
                dirty = true;
            }

            if (dirty) {
                if (panel_toc_tab) {
                    panel_visible_count = build_toc_panel(&stream,
                        panel_selected, &panel_total, &panel_window_start,
                        panel_visible_offsets,
                        panel_labels, panel_lens, panel_ptrs,
                        panel_status, sizeof(panel_status));
                } else {
                    panel_visible_count = build_bookmark_panel(&stream, &reader,
                        panel_bookmark_offsets, &panel_total,
                        panel_selected, &panel_window_start,
                        panel_visible_offsets,
                        panel_labels, panel_lens, panel_ptrs,
                        panel_status, sizeof(panel_status));
                }

                if (panel_total == 0u) {
                    panel_selected = 0u;
                    panel_window_start = 0u;
                } else if (panel_selected >= panel_total) {
                    panel_selected = (uint16_t)(panel_total - 1u);
                }

                renderer_draw_panel("跳转", panel_toc_tab ? "章节" : "书签",
                    panel_ptrs, panel_lens,
                    panel_visible_count, panel_total, panel_window_start,
                    panel_selected, panel_status);
                dirty = false;
            }
        } else {
            if (events.exit || events.menu) {
                state = APP_STATE_MENU;
                dirty = true;
                continue;
            }

            if (dirty) {
                renderer_draw_about();
                dirty = false;
            }
        }
    }

    if (doc_open) {
        (void)state_save_last_position(article_name(&stream), reader_current_start(&reader));
        glyph_unload();
        article_close(&stream);
    }

    glyph_unload();

    renderer_shutdown();

    return 0;
}
