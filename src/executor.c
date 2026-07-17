#include "executor.h"
#include "column.h"
#include "index.h"
#include "semantic.h"
#include "storage.h"
#include "util.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void print_header(const SelectQuery *query) {
    int i;

    if (query->select_all) {
        printf("%-10s %s\n", "id", "name");
        printf("%-10s %s\n", "----------", "------");
        return;
    }

    for (i = 0; i < query->column_count; i++) {
        if (i > 0) {
            printf(" ");
        }
        printf("%s", query->columns[i]);
    }
    printf("\n");
}

static void print_row(const Row *row, const SelectQuery *query) {
    int i;

    if (query->select_all) {
        printf("%-10d ", (int)row->id);
        (void)util_print_escaped_field(stdout, (const unsigned char *)row->name,
                                       STORAGE_NAME_SIZE);
        printf("\n");
        return;
    }

    for (i = 0; i < query->column_count; i++) {
        ColumnType column;

        if (i > 0) {
            printf(" ");
        }
        column = column_from_name(query->columns[i]);
        if (column == COLUMN_ID) {
            printf("%d", (int)row->id);
        } else if (column == COLUMN_NAME) {
            (void)util_print_escaped_field(stdout, (const unsigned char *)row->name,
                                           STORAGE_NAME_SIZE);
        }
    }
    printf("\n");
}

static int row_matches_where(const Row *row, const SelectQuery *query) {
    ColumnType column;
    int32_t wanted_id;

    if (!query->has_where) {
        return 1;
    }

    column = column_from_name(query->where_column);
    if (column == COLUMN_ID) {
        if (!util_parse_int32(query->where_value, &wanted_id)) {
            return 0;
        }
        return row->id == wanted_id;
    }
    if (column == COLUMN_NAME) {
        return strcmp(row->name, query->where_value) == 0;
    }
    return 0;
}

typedef enum {
    SELECT_INDEX_ERROR = -1,
    SELECT_INDEX_NOT_USED = 0,
    SELECT_INDEX_ROW_PRINTED = 1
} SelectIndexResult;

static SelectIndexResult select_with_index(Table *table,
                                           const SelectQuery *query) {
    int32_t search_id;
    Index *index = NULL;
    IndexStatus index_status;
    int64_t offset;
    Row row;

    if (!query->has_where ||
        column_from_name(query->where_column) != COLUMN_ID) {
        return SELECT_INDEX_NOT_USED;
    }
    if (!util_parse_int32(query->where_value, &search_id)) {
        return SELECT_INDEX_NOT_USED;
    }

    index_status = index_load_or_rebuild(query->table_name, &index);
    if (index_status == INDEX_DUPLICATE_PRIMARY_KEY) {
        printf("Error: Table '%s' contains duplicate primary keys.\n",
               query->table_name);
        index_free(index);
        return SELECT_INDEX_ERROR;
    }
    if (index_status != INDEX_OK || index == NULL) {
        index_free(index);
        return SELECT_INDEX_NOT_USED;
    }

    index_status = index_lookup(index, search_id, &offset);
    if (index_status == INDEX_OK) {
        index_status =
            index_validate_lookup(table, search_id, offset, &row);
    }
    index_free(index);

    if (index_status != INDEX_OK) {
        return SELECT_INDEX_NOT_USED;
    }

    print_header(query);
    print_row(&row, query);
    return SELECT_INDEX_ROW_PRINTED;
}

static ExecStatus select_with_scan(Table *table, const SelectQuery *query,
                                   int *rows_printed) {
    RowScanner scan;
    Row row;
    ScanStatus scan_status;

    if (storage_scan_begin(table, &scan) != TABLE_OK) {
        printf("Error: Could not scan table.\n");
        return EXEC_IO_ERROR;
    }

    scan_status = storage_scan_next(&scan, &row, NULL);
    while (scan_status == SCAN_OK) {
        if (row_matches_where(&row, query)) {
            if (*rows_printed == 0) {
                print_header(query);
            }
            print_row(&row, query);
            (*rows_printed)++;
        }
        scan_status = storage_scan_next(&scan, &row, NULL);
    }

    if (scan_status == SCAN_CORRUPT) {
        printf("Error: Table scan failed (corrupt data).\n");
        return EXEC_TABLE_CORRUPT;
    }
    if (scan_status != SCAN_END) {
        printf("Error: Table scan failed (I/O error).\n");
        return EXEC_IO_ERROR;
    }
    return EXEC_OK;
}

static ExecStatus execute_select(AST *ast) {
    const SelectQuery *query = &ast->query.select;
    Table *table = NULL;
    TableStatus table_status;
    ExecStatus result;
    int rows_printed = 0;
    SelectIndexResult index_result;

    table_status = storage_open_table_readonly(query->table_name, &table);
    if (table_status == TABLE_NOT_FOUND) {
        printf("Error: Table '%s' does not exist.\n", query->table_name);
        return EXEC_TABLE_NOT_FOUND;
    }
    if (table_status == TABLE_INCOMPATIBLE) {
        printf("Error: Table '%s' has an incompatible or legacy file format.\n",
               query->table_name);
        return EXEC_TABLE_CORRUPT;
    }
    if (table_status == TABLE_CORRUPT) {
        printf("Error: Table '%s' is corrupt.\n", query->table_name);
        return EXEC_TABLE_CORRUPT;
    }
    if (table_status != TABLE_OK) {
        printf("Error: Could not open table '%s'.\n", query->table_name);
        return EXEC_IO_ERROR;
    }

    index_result = select_with_index(table, query);
    if (index_result == SELECT_INDEX_ERROR) {
        storage_close_table(table);
        return EXEC_TABLE_CORRUPT;
    }
    if (index_result == SELECT_INDEX_ROW_PRINTED) {
        rows_printed = 1;
    } else {
        result = select_with_scan(table, query, &rows_printed);
        if (result != EXEC_OK) {
            storage_close_table(table);
            return result;
        }
    }

    if (rows_printed == 0) {
        printf("(empty result)\n");
    } else {
        printf("\n(%d row(s) returned)\n", rows_printed);
    }

    storage_close_table(table);
    return EXEC_OK;
}

static ExecStatus execute_insert(AST *ast) {
    const InsertQuery *query = &ast->query.insert;
    Table *table = NULL;
    TableStatus table_status;
    Index *index = NULL;
    IndexStatus index_status;
    Row row;
    uint64_t offset = 0;
    int64_t existing_offset;
    int duplicate_found = 0;
    ExecStatus result;

    table_status = storage_open_table(query->table_name, &table);
    if (table_status == TABLE_NOT_FOUND) {
        printf("Error: Table '%s' does not exist.\n", query->table_name);
        return EXEC_TABLE_NOT_FOUND;
    }
    if (table_status == TABLE_INCOMPATIBLE || table_status == TABLE_CORRUPT) {
        if (table_status == TABLE_INCOMPATIBLE) {
            printf("Error: Table '%s' is unreadable (incompatible format).\n",
                   query->table_name);
        } else {
            printf("Error: Table '%s' is unreadable (corrupt).\n",
                   query->table_name);
        }
        return EXEC_TABLE_CORRUPT;
    }
    if (table_status == TABLE_READ_ONLY) {
        printf("Error: Table '%s' is read-only.\n", query->table_name);
        return EXEC_READ_ONLY;
    }
    if (table_status != TABLE_OK) {
        printf("Error: Could not open table '%s'.\n", query->table_name);
        return EXEC_IO_ERROR;
    }

    /* Uniqueness: prefer valid index, else page-aware scan. */
    index_status = index_load_or_rebuild(query->table_name, &index);
    if (index_status == INDEX_DUPLICATE_PRIMARY_KEY) {
        printf("Error: Table '%s' contains duplicate primary keys.\n",
               query->table_name);
        result = EXEC_TABLE_CORRUPT;
        goto done;
    }
    if (index_status == INDEX_OK && index != NULL) {
        if (index_lookup(index, query->id, &existing_offset) == INDEX_OK) {
            Row existing;

            index_status = index_validate_lookup(
                table, query->id, existing_offset, &existing);
            if (index_status == INDEX_OK) {
                duplicate_found = 1;
            }
        }
    }

    if (!duplicate_found) {
        ScanStatus scan_status =
            storage_find_id(table, query->id, NULL, NULL);

        if (scan_status == SCAN_OK) {
            duplicate_found = 1;
        } else if (scan_status != SCAN_END) {
            printf("Error: Failed while checking for duplicate id.\n");
            result = EXEC_IO_ERROR;
            goto done;
        }
    }

    if (duplicate_found) {
        printf("Error: Duplicate id %d (id is a unique primary key).\n",
               (int)query->id);
        result = EXEC_DUPLICATE_ID;
        goto done;
    }

    memset(&row, 0, sizeof(row));
    row.id = query->id;
    if (!util_copy_checked(row.name, sizeof(row.name), query->name)) {
        result = EXEC_SEMANTIC_ERROR;
        goto done;
    }

    table_status = storage_insert_row(table, &row, &offset);
    if (table_status == TABLE_READ_ONLY) {
        printf("Error: Table '%s' is read-only.\n", query->table_name);
        result = EXEC_READ_ONLY;
        goto done;
    }
    if (table_status != TABLE_OK) {
        printf("Error: Could not insert row.\n");
        result = EXEC_IO_ERROR;
        goto done;
    }

    /* Maintain index; table row is already committed. */
    if (index == NULL) {
        index = index_create(query->table_name);
        if (index == NULL) {
            printf("Warning: Row inserted (id=%d) but index could not be allocated; "
                   "index will rebuild on next query.\n",
                   (int)row.id);
            result = EXEC_INDEX_PERSIST_FAILED;
            goto done;
        }
    }

    index_status = index_insert(index, row.id, (int64_t)offset, 0);
    if (index_status == INDEX_DUPLICATE_KEY) {
        /* Should not happen after uniqueness check; replace to stay consistent. */
        index_status = index_insert(index, row.id, (int64_t)offset, 1);
    }
    if (index_status != INDEX_OK) {
        printf("Warning: Row inserted (id=%d) but index update failed; "
               "index will rebuild on next query.\n",
               (int)row.id);
        result = EXEC_INDEX_ERROR;
        goto done;
    }

    index_status = index_persist(index);
    if (index_status != INDEX_OK) {
        printf("Warning: Row inserted (id=%d) but index persistence failed; "
               "index will rebuild on next query.\n",
               (int)row.id);
        result = EXEC_INDEX_PERSIST_FAILED;
        goto done;
    }

    printf("Row inserted successfully (id=%d).\n", (int)row.id);
    result = EXEC_OK;

done:
    index_free(index);
    storage_close_table(table);
    return result;
}

ExecStatus execute(AST *ast) {
    SemanticStatus semantic_status;

    if (ast == NULL) {
        return EXEC_UNKNOWN;
    }

    semantic_status = semantic_validate(ast);
    if (semantic_status != SEMANTIC_OK) {
        printf("Error: Semantic error: %s.\n",
               semantic_status_string(semantic_status));
        return EXEC_SEMANTIC_ERROR;
    }

    switch (ast->type) {
        case AST_SELECT:
            return execute_select(ast);
        case AST_INSERT:
            return execute_insert(ast);
        default:
            printf("Error: Unknown query type.\n");
            return EXEC_UNKNOWN;
    }
}
