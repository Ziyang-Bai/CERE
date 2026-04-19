#ifndef STATE_H
#define STATE_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define STATE_POS_APPVAR "CEREPOS"
#define STATE_BOOKMARK_APPVAR "CEREBM"

#define STATE_MAX_DOCS 24
#define STATE_MAX_BOOKMARKS_PER_DOC 16

bool state_load_last_position(const char *doc_name, size_t *out_offset);
bool state_save_last_position(const char *doc_name, size_t offset);

uint8_t state_get_bookmarks(const char *doc_name, size_t *out_offsets, uint8_t max_out);
bool state_toggle_bookmark(const char *doc_name, size_t offset, bool *out_added);

#endif
