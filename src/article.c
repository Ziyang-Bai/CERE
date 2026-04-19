#include "article.h"

#include <limits.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include <fileioc.h>

#include "text_resources.h"

static uint32_t read_u32_le(const uint8_t *p)
{
    return ((uint32_t)p[0])
        | ((uint32_t)p[1] << 8)
        | ((uint32_t)p[2] << 16)
        | ((uint32_t)p[3] << 24);
}

static void set_open_error(article_open_error_t *out_error, article_open_error_t value)
{
    if (out_error != NULL) {
        *out_error = value;
    }
}

static void copy_var_name(char *dst, const char *src)
{
    uint8_t i = 0u;

    if (dst == NULL) {
        return;
    }

    if (src == NULL) {
        dst[0] = '\0';
        return;
    }

    while (i < ARTICLE_VAR_NAME_MAX && src[i] != '\0') {
        dst[i] = src[i];
        i++;
    }
    dst[i] = '\0';
}

static void copy_field_name(char *dst, const uint8_t *src, uint8_t len)
{
    uint8_t i = 0u;

    while (i < len && src[i] != 0u) {
        dst[i] = (char)src[i];
        i++;
    }
    dst[i] = '\0';
}

static void stream_reset(article_stream_t *stream)
{
    if (stream == NULL) {
        return;
    }

    stream->handle = 0u;
    stream->data_offset = 0u;
    stream->text_len = 0u;
    stream->toc_offset = 0u;
    stream->toc_count = 0u;
    stream->name[0] = '\0';
    copy_var_name(stream->font_name, ARTICLE_DEFAULT_FONT_APPVAR);
}

void article_scan_documents(article_catalog_t *catalog)
{
    void *vat_ptr = NULL;
    char *name;

    if (catalog == NULL) {
        return;
    }

    catalog->count = 0u;

    while (catalog->count < ARTICLE_MAX_DOCS) {
        name = ti_Detect(&vat_ptr, ARTICLE_MAGIC);
        if (name == NULL) {
            break;
        }

        copy_var_name(catalog->names[catalog->count], name);
        if (catalog->names[catalog->count][0] != '\0') {
            catalog->count++;
        }
    }
}

bool article_open(article_stream_t *stream, const char *name)
{
    return article_open_ex(stream, name, NULL);
}

bool article_open_ex(article_stream_t *stream, const char *name, article_open_error_t *out_error)
{
    uint8_t handle;
    uint16_t size;
    uint8_t header[ARTICLE_HEADER_SIZE_V3];
    uint8_t version;
    size_t text_len;
    size_t data_offset;
    size_t toc_offset = 0u;
    size_t toc_count = 0u;

    if (stream == NULL || name == NULL || name[0] == '\0') {
        set_open_error(out_error, ARTICLE_OPEN_INVALID_ARG);
        return false;
    }

    article_close(stream);
    stream_reset(stream);

    handle = ti_OpenVar(name, "r", OS_TYPE_APPVAR);
    if (handle == 0u) {
        set_open_error(out_error, ARTICLE_OPEN_NOT_FOUND);
        return false;
    }

    size = ti_GetSize(handle);
    if (size < ARTICLE_HEADER_SIZE_V1) {
        ti_Close(handle);
        set_open_error(out_error, ARTICLE_OPEN_TOO_SMALL);
        return false;
    }

    if (ti_Read(header, 1u, ARTICLE_HEADER_SIZE_V1, handle) != ARTICLE_HEADER_SIZE_V1) {
        ti_Close(handle);
        set_open_error(out_error, ARTICLE_OPEN_READ_FAIL);
        return false;
    }

    if (memcmp(header, ARTICLE_MAGIC, 4u) != 0) {
        ti_Close(handle);
        set_open_error(out_error, ARTICLE_OPEN_BAD_MAGIC);
        return false;
    }

    version = header[4];
    text_len = read_u32_le(header + 8u);

    if (version == ARTICLE_VERSION_V1) {
        data_offset = ARTICLE_HEADER_SIZE_V1;
        copy_var_name(stream->font_name, ARTICLE_DEFAULT_FONT_APPVAR);
    } else if (version == ARTICLE_VERSION_V2) {
        if (size < ARTICLE_HEADER_SIZE_V2) {
            ti_Close(handle);
            set_open_error(out_error, ARTICLE_OPEN_BAD_HEADER);
            return false;
        }

        if (ti_Seek(ARTICLE_HEADER_SIZE_V1, SEEK_SET, handle) == EOF
            || ti_Read(header + ARTICLE_HEADER_SIZE_V1, 1u,
                ARTICLE_HEADER_SIZE_V2 - ARTICLE_HEADER_SIZE_V1, handle)
                != ARTICLE_HEADER_SIZE_V2 - ARTICLE_HEADER_SIZE_V1) {
            ti_Close(handle);
            set_open_error(out_error, ARTICLE_OPEN_READ_FAIL);
            return false;
        }

        data_offset = ARTICLE_HEADER_SIZE_V2;
        copy_field_name(stream->font_name, header + 12u, ARTICLE_VAR_NAME_MAX);
        if (stream->font_name[0] == '\0') {
            copy_var_name(stream->font_name, ARTICLE_DEFAULT_FONT_APPVAR);
        }
    } else if (version == ARTICLE_VERSION_V3) {
        if (size < ARTICLE_HEADER_SIZE_V3) {
            ti_Close(handle);
            set_open_error(out_error, ARTICLE_OPEN_BAD_HEADER);
            return false;
        }

        if (ti_Seek(ARTICLE_HEADER_SIZE_V1, SEEK_SET, handle) == EOF
            || ti_Read(header + ARTICLE_HEADER_SIZE_V1, 1u,
                ARTICLE_HEADER_SIZE_V3 - ARTICLE_HEADER_SIZE_V1, handle)
                != ARTICLE_HEADER_SIZE_V3 - ARTICLE_HEADER_SIZE_V1) {
            ti_Close(handle);
            set_open_error(out_error, ARTICLE_OPEN_READ_FAIL);
            return false;
        }

        data_offset = ARTICLE_HEADER_SIZE_V3;
        copy_field_name(stream->font_name, header + 12u, ARTICLE_VAR_NAME_MAX);
        if (stream->font_name[0] == '\0') {
            copy_var_name(stream->font_name, ARTICLE_DEFAULT_FONT_APPVAR);
        }

        toc_offset = read_u32_le(header + 20u);
        toc_count = read_u32_le(header + 24u);
    } else {
        ti_Close(handle);
        set_open_error(out_error, ARTICLE_OPEN_BAD_VERSION);
        return false;
    }

    if (data_offset + text_len > size) {
        ti_Close(handle);
        set_open_error(out_error, ARTICLE_OPEN_BAD_LENGTH);
        return false;
    }

    if (version == ARTICLE_VERSION_V3) {
        if (toc_count > 0u) {
            if (toc_offset < data_offset + text_len || toc_offset > size) {
                ti_Close(handle);
                set_open_error(out_error, ARTICLE_OPEN_BAD_HEADER);
                return false;
            }

            if (toc_count > UINT16_MAX) {
                ti_Close(handle);
                set_open_error(out_error, ARTICLE_OPEN_BAD_HEADER);
                return false;
            }
        } else {
            toc_offset = 0u;
        }
    }

    stream->handle = handle;
    stream->data_offset = (uint16_t)data_offset;
    stream->text_len = (uint32_t)text_len;
    stream->toc_offset = (uint16_t)toc_offset;
    stream->toc_count = (uint16_t)toc_count;
    copy_var_name(stream->name, name);

    set_open_error(out_error, ARTICLE_OPEN_OK);

    return true;
}

void article_close(article_stream_t *stream)
{
    if (stream == NULL) {
        return;
    }

    if (stream->handle != 0u) {
        ti_Close(stream->handle);
    }

    stream_reset(stream);
}

bool article_read_bytes(const article_stream_t *stream, size_t text_offset, uint8_t *out, size_t len)
{
    size_t raw_offset;

    if (stream == NULL || out == NULL || stream->handle == 0u) {
        return false;
    }

    if (len == 0u) {
        return true;
    }

    if (text_offset > stream->text_len || len > stream->text_len - text_offset) {
        return false;
    }

    raw_offset = stream->data_offset + text_offset;
    if (raw_offset > (size_t)INT_MAX) {
        return false;
    }

    if (ti_Seek((int)raw_offset, SEEK_SET, stream->handle) == EOF) {
        return false;
    }

    return ti_Read(out, 1u, len, stream->handle) == len;
}

size_t article_length(const article_stream_t *stream)
{
    if (stream == NULL) {
        return 0u;
    }

    return stream->text_len;
}

uint16_t article_toc_count(const article_stream_t *stream)
{
    if (stream == NULL) {
        return 0u;
    }

    return stream->toc_count;
}

bool article_read_toc_entry(const article_stream_t *stream, uint16_t index, article_toc_entry_t *out_entry)
{
    uint16_t i;
    size_t pos;

    if (stream == NULL || out_entry == NULL || stream->handle == 0u) {
        return false;
    }

    if (index >= stream->toc_count || stream->toc_offset == 0u) {
        return false;
    }

    pos = stream->toc_offset;
    for (i = 0u; i <= index; i++) {
        uint8_t header[5];
        uint32_t text_offset;
        uint8_t title_len;

        if (pos > (size_t)INT_MAX || ti_Seek((int)pos, SEEK_SET, stream->handle) == EOF) {
            return false;
        }

        if (ti_Read(header, 1u, sizeof(header), stream->handle) != sizeof(header)) {
            return false;
        }

        text_offset = read_u32_le(header);
        title_len = header[4];

        if (title_len > ARTICLE_TOC_TITLE_MAX) {
            return false;
        }

        if (i == index) {
            if (ti_Read(out_entry->title, 1u, title_len, stream->handle) != title_len) {
                return false;
            }

            out_entry->text_offset = text_offset;
            out_entry->title_len = title_len;
            out_entry->title[title_len] = '\0';
            return true;
        }

        pos += sizeof(header) + title_len;
    }

    return false;
}

const char *article_name(const article_stream_t *stream)
{
    if (stream == NULL) {
        return "";
    }

    return stream->name;
}

const char *article_font_name(const article_stream_t *stream)
{
    if (stream == NULL || stream->font_name[0] == '\0') {
        return ARTICLE_DEFAULT_FONT_APPVAR;
    }

    return stream->font_name;
}

const char *article_open_error_text(article_open_error_t error)
{
    switch (error) {
    case ARTICLE_OPEN_OK:
        return RES_TXT_ARTICLE_OPEN_OK;
    case ARTICLE_OPEN_INVALID_ARG:
        return RES_TXT_ARTICLE_INVALID_ARG;
    case ARTICLE_OPEN_NOT_FOUND:
        return RES_TXT_ARTICLE_NOT_FOUND;
    case ARTICLE_OPEN_TOO_SMALL:
        return RES_TXT_ARTICLE_TOO_SMALL;
    case ARTICLE_OPEN_READ_FAIL:
        return RES_TXT_ARTICLE_READ_FAIL;
    case ARTICLE_OPEN_BAD_MAGIC:
        return RES_TXT_ARTICLE_BAD_MAGIC;
    case ARTICLE_OPEN_BAD_VERSION:
        return RES_TXT_ARTICLE_BAD_VERSION;
    case ARTICLE_OPEN_BAD_HEADER:
        return RES_TXT_ARTICLE_BAD_HEADER;
    case ARTICLE_OPEN_BAD_LENGTH:
        return RES_TXT_ARTICLE_BAD_LENGTH;
    default:
        return RES_TXT_ARTICLE_UNKNOWN_ERROR;
    }
}
