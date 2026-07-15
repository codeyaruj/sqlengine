#ifndef INDEX_H
#define INDEX_H

#include "status.h"
#include "storage.h"
#include "sql_limits.h"

#include <stdint.h>
#include <stdio.h>

#define INDEX_INITIAL_BUCKETS 16u
#define INDEX_MAX_LOAD_NUM    3u
#define INDEX_MAX_LOAD_DEN    4u
#define INDEX_MAGIC           0x494C5153u /* "SQLI" little-endian */
#define INDEX_VERSION         1u
#define INDEX_HEADER_SIZE     32u
#define INDEX_ENTRY_ONDISK    12u /* int32 key + int64 offset */

typedef struct {
    int32_t key;
    int64_t file_offset;
} IndexEntry;

typedef struct IndexNode {
    IndexEntry entry;
    struct IndexNode *next;
} IndexNode;

typedef struct {
    char table_name[SQL_TABLE_NAME_CAPACITY];
    IndexNode **buckets;
    size_t bucket_count;
    size_t entry_count;
} Index;

typedef enum {
    INDEX_FAULT_NONE = 0,
    INDEX_FAULT_DURING_TEMP_WRITE,
    INDEX_FAULT_BEFORE_RENAME,
    INDEX_FAULT_AFTER_RENAME_BEFORE_DIR_SYNC
} IndexFaultPoint;

unsigned int index_hash(int32_t key);

Index *index_create(const char *table_name);
void index_free(Index *index);
size_t index_bucket_count(const Index *index);
void index_set_fault_point(IndexFaultPoint point);

/* Insert or replace. Returns INDEX_DUPLICATE_KEY if key exists and replace==0. */
IndexStatus index_insert(Index *index, int32_t key, int64_t file_offset, int replace);

/* Lookup key. INDEX_NOT_FOUND if absent. */
IndexStatus index_lookup(const Index *index, int32_t key, int64_t *out_offset);

/*
 * Load index from disk. On missing/corrupt/incompatible: return status and *out=NULL.
 * Does not rebuild automatically; callers should use index_load_or_rebuild.
 */
IndexStatus index_load(const char *table_name, Index **out);

/* Atomic persist: write temp file, flush, rename. */
IndexStatus index_persist(const Index *index);

/* Rebuild from table using page-aware scan, then persist. */
IndexStatus index_rebuild_from_table(const char *table_path, const char *index_path);

/* Convenience: rebuild using table name (paths derived). */
IndexStatus index_rebuild(const char *table_name);

/*
 * Load index; if missing/corrupt, rebuild from table. Always prefers table as source of truth.
 * *out is set on success (possibly rebuilt).
 */
IndexStatus index_load_or_rebuild(const char *table_name, Index **out);

/*
 * Validate a lookup result against the table: offset bounds, row boundary, key match.
 * On failure returns appropriate IndexStatus; caller should fall back to scan.
 */
IndexStatus index_validate_lookup(Table *table, int32_t key, int64_t offset, Row *out_row);

#endif
