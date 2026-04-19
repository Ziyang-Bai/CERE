#ifndef RENDERER_H
#define RENDERER_H

#include <stdbool.h>
#include <stdint.h>

#include "article.h"
#include "reader.h"

typedef struct {
    uint8_t margin_left;
    uint8_t margin_top;
    uint8_t body_cols;
    uint8_t body_lines;
} renderer_layout_t;

void renderer_init(renderer_layout_t *layout);
void renderer_draw_main_menu(const article_catalog_t *catalog, uint8_t selected, const char *status);
void renderer_draw_about(void);
void renderer_draw_reader(const reader_t *reader, const renderer_layout_t *layout,
    const char *doc_name, const char *status);
void renderer_draw_panel(const char *title, const char *tab_name,
    const uint8_t *const *items, const uint8_t *item_lens,
    uint8_t visible_count, uint16_t total_count,
    uint16_t window_start, uint16_t selected,
    const char *status);
void renderer_shutdown(void);

#endif
