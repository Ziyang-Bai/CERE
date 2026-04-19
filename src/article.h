#ifndef ARTICLE_H
#define ARTICLE_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define ARTICLE_MAGIC "CART"
#define ARTICLE_VERSION_V1 1u
#define ARTICLE_VERSION_V2 2u
#define ARTICLE_VERSION_V3 3u

#define ARTICLE_HEADER_SIZE_V1 12u
#define ARTICLE_HEADER_SIZE_V2 20u
#define ARTICLE_HEADER_SIZE_V3 28u

#define ARTICLE_VAR_NAME_MAX 8
#define ARTICLE_MAX_DOCS 24
#define ARTICLE_TOC_TITLE_MAX 48

#define ARTICLE_DEFAULT_FONT_APPVAR "CEREFNT"

typedef struct {
	char names[ARTICLE_MAX_DOCS][ARTICLE_VAR_NAME_MAX + 1u];
	uint8_t count;
} article_catalog_t;

typedef struct {
	uint8_t handle;
	uint16_t data_offset;
	uint32_t text_len;
	uint16_t toc_offset;
	uint16_t toc_count;
	char name[ARTICLE_VAR_NAME_MAX + 1u];
	char font_name[ARTICLE_VAR_NAME_MAX + 1u];
} article_stream_t;

typedef struct {
	uint32_t text_offset;
	uint8_t title_len;
	uint8_t title[ARTICLE_TOC_TITLE_MAX + 1u];
} article_toc_entry_t;

typedef enum {
	ARTICLE_OPEN_OK = 0,
	ARTICLE_OPEN_INVALID_ARG,
	ARTICLE_OPEN_NOT_FOUND,
	ARTICLE_OPEN_TOO_SMALL,
	ARTICLE_OPEN_READ_FAIL,
	ARTICLE_OPEN_BAD_MAGIC,
	ARTICLE_OPEN_BAD_VERSION,
	ARTICLE_OPEN_BAD_HEADER,
	ARTICLE_OPEN_BAD_LENGTH,
} article_open_error_t;

void article_scan_documents(article_catalog_t *catalog);

bool article_open(article_stream_t *stream, const char *name);
bool article_open_ex(article_stream_t *stream, const char *name, article_open_error_t *out_error);
void article_close(article_stream_t *stream);

bool article_read_bytes(const article_stream_t *stream, size_t text_offset, uint8_t *out, size_t len);
size_t article_length(const article_stream_t *stream);
uint16_t article_toc_count(const article_stream_t *stream);
bool article_read_toc_entry(const article_stream_t *stream, uint16_t index, article_toc_entry_t *out_entry);

const char *article_name(const article_stream_t *stream);
const char *article_font_name(const article_stream_t *stream);
const char *article_open_error_text(article_open_error_t error);

#endif
