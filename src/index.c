#include "index.h"
#include "util.h"

#include <errno.h>
#include <limits.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static IndexFaultPoint g_index_fault = INDEX_FAULT_NONE;

void index_set_fault_point(IndexFaultPoint point) {
    g_index_fault = point;
}

static int consume_index_fault(IndexFaultPoint point) {
    if (g_index_fault == point) {
        g_index_fault = INDEX_FAULT_NONE;
        errno = EIO;
        return 1;
    }
    return 0;
}

unsigned int index_hash(int32_t key) {
    uint32_t value = (uint32_t)key;
    value ^= value >> 16;
    value *= 0x7FEB352Du;
    value ^= value >> 15;
    value *= 0x846CA68Bu;
    value ^= value >> 16;
    return (unsigned int)value;
}

static bool bucket_count_for_entries(size_t expected_entries, size_t *out) {
    size_t buckets = INDEX_INITIAL_BUCKETS;
    size_t threshold;
    if (out == NULL) {
        return false;
    }
    for (;;) {
        threshold = (buckets / INDEX_MAX_LOAD_DEN) * INDEX_MAX_LOAD_NUM;
        if (expected_entries <= threshold) {
            *out = buckets;
            return true;
        }
        if (buckets > SIZE_MAX / 2u) {
            return false;
        }
        buckets *= 2u;
    }
}

static Index *index_create_sized(const char *table_name, size_t expected_entries) {
    Index *index;
    size_t bucket_count;
    size_t bytes;

    if (!storage_valid_table_name(table_name) ||
        !bucket_count_for_entries(expected_entries, &bucket_count) ||
        !util_mul_size(bucket_count, sizeof(IndexNode *), &bytes)) {
        return NULL;
    }
    index = (Index *)calloc(1, sizeof(Index));
    if (index == NULL) {
        return NULL;
    }
    index->buckets = (IndexNode **)calloc(1, bytes);
    if (index->buckets == NULL) {
        free(index);
        return NULL;
    }
    if (!util_copy_checked(index->table_name, sizeof(index->table_name), table_name)) {
        free(index->buckets);
        free(index);
        return NULL;
    }
    index->bucket_count = bucket_count;
    return index;
}

Index *index_create(const char *table_name) {
    return index_create_sized(table_name, 0);
}

void index_free(Index *index) {
    size_t i;
    if (index == NULL) {
        return;
    }
    for (i = 0; i < index->bucket_count; i++) {
        IndexNode *node = index->buckets[i];
        while (node != NULL) {
            IndexNode *next = node->next;
            free(node);
            node = next;
        }
    }
    free(index->buckets);
    free(index);
}

size_t index_bucket_count(const Index *index) {
    return (index == NULL) ? 0 : index->bucket_count;
}

static IndexStatus index_resize(Index *index, size_t new_count) {
    IndexNode **new_buckets;
    size_t bytes;
    size_t i;

    if (index == NULL || new_count <= index->bucket_count ||
        (new_count & (new_count - 1u)) != 0 ||
        !util_mul_size(new_count, sizeof(IndexNode *), &bytes)) {
        return INDEX_ALLOC_ERROR;
    }
    new_buckets = (IndexNode **)calloc(1, bytes);
    if (new_buckets == NULL) {
        return INDEX_ALLOC_ERROR;
    }
    for (i = 0; i < index->bucket_count; i++) {
        IndexNode *node = index->buckets[i];
        while (node != NULL) {
            IndexNode *next = node->next;
            size_t bucket = (size_t)index_hash(node->entry.key) & (new_count - 1u);
            node->next = new_buckets[bucket];
            new_buckets[bucket] = node;
            node = next;
        }
    }
    free(index->buckets);
    index->buckets = new_buckets;
    index->bucket_count = new_count;
    return INDEX_OK;
}

IndexStatus index_insert(Index *index, int32_t key, int64_t file_offset, int replace) {
    size_t bucket;
    size_t threshold;
    IndexNode *node;
    IndexNode *new_node;

    if (index == NULL || index->buckets == NULL || index->bucket_count == 0) {
        return INDEX_ALLOC_ERROR;
    }
    bucket = (size_t)index_hash(key) & (index->bucket_count - 1u);
    for (node = index->buckets[bucket]; node != NULL; node = node->next) {
        if (node->entry.key == key) {
            if (!replace) {
                return INDEX_DUPLICATE_KEY;
            }
            node->entry.file_offset = file_offset;
            return INDEX_OK;
        }
    }

    threshold = (index->bucket_count / INDEX_MAX_LOAD_DEN) * INDEX_MAX_LOAD_NUM;
    if (index->entry_count >= threshold) {
        if (index->bucket_count > SIZE_MAX / 2u) {
            return INDEX_ALLOC_ERROR;
        }
        if (index_resize(index, index->bucket_count * 2u) != INDEX_OK) {
            return INDEX_ALLOC_ERROR;
        }
        bucket = (size_t)index_hash(key) & (index->bucket_count - 1u);
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
    size_t bucket;
    IndexNode *node;
    if (index == NULL || index->buckets == NULL || index->bucket_count == 0 ||
        out_offset == NULL) {
        return INDEX_ALLOC_ERROR;
    }
    bucket = (size_t)index_hash(key) & (index->bucket_count - 1u);
    for (node = index->buckets[bucket]; node != NULL; node = node->next) {
        if (node->entry.key == key) {
            *out_offset = node->entry.file_offset;
            return INDEX_OK;
        }
    }
    return INDEX_NOT_FOUND;
}

IndexStatus index_persist(const Index *index) {
    char path[96];
    char temp_path[128];
    int fd = -1;
    FILE *f = NULL;
    size_t i;
    uint32_t count;
    int saved_errno;

    if (index == NULL || index->entry_count > UINT32_MAX ||
        !storage_index_path(index->table_name, path, sizeof(path))) {
        return INDEX_PERSIST_ERROR;
    }
    if (!util_secure_temp_file(path, temp_path, sizeof(temp_path), &fd)) {
        return (errno == EACCES || errno == EROFS) ? INDEX_READ_ONLY : INDEX_TEMPFILE_ERROR;
    }
    f = fdopen(fd, "wb");
    if (f == NULL) {
        saved_errno = errno;
        close(fd);
        unlink(temp_path);
        errno = saved_errno;
        return INDEX_TEMPFILE_ERROR;
    }
    count = (uint32_t)index->entry_count;
    if (!util_write_u32_le(f, INDEX_MAGIC) ||
        !util_write_u32_le(f, INDEX_VERSION) ||
        !util_write_u32_le(f, INDEX_HEADER_SIZE) ||
        !util_write_u32_le(f, INDEX_ENTRY_ONDISK) ||
        !util_write_u32_le(f, count) ||
        !util_write_u32_le(f, 0u)) {
        goto temp_failure;
    }
    {
        unsigned char padding[INDEX_HEADER_SIZE - 24u];
        memset(padding, 0, sizeof(padding));
        if (fwrite(padding, 1, sizeof(padding), f) != sizeof(padding)) {
            goto temp_failure;
        }
    }
    if (consume_index_fault(INDEX_FAULT_DURING_TEMP_WRITE)) {
        goto temp_failure;
    }
    for (i = 0; i < index->bucket_count; i++) {
        IndexNode *node;
        for (node = index->buckets[i]; node != NULL; node = node->next) {
            if (!util_write_i32_le(f, node->entry.key) ||
                !util_write_i64_le(f, node->entry.file_offset)) {
                goto temp_failure;
            }
        }
    }
    if (!util_flush_and_sync(f)) {
        goto temp_failure;
    }
    if (fclose(f) != 0) {
        f = NULL;
        saved_errno = errno;
        unlink(temp_path);
        errno = saved_errno;
        return INDEX_PERSIST_ERROR;
    }
    f = NULL;
    if (consume_index_fault(INDEX_FAULT_BEFORE_RENAME)) {
        saved_errno = errno;
        unlink(temp_path);
        errno = saved_errno;
        return INDEX_PERSIST_ERROR;
    }
    if (rename(temp_path, path) != 0) {
        saved_errno = errno;
        unlink(temp_path);
        errno = saved_errno;
        return INDEX_PERSIST_ERROR;
    }
    if (consume_index_fault(INDEX_FAULT_AFTER_RENAME_BEFORE_DIR_SYNC)) {
        return INDEX_PERSIST_ERROR;
    }
    if (!util_sync_parent_directory(path)) {
        return INDEX_PERSIST_ERROR;
    }
    return INDEX_OK;

temp_failure:
    saved_errno = errno;
    if (f != NULL) {
        fclose(f);
    }
    unlink(temp_path);
    errno = saved_errno;
    return INDEX_PERSIST_ERROR;
}

IndexStatus index_load(const char *table_name, Index **out) {
    char path[96];
    FILE *f = NULL;
    Index *index = NULL;
    Table *table = NULL;
    TableStatus table_status;
    uint32_t magic, version, header_size, entry_size, entry_count, reserved;
    uint32_t i;
    long file_size;
    uint64_t entries_size;
    uint64_t expected_size;
    IndexStatus result = INDEX_CORRUPT;

    if (out == NULL || !storage_valid_table_name(table_name) ||
        !storage_index_path(table_name, path, sizeof(path))) {
        return INDEX_ALLOC_ERROR;
    }
    *out = NULL;
    table_status = storage_open_table_readonly(table_name, &table);
    if (table_status != TABLE_OK) {
        return (table_status == TABLE_NOT_FOUND) ? INDEX_NOT_FOUND : INDEX_IO_ERROR;
    }
    f = fopen(path, "rb");
    if (f == NULL) {
        result = (errno == ENOENT) ? INDEX_NOT_FOUND : INDEX_IO_ERROR;
        goto done;
    }
    if (fseek(f, 0, SEEK_END) != 0 || (file_size = ftell(f)) < 0 ||
        fseek(f, 0, SEEK_SET) != 0) {
        result = INDEX_IO_ERROR;
        goto done;
    }
    if ((uint64_t)file_size < (uint64_t)INDEX_HEADER_SIZE) {
        result = INDEX_INCOMPATIBLE;
        goto done;
    }
    if (!util_read_u32_le(f, &magic) || !util_read_u32_le(f, &version) ||
        !util_read_u32_le(f, &header_size) || !util_read_u32_le(f, &entry_size) ||
        !util_read_u32_le(f, &entry_count) || !util_read_u32_le(f, &reserved)) {
        goto done;
    }
    if (magic != INDEX_MAGIC || version != INDEX_VERSION ||
        header_size != INDEX_HEADER_SIZE || entry_size != INDEX_ENTRY_ONDISK ||
        reserved != 0u) {
        result = INDEX_INCOMPATIBLE;
        goto done;
    }
    if ((uint64_t)entry_count != table->row_count ||
        !util_mul_u64((uint64_t)entry_count, (uint64_t)INDEX_ENTRY_ONDISK,
                      &entries_size) ||
        !util_add_u64((uint64_t)INDEX_HEADER_SIZE, entries_size, &expected_size) ||
        expected_size != (uint64_t)file_size) {
        goto done;
    }
#if SIZE_MAX < UINT32_MAX
    if (entry_count > (uint32_t)SIZE_MAX) {
        goto done;
    }
#endif
    if (fseek(f, (long)INDEX_HEADER_SIZE, SEEK_SET) != 0) {
        result = INDEX_IO_ERROR;
        goto done;
    }
    index = index_create_sized(table_name, (size_t)entry_count);
    if (index == NULL) {
        result = INDEX_ALLOC_ERROR;
        goto done;
    }
    for (i = 0; i < entry_count; i++) {
        int32_t key;
        int64_t offset;
        IndexStatus insert_status;
        if (!util_read_i32_le(f, &key) || !util_read_i64_le(f, &offset)) {
            goto done;
        }
        insert_status = index_insert(index, key, offset, 0);
        if (insert_status == INDEX_DUPLICATE_KEY) {
            goto done;
        }
        if (insert_status != INDEX_OK) {
            result = insert_status;
            goto done;
        }
    }
    *out = index;
    index = NULL;
    result = INDEX_OK;

done:
    index_free(index);
    if (f != NULL) {
        fclose(f);
    }
    storage_close_table(table);
    return result;
}

IndexStatus index_rebuild_from_table(const char *table_path, const char *index_path) {
    char table_name[SQL_TABLE_NAME_CAPACITY];
    char derived_index[96];
    const char *base;
    const char *slash;
    size_t length;
    Table *table = NULL;
    Index *index = NULL;
    RowScanner scan;
    Row row;
    uint64_t offset;
    ScanStatus scan_status;
    IndexStatus result;

    if (table_path == NULL) {
        return INDEX_ALLOC_ERROR;
    }
    slash = strrchr(table_path, '/');
    base = (slash == NULL) ? table_path : slash + 1;
    length = strlen(base);
    if (length > 4u && strcmp(base + length - 4u, ".tbl") == 0) {
        length -= 4u;
    }
    if (length == 0 || length >= sizeof(table_name)) {
        return INDEX_ALLOC_ERROR;
    }
    memcpy(table_name, base, length);
    table_name[length] = '\0';
    if (!storage_valid_table_name(table_name)) {
        return INDEX_ALLOC_ERROR;
    }
    {
        TableStatus status = storage_open_table_readonly(table_name, &table);
        if (status != TABLE_OK) {
            return (status == TABLE_NOT_FOUND) ? INDEX_NOT_FOUND :
                   (status == TABLE_ALLOC_ERROR) ? INDEX_ALLOC_ERROR :
                   (status == TABLE_CORRUPT || status == TABLE_INCOMPATIBLE) ? INDEX_CORRUPT :
                   INDEX_IO_ERROR;
        }
    }
    if (table->row_count > (uint64_t)SIZE_MAX) {
        storage_close_table(table);
        return INDEX_ALLOC_ERROR;
    }
    index = index_create_sized(table_name, (size_t)table->row_count);
    if (index == NULL) {
        storage_close_table(table);
        return INDEX_ALLOC_ERROR;
    }
    if (storage_scan_begin(table, &scan) != TABLE_OK) {
        result = INDEX_IO_ERROR;
        goto rebuild_done;
    }
    while ((scan_status = storage_scan_next(&scan, &row, &offset)) == SCAN_OK) {
        result = index_insert(index, row.id, (int64_t)offset, 0);
        if (result == INDEX_DUPLICATE_KEY) {
            result = INDEX_DUPLICATE_PRIMARY_KEY;
            goto rebuild_done;
        }
        if (result != INDEX_OK) {
            goto rebuild_done;
        }
    }
    if (scan_status != SCAN_END) {
        result = (scan_status == SCAN_CORRUPT) ? INDEX_CORRUPT : INDEX_IO_ERROR;
        goto rebuild_done;
    }
    result = index_persist(index);
    if (result != INDEX_OK) {
        goto rebuild_done;
    }
    if (index_path != NULL && storage_index_path(table_name, derived_index,
                                                  sizeof(derived_index)) &&
        strcmp(index_path, derived_index) != 0) {
        if (rename(derived_index, index_path) != 0 ||
            !util_sync_parent_directory(index_path)) {
            result = INDEX_PERSIST_ERROR;
            goto rebuild_done;
        }
    }

rebuild_done:
    index_free(index);
    storage_close_table(table);
    return result;
}

IndexStatus index_rebuild(const char *table_name) {
    char table_path[96];
    char index_path[96];
    if (!storage_valid_table_name(table_name) ||
        !storage_table_path(table_name, table_path, sizeof(table_path)) ||
        !storage_index_path(table_name, index_path, sizeof(index_path))) {
        return INDEX_ALLOC_ERROR;
    }
    return index_rebuild_from_table(table_path, index_path);
}

IndexStatus index_load_or_rebuild(const char *table_name, Index **out) {
    IndexStatus status;
    if (out == NULL) {
        return INDEX_ALLOC_ERROR;
    }
    *out = NULL;
    status = index_load(table_name, out);
    if (status == INDEX_OK) {
        return INDEX_OK;
    }
    if (status == INDEX_NOT_FOUND || status == INDEX_CORRUPT ||
        status == INDEX_INCOMPATIBLE) {
        status = index_rebuild(table_name);
        if (status != INDEX_OK) {
            return status;
        }
        return index_load(table_name, out);
    }
    return status;
}

IndexStatus index_validate_lookup(Table *table, int32_t key, int64_t offset, Row *out_row) {
    uint64_t file_size;
    Row row;
    TableStatus status;
    if (table == NULL || offset < 0) {
        return (table == NULL) ? INDEX_IO_ERROR : INDEX_INVALID_OFFSET;
    }
    if (storage_file_size(table, &file_size) != TABLE_OK ||
        !storage_is_valid_row_offset((uint64_t)offset, file_size, table->row_count)) {
        return INDEX_INVALID_OFFSET;
    }
    status = storage_read_row_at_offset(table, (uint64_t)offset, &row);
    if (status != TABLE_OK) {
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
