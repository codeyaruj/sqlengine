#include "index.h"
#include "util.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

unsigned int index_hash(int32_t key) {
    return ((unsigned int)key * 31u) % (unsigned int)INDEX_SIZE;
}

Index *index_create(const char *table_name) {
    Index *index;
    int i;

    if (table_name == NULL) {
        return NULL;
    }

    index = (Index *)calloc(1, sizeof(Index));
    if (index == NULL) {
        return NULL;
    }

    strncpy(index->table_name, table_name, sizeof(index->table_name) - 1);
    index->table_name[sizeof(index->table_name) - 1] = '\0';
    index->entry_count = 0;

    for (i = 0; i < INDEX_SIZE; i++) {
        index->buckets[i] = NULL;
    }
    return index;
}

void index_free(Index *index) {
    int i;
    if (index == NULL) {
        return;
    }
    for (i = 0; i < INDEX_SIZE; i++) {
        IndexNode *node = index->buckets[i];
        while (node != NULL) {
            IndexNode *next = node->next;
            free(node);
            node = next;
        }
    }
    free(index);
}

IndexStatus index_insert(Index *index, int32_t key, int64_t file_offset, int replace) {
    unsigned int bucket;
    IndexNode *node;
    IndexNode *new_node;

    if (index == NULL) {
        return INDEX_ALLOC_ERROR;
    }

    bucket = index_hash(key);
    node = index->buckets[bucket];
    while (node != NULL) {
        if (node->entry.key == key) {
            if (!replace) {
                return INDEX_DUPLICATE_KEY;
            }
            node->entry.file_offset = file_offset;
            return INDEX_OK;
        }
        node = node->next;
    }

    new_node = (IndexNode *)malloc(sizeof(IndexNode));
    if (new_node == NULL) {
        return INDEX_ALLOC_ERROR;
    }
    new_node->entry.key = key;
    new_node->entry.file_offset = file_offset;
    new_node->next = index->buckets[bucket];
    index->buckets[bucket] = new_node;
    index->entry_count++;
    return INDEX_OK;
}

IndexStatus index_lookup(const Index *index, int32_t key, int64_t *out_offset) {
    unsigned int bucket;
    IndexNode *node;

    if (index == NULL || out_offset == NULL) {
        return INDEX_ALLOC_ERROR;
    }

    bucket = index_hash(key);
    node = index->buckets[bucket];
    while (node != NULL) {
        if (node->entry.key == key) {
            *out_offset = node->entry.file_offset;
            return INDEX_OK;
        }
        node = node->next;
    }
    return INDEX_NOT_FOUND;
}

/*
 * Index file layout (little-endian, version 1):
 *   u32 magic, u32 version, u32 header_size, u32 entry_size,
 *   u32 entry_count, u32 reserved, then entry_count * (i32 key + i64 offset)
 *
 * Legacy raw IndexEntry dumps are rejected as INDEX_INCOMPATIBLE.
 */
IndexStatus index_persist(const Index *index) {
    char path[96];
    char tmp_path[112];
    FILE *f;
    int i;
    uint32_t count;

    if (index == NULL) {
        return INDEX_ALLOC_ERROR;
    }
    if (index->entry_count > UINT32_MAX) {
        return INDEX_PERSIST_ERROR;
    }

    storage_index_path(index->table_name, path, sizeof(path));
    snprintf(tmp_path, sizeof(tmp_path), "%s.tmp", path);

    f = fopen(tmp_path, "wb");
    if (f == NULL) {
        return INDEX_IO_ERROR;
    }

    count = (uint32_t)index->entry_count;

    if (!util_write_u32_le(f, INDEX_MAGIC) ||
        !util_write_u32_le(f, INDEX_VERSION) ||
        !util_write_u32_le(f, INDEX_HEADER_SIZE) ||
        !util_write_u32_le(f, INDEX_ENTRY_ONDISK) ||
        !util_write_u32_le(f, count) ||
        !util_write_u32_le(f, 0u)) {
        fclose(f);
        remove(tmp_path);
        return INDEX_IO_ERROR;
    }

    /* Pad header to INDEX_HEADER_SIZE (6 × u32 = 24 bytes written above). */
    {
        unsigned char pad[INDEX_HEADER_SIZE];
        size_t written = 24;
        memset(pad, 0, sizeof(pad));
        if (INDEX_HEADER_SIZE > written) {
            if (fwrite(pad + written, 1, INDEX_HEADER_SIZE - written, f) !=
                INDEX_HEADER_SIZE - written) {
                fclose(f);
                remove(tmp_path);
                return INDEX_IO_ERROR;
            }
        }
    }

    for (i = 0; i < INDEX_SIZE; i++) {
        IndexNode *node = index->buckets[i];
        while (node != NULL) {
            if (!util_write_i32_le(f, node->entry.key) ||
                !util_write_i64_le(f, node->entry.file_offset)) {
                fclose(f);
                remove(tmp_path);
                return INDEX_IO_ERROR;
            }
            node = node->next;
        }
    }

    if (fflush(f) != 0) {
        fclose(f);
        remove(tmp_path);
        return INDEX_IO_ERROR;
    }
    if (fclose(f) != 0) {
        remove(tmp_path);
        return INDEX_IO_ERROR;
    }

    if (rename(tmp_path, path) != 0) {
        remove(tmp_path);
        return INDEX_PERSIST_ERROR;
    }
    return INDEX_OK;
}

IndexStatus index_load(const char *table_name, Index **out) {
    char path[96];
    FILE *f;
    Index *index;
    uint32_t magic, version, header_size, entry_size, entry_count, reserved;
    uint32_t i;
    long file_size;
    uint64_t expected;

    if (out == NULL) {
        return INDEX_ALLOC_ERROR;
    }
    *out = NULL;

    if (table_name == NULL) {
        return INDEX_ALLOC_ERROR;
    }

    storage_index_path(table_name, path, sizeof(path));
    f = fopen(path, "rb");
    if (f == NULL) {
        return INDEX_NOT_FOUND;
    }

    if (fseek(f, 0, SEEK_END) != 0) {
        fclose(f);
        return INDEX_IO_ERROR;
    }
    file_size = ftell(f);
    if (file_size < 0) {
        fclose(f);
        return INDEX_IO_ERROR;
    }
    if ((uint64_t)file_size < (uint64_t)INDEX_HEADER_SIZE) {
        fclose(f);
        return INDEX_INCOMPATIBLE;
    }
    if (fseek(f, 0, SEEK_SET) != 0) {
        fclose(f);
        return INDEX_IO_ERROR;
    }

    if (!util_read_u32_le(f, &magic) ||
        !util_read_u32_le(f, &version) ||
        !util_read_u32_le(f, &header_size) ||
        !util_read_u32_le(f, &entry_size) ||
        !util_read_u32_le(f, &entry_count) ||
        !util_read_u32_le(f, &reserved)) {
        fclose(f);
        return INDEX_CORRUPT;
    }

    if (magic != INDEX_MAGIC) {
        fclose(f);
        return INDEX_INCOMPATIBLE;
    }
    if (version != INDEX_VERSION) {
        fclose(f);
        return INDEX_INCOMPATIBLE;
    }
    if (header_size != INDEX_HEADER_SIZE || entry_size != INDEX_ENTRY_ONDISK) {
        fclose(f);
        return INDEX_INCOMPATIBLE;
    }

    /* Reject absurd counts. */
    if (entry_count > 10000000u) {
        fclose(f);
        return INDEX_CORRUPT;
    }

    expected = (uint64_t)INDEX_HEADER_SIZE + (uint64_t)entry_count * (uint64_t)INDEX_ENTRY_ONDISK;
    if ((uint64_t)file_size < expected) {
        fclose(f);
        return INDEX_CORRUPT;
    }

    /* Seek to first entry (skip header padding). */
    if (fseek(f, (long)INDEX_HEADER_SIZE, SEEK_SET) != 0) {
        fclose(f);
        return INDEX_IO_ERROR;
    }

    index = index_create(table_name);
    if (index == NULL) {
        fclose(f);
        return INDEX_ALLOC_ERROR;
    }

    for (i = 0; i < entry_count; i++) {
        int32_t key;
        int64_t off;
        IndexStatus st;

        if (!util_read_i32_le(f, &key) || !util_read_i64_le(f, &off)) {
            index_free(index);
            fclose(f);
            return INDEX_CORRUPT;
        }
        st = index_insert(index, key, off, 1);
        if (st != INDEX_OK) {
            index_free(index);
            fclose(f);
            return st;
        }
    }

    fclose(f);
    *out = index;
    return INDEX_OK;
}

IndexStatus index_rebuild_from_table(const char *table_path, const char *index_path) {
    /* Derive table name from table_path ending in .tbl */
    char table_name[32];
    size_t len;
    const char *base;
    const char *slash;
    Table *table = NULL;
    TableStatus tst;
    Index *index;
    RowScanner scan;
    Row row;
    uint64_t offset;
    ScanStatus sst;
    IndexStatus ist;
    char derived_index[96];

    if (table_path == NULL) {
        return INDEX_ALLOC_ERROR;
    }

    slash = strrchr(table_path, '/');
    base = (slash != NULL) ? slash + 1 : table_path;
    len = strlen(base);
    if (len > 4 && strcmp(base + len - 4, ".tbl") == 0) {
        len -= 4;
    }
    if (len == 0 || len >= sizeof(table_name)) {
        return INDEX_ALLOC_ERROR;
    }
    memcpy(table_name, base, len);
    table_name[len] = '\0';

    tst = storage_open_table(table_name, &table);
    if (tst == TABLE_NOT_FOUND) {
        return INDEX_NOT_FOUND;
    }
    if (tst != TABLE_OK) {
        return (tst == TABLE_ALLOC_ERROR) ? INDEX_ALLOC_ERROR :
               (tst == TABLE_CORRUPT || tst == TABLE_INCOMPATIBLE) ? INDEX_CORRUPT :
               INDEX_IO_ERROR;
    }

    index = index_create(table_name);
    if (index == NULL) {
        storage_close_table(table);
        return INDEX_ALLOC_ERROR;
    }

    if (storage_scan_begin(table, &scan) != TABLE_OK) {
        index_free(index);
        storage_close_table(table);
        return INDEX_IO_ERROR;
    }

    while ((sst = storage_scan_next(&scan, &row, &offset)) == SCAN_OK) {
        ist = index_insert(index, row.id, (int64_t)offset, 1);
        if (ist != INDEX_OK) {
            index_free(index);
            storage_close_table(table);
            return ist;
        }
    }
    if (sst != SCAN_END) {
        index_free(index);
        storage_close_table(table);
        return (sst == SCAN_CORRUPT) ? INDEX_CORRUPT : INDEX_IO_ERROR;
    }

    storage_close_table(table);

    /* Persist to requested path: temporarily rename via index_persist uses table_name.idx.
     * If index_path differs, persist then rename. */
    ist = index_persist(index);
    if (ist != INDEX_OK) {
        index_free(index);
        return ist;
    }

    if (index_path != NULL) {
        storage_index_path(table_name, derived_index, sizeof(derived_index));
        if (strcmp(derived_index, index_path) != 0) {
            if (rename(derived_index, index_path) != 0) {
                index_free(index);
                return INDEX_PERSIST_ERROR;
            }
        }
    }

    index_free(index);
    return INDEX_OK;
}

IndexStatus index_rebuild(const char *table_name) {
    char tbl[96];
    char idx[96];
    if (table_name == NULL) {
        return INDEX_ALLOC_ERROR;
    }
    storage_table_path(table_name, tbl, sizeof(tbl));
    storage_index_path(table_name, idx, sizeof(idx));
    return index_rebuild_from_table(tbl, idx);
}

IndexStatus index_load_or_rebuild(const char *table_name, Index **out) {
    IndexStatus st;

    if (out == NULL) {
        return INDEX_ALLOC_ERROR;
    }
    *out = NULL;

    st = index_load(table_name, out);
    if (st == INDEX_OK) {
        return INDEX_OK;
    }

    /* Missing, corrupt, incompatible, truncated: rebuild from table. */
    if (st == INDEX_NOT_FOUND || st == INDEX_CORRUPT || st == INDEX_INCOMPATIBLE) {
        st = index_rebuild(table_name);
        if (st != INDEX_OK) {
            return st;
        }
        return index_load(table_name, out);
    }
    return st;
}

IndexStatus index_validate_lookup(Table *table, int32_t key, int64_t offset, Row *out_row) {
    uint64_t file_size;
    Row row;
    TableStatus st;

    if (table == NULL) {
        return INDEX_IO_ERROR;
    }
    if (offset < 0) {
        return INDEX_INVALID_OFFSET;
    }

    if (storage_file_size(table, &file_size) != TABLE_OK) {
        return INDEX_IO_ERROR;
    }

    if (!storage_is_valid_row_offset((uint64_t)offset, file_size, table->row_count)) {
        return INDEX_INVALID_OFFSET;
    }

    st = storage_read_row_at_offset(table, (uint64_t)offset, &row);
    if (st != TABLE_OK) {
        return INDEX_INVALID_OFFSET;
    }

    if (row.id != key) {
        return INDEX_KEY_MISMATCH;
    }

    if (out_row != NULL) {
        *out_row = row;
    }
    return INDEX_OK;
}
