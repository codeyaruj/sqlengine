#include "storage.h"
#include "util.h"

#include <errno.h>
#include <stdlib.h>
#include <string.h>

/*
 * Table file layout (little-endian, version 1):
 *
 *   offset 0:  Table header (STORAGE_HEADER_SIZE bytes)
 *     u32 magic          = STORAGE_TABLE_MAGIC ("SQLT")
 *     u32 version        = STORAGE_TABLE_VERSION
 *     u32 header_size    = STORAGE_HEADER_SIZE
 *     u32 page_size      = STORAGE_PAGE_SIZE
 *     u32 row_size       = STORAGE_ROW_SIZE
 *     u32 reserved0      = 0
 *     u64 row_count
 *     remaining bytes zero-padded to STORAGE_HEADER_SIZE
 *
 *   offset STORAGE_HEADER_SIZE: data pages of STORAGE_PAGE_SIZE bytes each.
 *   Each page holds storage_rows_per_page() rows of STORAGE_ROW_SIZE bytes,
 *   packed from the start of the page. Unused trailing bytes in a page are
 *   padding and must never be interpreted as rows.
 *
 * Legacy files without this header are rejected (TABLE_INCOMPATIBLE).
 */

size_t storage_row_size(void) {
    return (size_t)STORAGE_ROW_SIZE;
}

size_t storage_page_size(void) {
    return (size_t)STORAGE_PAGE_SIZE;
}

size_t storage_header_size(void) {
    return (size_t)STORAGE_HEADER_SIZE;
}

size_t storage_rows_per_page(void) {
    return (size_t)(STORAGE_PAGE_SIZE / STORAGE_ROW_SIZE);
}

bool storage_offset_for_row(uint64_t row_number, uint64_t *offset) {
    size_t rpp;
    uint64_t page;
    uint64_t slot;
    uint64_t page_base;
    uint64_t slot_off;
    uint64_t data_off;

    if (offset == NULL) {
        return false;
    }

    rpp = storage_rows_per_page();
    if (rpp == 0) {
        return false;
    }

    page = row_number / (uint64_t)rpp;
    slot = row_number % (uint64_t)rpp;

    if (!util_mul_u64(page, (uint64_t)STORAGE_PAGE_SIZE, &page_base)) {
        return false;
    }
    if (!util_mul_u64(slot, (uint64_t)STORAGE_ROW_SIZE, &slot_off)) {
        return false;
    }
    if (!util_add_u64(page_base, slot_off, &data_off)) {
        return false;
    }
    if (!util_add_u64(data_off, (uint64_t)STORAGE_HEADER_SIZE, offset)) {
        return false;
    }
    return true;
}

bool storage_row_number_for_offset(uint64_t offset, uint64_t *row_number) {
    uint64_t data_off;
    uint64_t page;
    uint64_t within;
    uint64_t slot;
    size_t rpp;

    if (row_number == NULL) {
        return false;
    }
    if (offset < (uint64_t)STORAGE_HEADER_SIZE) {
        return false;
    }

    data_off = offset - (uint64_t)STORAGE_HEADER_SIZE;
    page = data_off / (uint64_t)STORAGE_PAGE_SIZE;
    within = data_off % (uint64_t)STORAGE_PAGE_SIZE;

    if (within % (uint64_t)STORAGE_ROW_SIZE != 0) {
        return false;
    }

    slot = within / (uint64_t)STORAGE_ROW_SIZE;
    rpp = storage_rows_per_page();
    if (slot >= (uint64_t)rpp) {
        return false;
    }

    if (!util_mul_u64(page, (uint64_t)rpp, row_number)) {
        return false;
    }
    if (!util_add_u64(*row_number, slot, row_number)) {
        return false;
    }
    return true;
}

bool storage_is_valid_row_offset(uint64_t offset, uint64_t file_size, uint64_t row_count) {
    uint64_t row_number;
    uint64_t expected;
    uint64_t end;

    if (!storage_row_number_for_offset(offset, &row_number)) {
        return false;
    }
    if (row_number >= row_count) {
        return false;
    }
    if (!storage_offset_for_row(row_number, &expected) || expected != offset) {
        return false;
    }
    if (!util_add_u64(offset, (uint64_t)STORAGE_ROW_SIZE, &end)) {
        return false;
    }
    if (end > file_size) {
        return false;
    }
    return true;
}

void storage_serialize_row(const Row *src, void *dest) {
    unsigned char *ptr = (unsigned char *)dest;
    int32_t id = src->id;

    ptr[0] = (unsigned char)((uint32_t)id & 0xFFu);
    ptr[1] = (unsigned char)(((uint32_t)id >> 8) & 0xFFu);
    ptr[2] = (unsigned char)(((uint32_t)id >> 16) & 0xFFu);
    ptr[3] = (unsigned char)(((uint32_t)id >> 24) & 0xFFu);
    memcpy(ptr + 4, src->name, STORAGE_NAME_SIZE);
}

void storage_deserialize_row(const void *src, Row *dest) {
    const unsigned char *ptr = (const unsigned char *)src;
    uint32_t uid;

    uid = (uint32_t)ptr[0]
        | ((uint32_t)ptr[1] << 8)
        | ((uint32_t)ptr[2] << 16)
        | ((uint32_t)ptr[3] << 24);
    dest->id = (int32_t)uid;
    memcpy(dest->name, ptr + 4, STORAGE_NAME_SIZE);
    dest->name[STORAGE_NAME_SIZE - 1] = '\0';
}

void storage_table_path(const char *table_name, char *buf, size_t buflen) {
    snprintf(buf, buflen, "%s.tbl", table_name);
}

void storage_index_path(const char *table_name, char *buf, size_t buflen) {
    snprintf(buf, buflen, "%s.idx", table_name);
}

static int valid_table_name(const char *name) {
    size_t i;
    if (name == NULL || name[0] == '\0') {
        return 0;
    }
    for (i = 0; name[i] != '\0'; i++) {
        unsigned char c = (unsigned char)name[i];
        if (!( (c >= 'a' && c <= 'z') ||
               (c >= 'A' && c <= 'Z') ||
               (c >= '0' && c <= '9') ||
               c == '_' )) {
            return 0;
        }
        if (i >= 31) {
            return 0;
        }
    }
    return 1;
}

static TableStatus write_header(FILE *f, uint64_t row_count) {
    unsigned char pad[STORAGE_HEADER_SIZE];
    size_t written;

    if (fseek(f, 0, SEEK_SET) != 0) {
        return TABLE_IO_ERROR;
    }

    memset(pad, 0, sizeof(pad));
    if (!util_write_u32_le(f, STORAGE_TABLE_MAGIC) ||
        !util_write_u32_le(f, STORAGE_TABLE_VERSION) ||
        !util_write_u32_le(f, STORAGE_HEADER_SIZE) ||
        !util_write_u32_le(f, STORAGE_PAGE_SIZE) ||
        !util_write_u32_le(f, STORAGE_ROW_SIZE) ||
        !util_write_u32_le(f, 0u) ||
        !util_write_u64_le(f, row_count)) {
        return TABLE_IO_ERROR;
    }

    /* Header fields above are 32 bytes; zero-pad the rest. */
    written = 32;
    if (fwrite(pad + written, 1, STORAGE_HEADER_SIZE - written, f) !=
        STORAGE_HEADER_SIZE - written) {
        return TABLE_IO_ERROR;
    }

    if (fflush(f) != 0) {
        return TABLE_IO_ERROR;
    }
    return TABLE_OK;
}

static TableStatus read_and_validate_header(FILE *f, uint64_t *row_count) {
    uint32_t magic, version, header_size, page_size, row_size, reserved0;
    uint64_t count;
    long file_size;
    uint64_t min_size;
    uint64_t expected_pages;
    uint64_t rpp;
    uint64_t full_pages;
    uint64_t rem;
    uint64_t expected_size;

    if (fseek(f, 0, SEEK_END) != 0) {
        return TABLE_IO_ERROR;
    }
    file_size = ftell(f);
    if (file_size < 0) {
        return TABLE_IO_ERROR;
    }
    if ((uint64_t)file_size < (uint64_t)STORAGE_HEADER_SIZE) {
        return TABLE_INCOMPATIBLE;
    }

    if (fseek(f, 0, SEEK_SET) != 0) {
        return TABLE_IO_ERROR;
    }

    if (!util_read_u32_le(f, &magic) ||
        !util_read_u32_le(f, &version) ||
        !util_read_u32_le(f, &header_size) ||
        !util_read_u32_le(f, &page_size) ||
        !util_read_u32_le(f, &row_size) ||
        !util_read_u32_le(f, &reserved0) ||
        !util_read_u64_le(f, &count)) {
        return TABLE_CORRUPT;
    }

    if (magic != STORAGE_TABLE_MAGIC) {
        return TABLE_INCOMPATIBLE;
    }
    if (version != STORAGE_TABLE_VERSION) {
        return TABLE_INCOMPATIBLE;
    }
    if (header_size != STORAGE_HEADER_SIZE) {
        return TABLE_INCOMPATIBLE;
    }
    if (page_size != STORAGE_PAGE_SIZE) {
        return TABLE_INCOMPATIBLE;
    }
    if (row_size != STORAGE_ROW_SIZE) {
        return TABLE_INCOMPATIBLE;
    }

    /* Reject absurd counts (more than file could hold). */
    rpp = (uint64_t)storage_rows_per_page();
    if (rpp == 0) {
        return TABLE_CORRUPT;
    }

    min_size = (uint64_t)STORAGE_HEADER_SIZE;
    if (count > 0) {
        full_pages = (count - 1) / rpp;
        rem = ((count - 1) % rpp) + 1;
        /* File must cover full_pages complete pages + rem rows in last page. */
        if (!util_mul_u64(full_pages, (uint64_t)STORAGE_PAGE_SIZE, &expected_pages)) {
            return TABLE_CORRUPT;
        }
        if (!util_add_u64(min_size, expected_pages, &expected_size)) {
            return TABLE_CORRUPT;
        }
        {
            uint64_t last_bytes;
            if (!util_mul_u64(rem, (uint64_t)STORAGE_ROW_SIZE, &last_bytes)) {
                return TABLE_CORRUPT;
            }
            if (!util_add_u64(expected_size, last_bytes, &expected_size)) {
                return TABLE_CORRUPT;
            }
        }
        if ((uint64_t)file_size < expected_size) {
            return TABLE_CORRUPT;
        }
    }

    /* Cap: file cannot imply more rows than count without being corrupt later. */
    {
        uint64_t data_bytes = (uint64_t)file_size - (uint64_t)STORAGE_HEADER_SIZE;
        uint64_t max_pages = data_bytes / (uint64_t)STORAGE_PAGE_SIZE;
        uint64_t partial = data_bytes % (uint64_t)STORAGE_PAGE_SIZE;
        uint64_t max_rows = max_pages * rpp + (partial / (uint64_t)STORAGE_ROW_SIZE);
        /* Only complete slots in partial page count. */
        if (partial / (uint64_t)STORAGE_ROW_SIZE > rpp) {
            max_rows = max_pages * rpp + rpp;
        }
        if (count > max_rows) {
            return TABLE_CORRUPT;
        }
    }

    *row_count = count;
    return TABLE_OK;
}

TableCreateStatus storage_create_table(const char *table_name) {
    char path[96];
    FILE *f;

    if (!valid_table_name(table_name)) {
        return TABLE_CREATE_INVALID_NAME;
    }

    storage_table_path(table_name, path, sizeof(path));

    f = fopen(path, "rb");
    if (f != NULL) {
        fclose(f);
        return TABLE_CREATE_ALREADY_EXISTS;
    }

    f = fopen(path, "wb");
    if (f == NULL) {
        return TABLE_CREATE_IO_ERROR;
    }

    if (write_header(f, 0) != TABLE_OK) {
        fclose(f);
        remove(path);
        return TABLE_CREATE_IO_ERROR;
    }

    if (fclose(f) != 0) {
        remove(path);
        return TABLE_CREATE_IO_ERROR;
    }
    return TABLE_CREATE_OK;
}

TableStatus storage_open_table(const char *table_name, Table **out) {
    Table *table;
    char path[96];
    TableStatus st;

    if (out == NULL) {
        return TABLE_IO_ERROR;
    }
    *out = NULL;

    if (!valid_table_name(table_name)) {
        return TABLE_NOT_FOUND;
    }

    table = (Table *)calloc(1, sizeof(Table));
    if (table == NULL) {
        return TABLE_ALLOC_ERROR;
    }

    strncpy(table->name, table_name, sizeof(table->name) - 1);
    table->name[sizeof(table->name) - 1] = '\0';
    storage_table_path(table_name, path, sizeof(path));

    table->file = fopen(path, "rb+");
    if (table->file == NULL) {
        free(table);
        if (errno == ENOENT) {
            return TABLE_NOT_FOUND;
        }
        return TABLE_IO_ERROR;
    }

    st = read_and_validate_header(table->file, &table->row_count);
    if (st != TABLE_OK) {
        fclose(table->file);
        free(table);
        return st;
    }

    table->header_dirty = 0;
    *out = table;
    return TABLE_OK;
}

void storage_close_table(Table *table) {
    if (table == NULL) {
        return;
    }
    if (table->file != NULL) {
        if (table->header_dirty) {
            (void)write_header(table->file, table->row_count);
        }
        fclose(table->file);
    }
    free(table);
}

TableStatus storage_file_size(Table *table, uint64_t *out_size) {
    long sz;
    if (table == NULL || table->file == NULL || out_size == NULL) {
        return TABLE_IO_ERROR;
    }
    if (fseek(table->file, 0, SEEK_END) != 0) {
        return TABLE_IO_ERROR;
    }
    sz = ftell(table->file);
    if (sz < 0) {
        return TABLE_IO_ERROR;
    }
    *out_size = (uint64_t)sz;
    return TABLE_OK;
}

TableStatus storage_insert_row(Table *table, const Row *row, uint64_t *out_offset) {
    uint64_t offset;
    unsigned char slot[STORAGE_ROW_SIZE];
    uint64_t new_count;

    if (table == NULL || table->file == NULL || row == NULL) {
        return TABLE_IO_ERROR;
    }

    if (!storage_offset_for_row(table->row_count, &offset)) {
        return TABLE_IO_ERROR;
    }

    storage_serialize_row(row, slot);

    if (fseek(table->file, (long)offset, SEEK_SET) != 0) {
        return TABLE_IO_ERROR;
    }
    if (fwrite(slot, 1, STORAGE_ROW_SIZE, table->file) != STORAGE_ROW_SIZE) {
        return TABLE_IO_ERROR;
    }

    if (!util_add_u64(table->row_count, 1, &new_count)) {
        return TABLE_IO_ERROR;
    }
    table->row_count = new_count;
    table->header_dirty = 1;

    if (write_header(table->file, table->row_count) != TABLE_OK) {
        return TABLE_IO_ERROR;
    }
    table->header_dirty = 0;

    if (fflush(table->file) != 0) {
        return TABLE_IO_ERROR;
    }

    if (out_offset != NULL) {
        *out_offset = offset;
    }
    return TABLE_OK;
}

TableStatus storage_read_row_at_offset(Table *table, uint64_t offset, Row *row) {
    unsigned char buf[STORAGE_ROW_SIZE];
    uint64_t file_size;

    if (table == NULL || table->file == NULL || row == NULL) {
        return TABLE_IO_ERROR;
    }

    if (storage_file_size(table, &file_size) != TABLE_OK) {
        return TABLE_IO_ERROR;
    }

    if (!storage_is_valid_row_offset(offset, file_size, table->row_count)) {
        return TABLE_INVALID_OFFSET;
    }

    if (fseek(table->file, (long)offset, SEEK_SET) != 0) {
        return TABLE_IO_ERROR;
    }
    if (fread(buf, 1, STORAGE_ROW_SIZE, table->file) != STORAGE_ROW_SIZE) {
        return TABLE_IO_ERROR;
    }

    storage_deserialize_row(buf, row);
    return TABLE_OK;
}

TableStatus storage_scan_begin(Table *table, RowScanner *scan) {
    if (table == NULL || table->file == NULL || scan == NULL) {
        return TABLE_IO_ERROR;
    }
    scan->table = table;
    scan->next_row = 0;
    scan->row_count = table->row_count;
    return TABLE_OK;
}

ScanStatus storage_scan_next(RowScanner *scan, Row *row, uint64_t *out_offset) {
    uint64_t offset;
    TableStatus st;

    if (scan == NULL || scan->table == NULL || row == NULL) {
        return SCAN_IO_ERROR;
    }
    if (scan->next_row >= scan->row_count) {
        return SCAN_END;
    }

    if (!storage_offset_for_row(scan->next_row, &offset)) {
        return SCAN_CORRUPT;
    }

    st = storage_read_row_at_offset(scan->table, offset, row);
    if (st == TABLE_INVALID_OFFSET || st == TABLE_CORRUPT) {
        return SCAN_CORRUPT;
    }
    if (st != TABLE_OK) {
        return SCAN_IO_ERROR;
    }

    if (out_offset != NULL) {
        *out_offset = offset;
    }
    scan->next_row++;
    return SCAN_OK;
}

ScanStatus storage_find_id(Table *table, int32_t id, Row *row, uint64_t *out_offset) {
    RowScanner scan;
    Row cur;
    uint64_t off;
    ScanStatus st;

    if (storage_scan_begin(table, &scan) != TABLE_OK) {
        return SCAN_IO_ERROR;
    }

    while ((st = storage_scan_next(&scan, &cur, &off)) == SCAN_OK) {
        if (cur.id == id) {
            if (row != NULL) {
                *row = cur;
            }
            if (out_offset != NULL) {
                *out_offset = off;
            }
            return SCAN_OK;
        }
    }
    return st;
}

TableStatus storage_select_all_rows(Table *table, Row **rows, uint64_t *count) {
    RowScanner scan;
    Row *arr;
    uint64_t n;
    uint64_t i;
    size_t bytes;
    ScanStatus st;

    if (table == NULL || rows == NULL || count == NULL) {
        return TABLE_IO_ERROR;
    }

    *rows = NULL;
    *count = 0;
    n = table->row_count;

    if (n == 0) {
        return TABLE_OK;
    }

    if (!util_mul_size((size_t)n, sizeof(Row), &bytes)) {
        return TABLE_ALLOC_ERROR;
    }

    arr = (Row *)malloc(bytes);
    if (arr == NULL) {
        return TABLE_ALLOC_ERROR;
    }

    if (storage_scan_begin(table, &scan) != TABLE_OK) {
        free(arr);
        return TABLE_IO_ERROR;
    }

    i = 0;
    while (i < n) {
        st = storage_scan_next(&scan, &arr[i], NULL);
        if (st == SCAN_END) {
            break;
        }
        if (st != SCAN_OK) {
            free(arr);
            return (st == SCAN_CORRUPT) ? TABLE_CORRUPT : TABLE_IO_ERROR;
        }
        i++;
    }

    *rows = arr;
    *count = i;
    return TABLE_OK;
}

/* Compatibility wrappers */
TableCreateStatus create_table(const char *table_name) {
    return storage_create_table(table_name);
}

Table *open_table(const char *table_name) {
    Table *t = NULL;
    if (storage_open_table(table_name, &t) != TABLE_OK) {
        return NULL;
    }
    return t;
}

void close_table(Table *table) {
    storage_close_table(table);
}
