#include "storage.h"
#include "util.h"

#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#define JOURNAL_MAGIC       0x4A4C5153u /* "SQLJ" little-endian */
#define JOURNAL_VERSION     1u
#define JOURNAL_SIZE        128u
#define JOURNAL_CHECKSUM_AT 108u

static StorageFaultPoint g_storage_fault = STORAGE_FAULT_NONE;

void storage_set_fault_point(StorageFaultPoint point) {
    g_storage_fault = point;
}

static int consume_storage_fault(StorageFaultPoint point) {
    if (g_storage_fault == point) {
        g_storage_fault = STORAGE_FAULT_NONE;
        errno = EIO;
        return 1;
    }
    return 0;
}

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

static bool build_path(const char *table_name, const char *suffix, char *buf, size_t buflen) {
    size_t name_length;
    size_t suffix_length;
    size_t needed;
    if (table_name == NULL || suffix == NULL || buf == NULL || buflen == 0) {
        return false;
    }
    name_length = strlen(table_name);
    suffix_length = strlen(suffix);
    if (!util_add_size(name_length, suffix_length, &needed) ||
        !util_add_size(needed, 1, &needed) || needed > buflen) {
        return false;
    }
    memcpy(buf, table_name, name_length);
    memcpy(buf + name_length, suffix, suffix_length + 1);
    return true;
}

bool storage_table_path(const char *table_name, char *buf, size_t buflen) {
    return build_path(table_name, ".tbl", buf, buflen);
}

bool storage_index_path(const char *table_name, char *buf, size_t buflen) {
    return build_path(table_name, ".idx", buf, buflen);
}

bool storage_journal_path(const char *table_name, char *buf, size_t buflen) {
    return build_path(table_name, ".tbl.journal", buf, buflen);
}

bool storage_valid_table_name(const char *name) {
    size_t i;
    if (name == NULL || name[0] == '\0') {
        return false;
    }
    for (i = 0; name[i] != '\0'; i++) {
        unsigned char c = (unsigned char)name[i];
        if (!( (c >= 'a' && c <= 'z') ||
               (c >= 'A' && c <= 'Z') ||
               (c >= '0' && c <= '9') ||
               c == '_' )) {
            return false;
        }
        if (i >= SQL_MAX_TABLE_NAME_LENGTH) {
            return false;
        }
    }
    return true;
}

static TableStatus write_header(FILE *f, uint64_t row_count) {
    unsigned char padding[STORAGE_HEADER_SIZE - 32u];

    if (fseek(f, 0, SEEK_SET) != 0) {
        return TABLE_IO_ERROR;
    }

    if (!util_write_u32_le(f, STORAGE_TABLE_MAGIC) ||
        !util_write_u32_le(f, STORAGE_TABLE_VERSION) ||
        !util_write_u32_le(f, STORAGE_HEADER_SIZE) ||
        !util_write_u32_le(f, STORAGE_PAGE_SIZE) ||
        !util_write_u32_le(f, STORAGE_ROW_SIZE) ||
        !util_write_u32_le(f, 0u) ||
        !util_write_u64_le(f, row_count)) {
        return TABLE_IO_ERROR;
    }

    memset(padding, 0, sizeof(padding));
    if (fwrite(padding, 1, sizeof(padding), f) != sizeof(padding)) {
        return TABLE_IO_ERROR;
    }

    if (fflush(f) != 0) {
        return TABLE_IO_ERROR;
    }
    return TABLE_OK;
}

static void put_u32_le(unsigned char *dest, uint32_t value) {
    dest[0] = (unsigned char)(value & 0xFFu);
    dest[1] = (unsigned char)((value >> 8) & 0xFFu);
    dest[2] = (unsigned char)((value >> 16) & 0xFFu);
    dest[3] = (unsigned char)((value >> 24) & 0xFFu);
}

static void put_u64_le(unsigned char *dest, uint64_t value) {
    int i;
    for (i = 0; i < 8; i++) {
        dest[i] = (unsigned char)((value >> (8 * i)) & 0xFFu);
    }
}

static uint32_t get_u32_le(const unsigned char *src) {
    return (uint32_t)src[0]
         | ((uint32_t)src[1] << 8)
         | ((uint32_t)src[2] << 16)
         | ((uint32_t)src[3] << 24);
}

static uint64_t get_u64_le(const unsigned char *src) {
    uint64_t value = 0;
    int i;
    for (i = 0; i < 8; i++) {
        value |= (uint64_t)src[i] << (8 * i);
    }
    return value;
}

static uint32_t journal_checksum(const unsigned char *bytes) {
    uint32_t hash = 2166136261u;
    size_t i;

    for (i = 0; i < JOURNAL_SIZE; i++) {
        unsigned char byte = bytes[i];

        if (i >= JOURNAL_CHECKSUM_AT && i < JOURNAL_CHECKSUM_AT + 4u) {
            byte = 0;
        }
        hash ^= (uint32_t)byte;
        hash *= 16777619u;
    }
    return hash;
}

static bool storage_logical_size(uint64_t row_count, uint64_t *size) {
    uint64_t last_offset;
    if (size == NULL) {
        return false;
    }
    if (row_count == 0) {
        *size = (uint64_t)STORAGE_HEADER_SIZE;
        return true;
    }
    if (!storage_offset_for_row(row_count - 1, &last_offset)) {
        return false;
    }
    return util_add_u64(last_offset, (uint64_t)STORAGE_ROW_SIZE, size);
}

static TableStatus persist_insert_journal(const Table *table, const Row *row,
                                          uint64_t offset, uint64_t new_count) {
    unsigned char bytes[JOURNAL_SIZE];
    unsigned char row_bytes[STORAGE_ROW_SIZE];
    char journal_path[128];
    char temp_path[160];
    int fd = -1;
    FILE *f = NULL;
    int saved_errno;

    if (!storage_journal_path(table->name, journal_path, sizeof(journal_path))) {
        return TABLE_DURABILITY_ERROR;
    }
    memset(bytes, 0, sizeof(bytes));
    put_u32_le(bytes, JOURNAL_MAGIC);
    put_u32_le(bytes + 4, JOURNAL_VERSION);
    put_u32_le(bytes + 8, JOURNAL_SIZE);
    put_u64_le(bytes + 16, table->row_count);
    put_u64_le(bytes + 24, new_count);
    put_u64_le(bytes + 32, offset);
    memcpy(bytes + 40, table->name, strlen(table->name));
    storage_serialize_row(row, row_bytes);
    memcpy(bytes + 72, row_bytes, sizeof(row_bytes));
    put_u32_le(bytes + JOURNAL_CHECKSUM_AT, journal_checksum(bytes));

    if (!util_secure_temp_file(journal_path, temp_path, sizeof(temp_path), &fd)) {
        if (errno == EACCES || errno == EROFS) {
            return TABLE_READ_ONLY;
        }
        return TABLE_DURABILITY_ERROR;
    }
    f = fdopen(fd, "wb");
    if (f == NULL) {
        saved_errno = errno;
        close(fd);
        unlink(temp_path);
        errno = saved_errno;
        return TABLE_DURABILITY_ERROR;
    }
    if (fwrite(bytes, 1, sizeof(bytes), f) != sizeof(bytes) || !util_flush_and_sync(f)) {
        saved_errno = errno;
        fclose(f);
        unlink(temp_path);
        errno = saved_errno;
        return TABLE_DURABILITY_ERROR;
    }
    if (fclose(f) != 0) {
        saved_errno = errno;
        unlink(temp_path);
        errno = saved_errno;
        return TABLE_DURABILITY_ERROR;
    }
    if (rename(temp_path, journal_path) != 0) {
        saved_errno = errno;
        unlink(temp_path);
        errno = saved_errno;
        if (saved_errno == EACCES || saved_errno == EROFS) {
            return TABLE_READ_ONLY;
        }
        return TABLE_DURABILITY_ERROR;
    }
    if (!util_sync_parent_directory(journal_path)) {
        return TABLE_DURABILITY_ERROR;
    }
    return TABLE_OK;
}

static TableStatus recover_insert_journal(FILE *table_file, const char *table_name) {
    unsigned char bytes[JOURNAL_SIZE];
    char journal_path[128];
    char stored_name[SQL_TABLE_NAME_CAPACITY];
    uint32_t checksum;
    uint64_t previous_count;
    uint64_t intended_count;
    uint64_t expected_count;
    uint64_t offset;
    uint64_t expected_offset;
    uint64_t old_size;
    off_t truncate_size;
    int fd;
    ssize_t amount;
    unsigned char extra;

    if (!storage_journal_path(table_name, journal_path, sizeof(journal_path))) {
        return TABLE_DURABILITY_ERROR;
    }
    {
        int flags = O_RDONLY;
#ifdef O_NOFOLLOW
        flags |= O_NOFOLLOW;
#endif
        fd = open(journal_path, flags);
    }
    if (fd < 0) {
        if (errno == ENOENT) {
            return TABLE_OK;
        }
        return TABLE_DURABILITY_ERROR;
    }
    amount = read(fd, bytes, sizeof(bytes));
    if (amount != (ssize_t)sizeof(bytes) || read(fd, &extra, 1) != 0) {
        close(fd);
        return TABLE_CORRUPT;
    }
    if (close(fd) != 0) {
        return TABLE_DURABILITY_ERROR;
    }

    checksum = get_u32_le(bytes + JOURNAL_CHECKSUM_AT);
    if (get_u32_le(bytes) != JOURNAL_MAGIC ||
        get_u32_le(bytes + 4) != JOURNAL_VERSION ||
        get_u32_le(bytes + 8) != JOURNAL_SIZE ||
        get_u32_le(bytes + 12) != 0u ||
        checksum != journal_checksum(bytes)) {
        return TABLE_CORRUPT;
    }
    memcpy(stored_name, bytes + 40, sizeof(stored_name));
    stored_name[sizeof(stored_name) - 1] = '\0';
    previous_count = get_u64_le(bytes + 16);
    intended_count = get_u64_le(bytes + 24);
    offset = get_u64_le(bytes + 32);
    if (strcmp(stored_name, table_name) != 0 ||
        !util_add_u64(previous_count, 1, &expected_count) ||
        intended_count != expected_count ||
        !storage_offset_for_row(previous_count, &expected_offset) || expected_offset != offset ||
        !storage_logical_size(previous_count, &old_size)) {
        return TABLE_CORRUPT;
    }
    truncate_size = (off_t)old_size;
    if (truncate_size < 0 || (uint64_t)truncate_size != old_size) {
        return TABLE_CORRUPT;
    }

    if (write_header(table_file, previous_count) != TABLE_OK || !util_flush_and_sync(table_file)) {
        return TABLE_DURABILITY_ERROR;
    }
    if (ftruncate(fileno(table_file), truncate_size) != 0 || !util_flush_and_sync(table_file)) {
        return TABLE_DURABILITY_ERROR;
    }
    if (unlink(journal_path) != 0 || !util_sync_parent_directory(journal_path)) {
        return TABLE_DURABILITY_ERROR;
    }
    return TABLE_OK;
}

static TableStatus read_and_validate_header(FILE *f, uint64_t *row_count) {
    uint32_t magic, version, header_size, page_size, row_size, reserved0;
    uint64_t count;
    long file_size;
    uint64_t minimum_size;

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
    if (reserved0 != 0u) {
        return TABLE_CORRUPT;
    }

    if (!storage_logical_size(count, &minimum_size)) {
        return TABLE_CORRUPT;
    }
    if ((uint64_t)file_size < minimum_size) {
        return TABLE_CORRUPT;
    }

    *row_count = count;
    return TABLE_OK;
}

TableCreateStatus storage_create_table(const char *table_name) {
    char path[96];
    char journal_path[128];
    int fd;
    int flags = O_WRONLY | O_CREAT | O_EXCL;
    FILE *f;
    int saved_errno;

    if (table_name != NULL && strlen(table_name) > SQL_MAX_TABLE_NAME_LENGTH) {
        return TABLE_CREATE_NAME_TOO_LONG;
    }
    if (!storage_valid_table_name(table_name)) {
        return TABLE_CREATE_INVALID_NAME;
    }
    if (!storage_table_path(table_name, path, sizeof(path)) ||
        !storage_journal_path(table_name, journal_path, sizeof(journal_path))) {
        return TABLE_CREATE_INVALID_NAME;
    }
#ifdef O_NOFOLLOW
    flags |= O_NOFOLLOW;
#endif
    fd = open(path, flags, S_IRUSR | S_IWUSR);
    if (fd < 0) {
        if (errno == EEXIST) {
            return TABLE_CREATE_ALREADY_EXISTS;
        }
        return TABLE_CREATE_IO_ERROR;
    }
    if (fchmod(fd, S_IRUSR | S_IWUSR) != 0) {
        saved_errno = errno;
        close(fd);
        unlink(path);
        errno = saved_errno;
        return TABLE_CREATE_IO_ERROR;
    }
    f = fdopen(fd, "wb");
    if (f == NULL) {
        saved_errno = errno;
        close(fd);
        unlink(path);
        errno = saved_errno;
        return TABLE_CREATE_IO_ERROR;
    }

    if (write_header(f, 0) != TABLE_OK || !util_flush_and_sync(f)) {
        saved_errno = errno;
        fclose(f);
        unlink(path);
        errno = saved_errno;
        return TABLE_CREATE_IO_ERROR;
    }

    if (fclose(f) != 0) {
        saved_errno = errno;
        unlink(path);
        errno = saved_errno;
        return TABLE_CREATE_IO_ERROR;
    }
    if (access(journal_path, F_OK) == 0) {
        unlink(path);
        return TABLE_CREATE_IO_ERROR;
    }
    if (!util_sync_parent_directory(path)) {
        saved_errno = errno;
        unlink(path);
        errno = saved_errno;
        return TABLE_CREATE_IO_ERROR;
    }
    return TABLE_CREATE_OK;
}

static TableStatus storage_open_table_mode(const char *table_name, int writable, Table **out) {
    Table *table;
    char path[96];
    char journal_path[128];
    TableStatus st;
    int flags;
    int fd;

    if (out == NULL) {
        return TABLE_IO_ERROR;
    }
    *out = NULL;

    if (!storage_valid_table_name(table_name)) {
        return TABLE_NOT_FOUND;
    }

    table = (Table *)calloc(1, sizeof(Table));
    if (table == NULL) {
        return TABLE_ALLOC_ERROR;
    }

    if (!util_copy_checked(table->name, sizeof(table->name), table_name) ||
        !storage_table_path(table_name, path, sizeof(path)) ||
        !storage_journal_path(table_name, journal_path, sizeof(journal_path))) {
        free(table);
        return TABLE_IO_ERROR;
    }
    if (writable) {
        flags = O_RDWR;
    } else {
        flags = O_RDONLY;
    }
#ifdef O_NOFOLLOW
    flags |= O_NOFOLLOW;
#endif
    fd = open(path, flags);
    if (fd < 0) {
        int open_errno = errno;
        free(table);
        if (open_errno == ENOENT) {
            return TABLE_NOT_FOUND;
        }
        if (writable && (open_errno == EACCES || open_errno == EROFS)) {
            return TABLE_READ_ONLY;
        }
        return TABLE_IO_ERROR;
    }
    if (writable) {
        table->file = fdopen(fd, "rb+");
    } else {
        table->file = fdopen(fd, "rb");
    }
    if (table->file == NULL) {
        int open_errno = errno;
        close(fd);
        free(table);
        errno = open_errno;
        return TABLE_IO_ERROR;
    }

    if (writable) {
        st = recover_insert_journal(table->file, table_name);
        if (st != TABLE_OK) {
            fclose(table->file);
            free(table);
            return st;
        }
    } else if (access(journal_path, F_OK) == 0) {
        fclose(table->file);
        free(table);
        return TABLE_RECOVERY_REQUIRED;
    }

    st = read_and_validate_header(table->file, &table->row_count);
    if (st != TABLE_OK) {
        fclose(table->file);
        free(table);
        return st;
    }

    table->writable = writable;
    *out = table;
    return TABLE_OK;
}

TableStatus storage_open_table_readonly(const char *table_name, Table **out) {
    return storage_open_table_mode(table_name, 0, out);
}

TableStatus storage_open_table(const char *table_name, Table **out) {
    return storage_open_table_mode(table_name, 1, out);
}

void storage_close_table(Table *table) {
    if (table == NULL) {
        return;
    }
    if (table->file != NULL) {
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
    char journal_path[128];
    TableStatus journal_status;

    if (table == NULL || table->file == NULL || row == NULL) {
        return TABLE_IO_ERROR;
    }
    if (!table->writable) {
        return TABLE_READ_ONLY;
    }
    if (!storage_offset_for_row(table->row_count, &offset) || offset > (uint64_t)LONG_MAX ||
        !util_add_u64(table->row_count, 1, &new_count) ||
        !storage_journal_path(table->name, journal_path, sizeof(journal_path))) {
        return TABLE_IO_ERROR;
    }

    storage_serialize_row(row, slot);

    journal_status = persist_insert_journal(table, row, offset, new_count);
    if (journal_status != TABLE_OK) {
        return journal_status;
    }
    if (consume_storage_fault(STORAGE_FAULT_BEFORE_ROW_WRITE)) {
        return TABLE_IO_ERROR;
    }

    if (fseek(table->file, (long)offset, SEEK_SET) != 0) {
        return TABLE_IO_ERROR;
    }
    if (consume_storage_fault(STORAGE_FAULT_DURING_ROW_WRITE)) {
        (void)fwrite(slot, 1, STORAGE_ROW_SIZE / 2u, table->file);
        (void)fflush(table->file);
        return TABLE_IO_ERROR;
    }
    if (fwrite(slot, 1, STORAGE_ROW_SIZE, table->file) != STORAGE_ROW_SIZE) {
        return TABLE_IO_ERROR;
    }
    if (consume_storage_fault(STORAGE_FAULT_AFTER_ROW_WRITE_BEFORE_SYNC)) {
        return TABLE_IO_ERROR;
    }
    if (!util_flush_and_sync(table->file)) {
        return TABLE_DURABILITY_ERROR;
    }
    if (consume_storage_fault(STORAGE_FAULT_BEFORE_METADATA_UPDATE)) {
        return TABLE_IO_ERROR;
    }
    if (consume_storage_fault(STORAGE_FAULT_DURING_METADATA_UPDATE)) {
        unsigned char damaged[2] = {0, 0};
        if (fseek(table->file, 0, SEEK_SET) == 0) {
            (void)fwrite(damaged, 1, sizeof(damaged), table->file);
            (void)fflush(table->file);
        }
        return TABLE_IO_ERROR;
    }
    if (write_header(table->file, new_count) != TABLE_OK || !util_flush_and_sync(table->file)) {
        return TABLE_DURABILITY_ERROR;
    }
    if (unlink(journal_path) != 0 || !util_sync_parent_directory(journal_path)) {
        return TABLE_DURABILITY_ERROR;
    }

    table->row_count = new_count;
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
            if (st == SCAN_CORRUPT) {
                return TABLE_CORRUPT;
            }
            return TABLE_IO_ERROR;
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
