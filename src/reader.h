#ifndef READER_H
#define READER_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "article.h"

#define READER_MAX_LINES_PER_PAGE 14
#define READER_MAX_COLS_PER_LINE  40
#define READER_MAX_LINE_BYTES 128
#define READER_HISTORY_CAPACITY 512

typedef struct {
    uint8_t bytes[READER_MAX_LINE_BYTES];
    uint8_t len;
} reader_line_t;

typedef struct {
    reader_line_t lines[READER_MAX_LINES_PER_PAGE];
    uint8_t line_count;
    size_t page_start;
    size_t page_end;
} reader_page_t;

typedef struct {
    const article_stream_t *stream;
    uint8_t cols;
    uint8_t lines;
    size_t curr_start;
    size_t history[READER_HISTORY_CAPACITY];
    uint16_t history_count;
    size_t page_number;
    reader_page_t current_page;
} reader_t;

bool reader_init(reader_t *reader, const article_stream_t *stream, uint8_t cols, uint8_t lines);
bool reader_next_page(reader_t *reader);
bool reader_prev_page(reader_t *reader);
const reader_page_t *reader_get_page(const reader_t *reader);
size_t reader_page_index(const reader_t *reader);
bool reader_has_next_page(const reader_t *reader);
size_t reader_text_length(const reader_t *reader);
size_t reader_current_start(const reader_t *reader);
size_t reader_current_end(const reader_t *reader);
bool reader_jump_to_offset(reader_t *reader, size_t offset, bool reset_history);

#endif
