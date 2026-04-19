#include "state.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include <fileioc.h>

#include "article.h"

typedef struct {
    uint8_t used;
    char name[ARTICLE_VAR_NAME_MAX + 1u];
    uint32_t offset;
} pos_entry_t;

typedef struct {
    pos_entry_t entries[STATE_MAX_DOCS];
} pos_db_t;

typedef struct {
    uint8_t used;
    char name[ARTICLE_VAR_NAME_MAX + 1u];
    uint8_t count;
    uint32_t offsets[STATE_MAX_BOOKMARKS_PER_DOC];
} bookmark_doc_t;

typedef struct {
    bookmark_doc_t docs[STATE_MAX_DOCS];
} bookmark_db_t;

static pos_db_t g_pos_db;
static bookmark_db_t g_bookmark_db;

static void copy_name(char *dst, const char *src)
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

static bool names_equal(const char *a, const char *b)
{
    uint8_t i;

    if (a == NULL || b == NULL) {
        return false;
    }

    for (i = 0u; i < ARTICLE_VAR_NAME_MAX + 1u; i++) {
        if (a[i] != b[i]) {
            return false;
        }
        if (a[i] == '\0') {
            return true;
        }
    }

    return true;
}

static bool load_pos_db(pos_db_t *db)
{
    uint8_t handle;

    if (db == NULL) {
        return false;
    }

    memset(db, 0, sizeof(*db));

    handle = ti_OpenVar(STATE_POS_APPVAR, "r", OS_TYPE_APPVAR);
    if (handle == 0u) {
        return true;
    }

    if (ti_Read(db, 1u, sizeof(*db), handle) != sizeof(*db)) {
        ti_Close(handle);
        memset(db, 0, sizeof(*db));
        return false;
    }

    ti_Close(handle);
    return true;
}

static bool save_pos_db(const pos_db_t *db)
{
    uint8_t handle;

    if (db == NULL) {
        return false;
    }

    handle = ti_OpenVar(STATE_POS_APPVAR, "w", OS_TYPE_APPVAR);
    if (handle == 0u) {
        return false;
    }

    if (ti_Write(db, 1u, sizeof(*db), handle) != sizeof(*db)) {
        ti_Close(handle);
        return false;
    }

    ti_SetArchiveStatus(true, handle);
    ti_Close(handle);
    return true;
}

static bool load_bookmark_db(bookmark_db_t *db)
{
    uint8_t handle;

    if (db == NULL) {
        return false;
    }

    memset(db, 0, sizeof(*db));

    handle = ti_OpenVar(STATE_BOOKMARK_APPVAR, "r", OS_TYPE_APPVAR);
    if (handle == 0u) {
        return true;
    }

    if (ti_Read(db, 1u, sizeof(*db), handle) != sizeof(*db)) {
        ti_Close(handle);
        memset(db, 0, sizeof(*db));
        return false;
    }

    ti_Close(handle);
    return true;
}

static bool save_bookmark_db(const bookmark_db_t *db)
{
    uint8_t handle;

    if (db == NULL) {
        return false;
    }

    handle = ti_OpenVar(STATE_BOOKMARK_APPVAR, "w", OS_TYPE_APPVAR);
    if (handle == 0u) {
        return false;
    }

    if (ti_Write(db, 1u, sizeof(*db), handle) != sizeof(*db)) {
        ti_Close(handle);
        return false;
    }

    ti_SetArchiveStatus(true, handle);
    ti_Close(handle);
    return true;
}

static int find_pos_slot(const pos_db_t *db, const char *doc_name)
{
    uint8_t i;

    if (db == NULL || doc_name == NULL) {
        return -1;
    }

    for (i = 0u; i < STATE_MAX_DOCS; i++) {
        if (db->entries[i].used != 0u && names_equal(db->entries[i].name, doc_name)) {
            return (int)i;
        }
    }

    return -1;
}

static int find_empty_pos_slot(const pos_db_t *db)
{
    uint8_t i;

    if (db == NULL) {
        return -1;
    }

    for (i = 0u; i < STATE_MAX_DOCS; i++) {
        if (db->entries[i].used == 0u) {
            return (int)i;
        }
    }

    return -1;
}

static int find_bookmark_doc(const bookmark_db_t *db, const char *doc_name)
{
    uint8_t i;

    if (db == NULL || doc_name == NULL) {
        return -1;
    }

    for (i = 0u; i < STATE_MAX_DOCS; i++) {
        if (db->docs[i].used != 0u && names_equal(db->docs[i].name, doc_name)) {
            return (int)i;
        }
    }

    return -1;
}

static int find_empty_bookmark_doc(const bookmark_db_t *db)
{
    uint8_t i;

    if (db == NULL) {
        return -1;
    }

    for (i = 0u; i < STATE_MAX_DOCS; i++) {
        if (db->docs[i].used == 0u) {
            return (int)i;
        }
    }

    return -1;
}

bool state_load_last_position(const char *doc_name, size_t *out_offset)
{
    int slot;

    if (doc_name == NULL || out_offset == NULL) {
        return false;
    }

    if (!load_pos_db(&g_pos_db)) {
        return false;
    }

    slot = find_pos_slot(&g_pos_db, doc_name);
    if (slot < 0) {
        return false;
    }

    *out_offset = g_pos_db.entries[slot].offset;
    return true;
}

bool state_save_last_position(const char *doc_name, size_t offset)
{
    int slot;

    if (doc_name == NULL || doc_name[0] == '\0') {
        return false;
    }

    if (!load_pos_db(&g_pos_db)) {
        return false;
    }

    slot = find_pos_slot(&g_pos_db, doc_name);
    if (slot < 0) {
        slot = find_empty_pos_slot(&g_pos_db);
    }
    if (slot < 0) {
        slot = 0;
    }

    g_pos_db.entries[slot].used = 1u;
    copy_name(g_pos_db.entries[slot].name, doc_name);
    g_pos_db.entries[slot].offset = (uint32_t)offset;

    return save_pos_db(&g_pos_db);
}

uint8_t state_get_bookmarks(const char *doc_name, size_t *out_offsets, uint8_t max_out)
{
    int slot;
    uint8_t i;
    uint8_t count;

    if (doc_name == NULL || out_offsets == NULL || max_out == 0u) {
        return 0u;
    }

    if (!load_bookmark_db(&g_bookmark_db)) {
        return 0u;
    }

    slot = find_bookmark_doc(&g_bookmark_db, doc_name);
    if (slot < 0) {
        return 0u;
    }

    count = g_bookmark_db.docs[slot].count;
    if (count > STATE_MAX_BOOKMARKS_PER_DOC) {
        count = STATE_MAX_BOOKMARKS_PER_DOC;
    }
    if (count > max_out) {
        count = max_out;
    }

    for (i = 0u; i < count; i++) {
        out_offsets[i] = g_bookmark_db.docs[slot].offsets[i];
    }

    return count;
}

bool state_toggle_bookmark(const char *doc_name, size_t offset, bool *out_added)
{
    int slot;
    uint8_t i;

    if (out_added != NULL) {
        *out_added = false;
    }

    if (doc_name == NULL || doc_name[0] == '\0') {
        return false;
    }

    if (!load_bookmark_db(&g_bookmark_db)) {
        return false;
    }

    slot = find_bookmark_doc(&g_bookmark_db, doc_name);
    if (slot < 0) {
        slot = find_empty_bookmark_doc(&g_bookmark_db);
        if (slot < 0) {
            slot = 0;
        }
        memset(&g_bookmark_db.docs[slot], 0, sizeof(g_bookmark_db.docs[slot]));
        g_bookmark_db.docs[slot].used = 1u;
        copy_name(g_bookmark_db.docs[slot].name, doc_name);
    }

    for (i = 0u; i < g_bookmark_db.docs[slot].count && i < STATE_MAX_BOOKMARKS_PER_DOC; i++) {
        if (g_bookmark_db.docs[slot].offsets[i] == (uint32_t)offset) {
            uint8_t j;
            for (j = i + 1u; j < g_bookmark_db.docs[slot].count; j++) {
                g_bookmark_db.docs[slot].offsets[j - 1u] = g_bookmark_db.docs[slot].offsets[j];
            }
            if (g_bookmark_db.docs[slot].count > 0u) {
                g_bookmark_db.docs[slot].count--;
            }
            return save_bookmark_db(&g_bookmark_db);
        }
    }

    if (g_bookmark_db.docs[slot].count < STATE_MAX_BOOKMARKS_PER_DOC) {
        g_bookmark_db.docs[slot].offsets[g_bookmark_db.docs[slot].count] = (uint32_t)offset;
        g_bookmark_db.docs[slot].count++;
    } else {
        memmove(g_bookmark_db.docs[slot].offsets,
            g_bookmark_db.docs[slot].offsets + 1u,
            (STATE_MAX_BOOKMARKS_PER_DOC - 1u) * sizeof(g_bookmark_db.docs[slot].offsets[0]));
        g_bookmark_db.docs[slot].offsets[STATE_MAX_BOOKMARKS_PER_DOC - 1u] = (uint32_t)offset;
    }

    if (out_added != NULL) {
        *out_added = true;
    }

    return save_bookmark_db(&g_bookmark_db);
}
