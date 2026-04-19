#include "reader.h"

#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include "glyph.h"
#include "utf8.h"

static bool read_byte(const reader_t *reader, size_t offset, uint8_t *out)
{
    return article_read_bytes(reader->stream, offset, out, 1u);
}

static utf8_char_t decode_at(const reader_t *reader, size_t offset, uint8_t seq[4])
{
    utf8_char_t ch;
    size_t remaining;
    size_t to_read;

    ch.codepoint = 0xFFFDu;
    ch.bytes = 1u;

    remaining = article_length(reader->stream) - offset;
    to_read = (remaining < 4u) ? remaining : 4u;
    if (to_read == 0u) {
        return ch;
    }

    memset(seq, 0, 4u);
    if (!article_read_bytes(reader->stream, offset, seq, to_read)) {
        return ch;
    }

    ch = utf8_decode_one(seq, to_read);
    if (ch.bytes == 0u || ch.bytes > to_read) {
        ch.codepoint = 0xFFFDu;
        ch.bytes = 1u;
    }

    return ch;
}

static bool read_byte_from_stream(const article_stream_t *stream, size_t offset, uint8_t *out)
{
    return article_read_bytes(stream, offset, out, 1u);
}

static utf8_char_t decode_at_stream(const article_stream_t *stream, size_t offset, uint8_t seq[4])
{
    utf8_char_t ch;
    size_t remaining;
    size_t to_read;

    ch.codepoint = 0xFFFDu;
    ch.bytes = 1u;

    remaining = article_length(stream) - offset;
    to_read = (remaining < 4u) ? remaining : 4u;
    if (to_read == 0u) {
        return ch;
    }

    memset(seq, 0, 4u);
    if (!article_read_bytes(stream, offset, seq, to_read)) {
        return ch;
    }

    ch = utf8_decode_one(seq, to_read);
    if (ch.bytes == 0u || ch.bytes > to_read) {
        ch.codepoint = 0xFFFDu;
        ch.bytes = 1u;
    }

    return ch;
}

static void build_page(reader_t *reader, size_t page_start)
{
    size_t text_len = article_length(reader->stream);
    size_t pos = (page_start > text_len) ? text_len : page_start;
    uint8_t line_idx = 0u;

    reader->current_page.page_start = pos;

    while (line_idx < reader->lines && pos < text_len) {
        uint8_t used_cols = 0u;
        uint8_t line_len = 0u;
        size_t line_start = pos;

        reader->current_page.lines[line_idx].start_offset = line_start;

        while (pos < text_len) {
            uint8_t b;
            uint8_t next_b;
            uint8_t seq[4];
            utf8_char_t ch;
            uint8_t char_w;

            if (!read_byte(reader, pos, &b)) {
                pos = text_len;
                break;
            }

            if (b == (uint8_t)'\r') {
                pos += 1u;
                if (pos < text_len && read_byte(reader, pos, &next_b) && next_b == (uint8_t)'\n') {
                    pos += 1u;
                }
                break;
            }

            if (b == (uint8_t)'\n') {
                pos += 1u;
                break;
            }

            ch = decode_at(reader, pos, seq);
            char_w = glyph_display_width(ch.codepoint);

            if ((uint16_t)used_cols + (uint16_t)char_w > (uint16_t)reader->cols) {
                break;
            }

            if ((uint16_t)line_len + (uint16_t)ch.bytes > READER_MAX_LINE_BYTES) {
                break;
            }

            memcpy(reader->current_page.lines[line_idx].bytes + line_len, seq, ch.bytes);
            line_len = (uint8_t)(line_len + ch.bytes);
            used_cols = (uint8_t)(used_cols + char_w);
            pos += ch.bytes;
        }

        if (line_len == 0u && pos == line_start && pos < text_len) {
            uint8_t skip = 0u;
            if (read_byte(reader, pos, &skip)) {
                pos += 1u;
            } else {
                pos = text_len;
            }
        }

        reader->current_page.lines[line_idx].len = line_len;
        line_idx++;
    }

    reader->current_page.line_count = line_idx;
    reader->current_page.page_end = pos;
}

static void history_push(reader_t *reader, size_t offset)
{
    if (reader->history_count < READER_HISTORY_CAPACITY) {
        reader->history[reader->history_count] = offset;
        reader->history_count++;
        return;
    }

    memmove(reader->history, reader->history + 1u,
        (READER_HISTORY_CAPACITY - 1u) * sizeof(reader->history[0]));
    reader->history[READER_HISTORY_CAPACITY - 1u] = offset;
}

bool reader_init(reader_t *reader, const article_stream_t *stream, uint8_t cols, uint8_t lines)
{
    if (reader == NULL || stream == NULL || stream->handle == 0u || cols == 0u || lines == 0u) {
        return false;
    }

    if (lines > READER_MAX_LINES_PER_PAGE || cols > READER_MAX_COLS_PER_LINE) {
        return false;
    }

    memset(reader, 0, sizeof(*reader));
    reader->stream = stream;
    reader->cols = cols;
    reader->lines = lines;
    reader->curr_start = 0u;
    reader->page_number = 1u;

    build_page(reader, 0u);

    return true;
}

bool reader_next_page(reader_t *reader)
{
    size_t next_start;

    if (reader == NULL) {
        return false;
    }

    next_start = reader->current_page.page_end;
    if (next_start >= article_length(reader->stream) || next_start == reader->curr_start) {
        return false;
    }

    history_push(reader, reader->curr_start);
    reader->curr_start = next_start;
    reader->page_number++;
    build_page(reader, next_start);

    return true;
}

bool reader_prev_page(reader_t *reader)
{
    size_t prev_start;

    if (reader == NULL || reader->history_count == 0u) {
        return false;
    }

    reader->history_count--;
    prev_start = reader->history[reader->history_count];
    reader->curr_start = prev_start;

    if (reader->page_number > 1u) {
        reader->page_number--;
    }

    build_page(reader, prev_start);

    return true;
}

const reader_page_t *reader_get_page(const reader_t *reader)
{
    if (reader == NULL) {
        return NULL;
    }

    return &reader->current_page;
}

size_t reader_page_index(const reader_t *reader)
{
    if (reader == NULL) {
        return 0u;
    }

    return reader->page_number;
}

bool reader_has_next_page(const reader_t *reader)
{
    if (reader == NULL) {
        return false;
    }

    return reader->current_page.page_end < article_length(reader->stream);
}

size_t reader_text_length(const reader_t *reader)
{
    if (reader == NULL) {
        return 0u;
    }

    return article_length(reader->stream);
}

size_t reader_current_start(const reader_t *reader)
{
    if (reader == NULL) {
        return 0u;
    }

    return reader->current_page.page_start;
}

size_t reader_current_end(const reader_t *reader)
{
    if (reader == NULL) {
        return 0u;
    }

    return reader->current_page.page_end;
}

size_t reader_page_number_for_offset(const article_stream_t *stream, uint8_t cols, uint8_t lines, size_t offset)
{
    size_t text_len;
    size_t pos;
    size_t page_start;
    size_t page_end;
    size_t page_number = 1u;
    uint8_t line_idx;

    if (stream == NULL || stream->handle == 0u || cols == 0u || lines == 0u) {
        return 0u;
    }

    text_len = article_length(stream);
    if (offset > text_len) {
        offset = text_len;
    }

    pos = 0u;
    while (pos < text_len) {
        page_start = pos;

        line_idx = 0u;
        while (line_idx < lines && pos < text_len) {
            uint8_t line_used_cols = 0u;
            uint8_t line_len = 0u;
            size_t line_start = pos;

            while (pos < text_len) {
                uint8_t b;
                uint8_t next_b;
                uint8_t seq[4];
                utf8_char_t ch;
                uint8_t char_w;

                if (!read_byte_from_stream(stream, pos, &b)) {
                    pos = text_len;
                    break;
                }

                if (b == (uint8_t)'\r') {
                    pos += 1u;
                    if (pos < text_len && article_read_bytes(stream, pos, &next_b, 1u) && next_b == (uint8_t)'\n') {
                        pos += 1u;
                    }
                    break;
                }

                if (b == (uint8_t)'\n') {
                    pos += 1u;
                    break;
                }

                ch = decode_at_stream(stream, pos, seq);
                char_w = glyph_display_width(ch.codepoint);

                if ((uint16_t)line_used_cols + (uint16_t)char_w > (uint16_t)cols) {
                    break;
                }

                if ((uint16_t)line_len + (uint16_t)ch.bytes > READER_MAX_LINE_BYTES) {
                    break;
                }

                pos += ch.bytes;
                line_len = (uint8_t)(line_len + ch.bytes);
                line_used_cols = (uint8_t)(line_used_cols + char_w);
            }

            if (line_len == 0u && pos == line_start && pos < text_len) {
                pos += 1u;
            }

            line_idx++;
        }

        page_end = pos;
        if (page_end > offset) {
            return page_number;
        }

        if (page_end == page_start || page_end >= text_len) {
            break;
        }

        page_number++;
    }

    return page_number;
}

bool reader_jump_to_offset(reader_t *reader, size_t offset, bool reset_history)
{
    size_t text_len;

    if (reader == NULL || reader->stream == NULL) {
        return false;
    }

    text_len = article_length(reader->stream);
    if (offset > text_len) {
        return false;
    }

    if (reset_history) {
        reader->history_count = 0u;
        reader->page_number = 1u;
    } else {
        history_push(reader, reader->curr_start);
        if (reader->page_number < (size_t)-1) {
            reader->page_number++;
        }
    }

    reader->curr_start = offset;
    build_page(reader, offset);
    return true;
}
