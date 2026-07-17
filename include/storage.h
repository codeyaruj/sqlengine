#ifndef STORAGE_H
#define STORAGE_H

#include "status.h"
#include "sql_limits.h"

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>

#define STORAGE_PAGE_SIZE      4096u
#define STORAGE_NAME_SIZE      SQL_STORED_NAME_CAPACITY
#define STORAGE_TABLE_MAGIC    0x544C5153u /* "SQLT" little-endian */
#define STORAGE_TABLE_VERSION  1u
#define STORAGE_HEADER_SIZE    64u

/* On-disk row: int32 id + NAME_SIZE bytes name (no padding). */
#define STORAGE_ROW_SIZE       (4u + STORAGE_NAME_SIZE)

typedef struct {
    int32_t id;
    char name[STORAGE_NAME_SIZE];
} Row;

typedef struct {
    char name[SQL_TABLE_NAME_CAPACITY];
    FILE *file;
    uint64_t row_count;
    int writable;
} Table;

typedef enum {
    STORAGE_FAULT_NONE = 0,
    STORAGE_FAULT_BEFORE_ROW_WRITE,
    STORAGE_FAULT_DURING_ROW_WRITE,
    STORAGE_FAULT_AFTER_ROW_WRITE_BEFORE_SYNC,
    STORAGE_FAULT_BEFORE_METADATA_UPDATE,
    STORAGE_FAULT_DURING_METADATA_UPDATE
} StorageFaultPoint;

/* Layout helpers (single authoritative implementation). */
size_t storage_row_size(void);
size_t storage_page_size(void);
size_t storage_rows_per_page(void);
size_t storage_header_size(void);

bool storage_offset_for_row(uint64_t row_number, uint64_t *offset);
bool storage_is_valid_row_offset(uint64_t offset, uint64_t file_size, uint64_t row_count);
bool storage_row_number_for_offset(uint64_t offset, uint64_t *row_number);

void storage_serialize_row(const Row *src, void *dest);
void storage_deserialize_row(const void *src, Row *dest);

TableCreateStatus storage_create_table(const char *table_name);
TableStatus storage_open_table_readonly(const char *table_name, Table **out);
TableStatus storage_open_table(const char *table_name, Table **out);
void storage_close_table(Table *table);
bool storage_valid_table_name(const char *name);

/* Insert does not check uniqueness; caller must enforce primary-key policy. */
TableStatus storage_insert_row(Table *table, const Row *row, uint64_t *out_offset);

/* Page-aware full scan into a newly allocated array (caller frees). */
TableStatus storage_select_all_rows(Table *table, Row **rows, uint64_t *count);

/* Read one row at a validated file offset. */
TableStatus storage_read_row_at_offset(Table *table, uint64_t offset, Row *row);

/*
 * Page-aware row iterator. Use for SELECT, filters, index rebuild, duplicate checks.
 * Call storage_scan_begin, then storage_scan_next until SCAN_END or error.
 */
typedef struct {
    Table *table;
    uint64_t next_row;
    uint64_t row_count;
} RowScanner;

TableStatus storage_scan_begin(Table *table, RowScanner *scan);
ScanStatus storage_scan_next(RowScanner *scan, Row *row, uint64_t *out_offset);

/* Find first row with given id via page-aware scan. SCAN_END if not found. */
ScanStatus storage_find_id(Table *table, int32_t id, Row *row, uint64_t *out_offset);

/* Path helpers for tests and index layer. */
bool storage_table_path(const char *table_name, char *buf, size_t buflen);
bool storage_index_path(const char *table_name, char *buf, size_t buflen);
bool storage_journal_path(const char *table_name, char *buf, size_t buflen);

/* Deterministic fault injection used by crash-consistency regression tests. */
void storage_set_fault_point(StorageFaultPoint point);

/* File size of open table (header + data). */
TableStatus storage_file_size(Table *table, uint64_t *out_size);

/* Compatibility aliases used by older call sites. */
#define PAGE_SIZE STORAGE_PAGE_SIZE
#define NAME_SIZE STORAGE_NAME_SIZE

TableCreateStatus create_table(const char *table_name);
Table *open_table(const char *table_name);
void close_table(Table *table);

#endif
