#include "executor.h"
#include "column.h"
#include "index.h"
#include "semantic.h"
#include "storage.h"
#include "util.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void print_header(const SelectQuery *sel) {
    int i;
    if (sel->select_all) {
        printf("%-10s %s\n", "id", "name");
        printf("%-10s %s\n", "----------", "------");
        return;
    }
    for (i = 0; i < sel->column_count; i++) {
        if (i > 0) {
            printf(" ");
        }
        printf("%s", sel->columns[i]);
    }
    printf("\n");
}

static void print_row(const Row *row, const SelectQuery *sel) {
    int i;
    if (sel->select_all) {
        printf("%-10d ", (int)row->id);
        (void)util_print_escaped_field(stdout, (const unsigned char *)row->name,
                                       STORAGE_NAME_SIZE);
        printf("\n");
        return;
    }
    for (i = 0; i < sel->column_count; i++) {
        ColumnType col;
        if (i > 0) {
            printf(" ");
        }
        col = column_from_name(sel->columns[i]);
        if (col == COLUMN_ID) {
            printf("%d", (int)row->id);
        } else if (col == COLUMN_NAME) {
            (void)util_print_escaped_field(stdout, (const unsigned char *)row->name,
                                           STORAGE_NAME_SIZE);
        }
    }
    printf("\n");
}

static int row_matches_where(const Row *row, const SelectQuery *sel) {
    ColumnType col;
    int32_t want;

    if (!sel->has_where) {
        return 1;
    }

    col = column_from_name(sel->where_column);
    if (col == COLUMN_ID) {
        if (!util_parse_int32(sel->where_value, &want)) {
            return 0;
        }
        return row->id == want;
    }
    if (col == COLUMN_NAME) {
        return strcmp(row->name, sel->where_value) == 0;
    }
    return 0;
}

static ExecStatus execute_select(AST *ast) {
    SelectQuery *sel = &ast->query.select;
    Table *table = NULL;
    TableStatus tst;
    int printed = 0;
    int used_index = 0;

    tst = storage_open_table_readonly(sel->table_name, &table);
    if (tst == TABLE_NOT_FOUND) {
        printf("Error: Table '%s' does not exist.\n", sel->table_name);
        return EXEC_TABLE_NOT_FOUND;
    }
    if (tst == TABLE_INCOMPATIBLE) {
        printf("Error: Table '%s' has an incompatible or legacy file format.\n",
               sel->table_name);
        return EXEC_TABLE_CORRUPT;
    }
    if (tst == TABLE_CORRUPT) {
        printf("Error: Table '%s' is corrupt.\n", sel->table_name);
        return EXEC_TABLE_CORRUPT;
    }
    if (tst != TABLE_OK) {
        printf("Error: Could not open table '%s'.\n", sel->table_name);
        return EXEC_IO_ERROR;
    }

    /* Indexed path: WHERE id = <number> */
    if (sel->has_where && column_from_name(sel->where_column) == COLUMN_ID) {
        int32_t search_id;
        Index *index = NULL;
        IndexStatus ist;
        int64_t offset = -1;
        Row row;

        if (util_parse_int32(sel->where_value, &search_id)) {
            ist = index_load_or_rebuild(sel->table_name, &index);
            if (ist == INDEX_DUPLICATE_PRIMARY_KEY) {
                printf("Error: Table '%s' contains duplicate primary keys.\n",
                       sel->table_name);
                storage_close_table(table);
                return EXEC_TABLE_CORRUPT;
            }
            if (ist == INDEX_OK && index != NULL) {
                ist = index_lookup(index, search_id, &offset);
                if (ist == INDEX_OK) {
                    ist = index_validate_lookup(table, search_id, offset, &row);
                    if (ist == INDEX_OK) {
                        used_index = 1;
                        print_header(sel);
                        print_row(&row, sel);
                        printed = 1;
                    }
                    /* Validation failed: fall through to full scan. */
                }
                /* INDEX_NOT_FOUND: row may still exist if index is stale — scan. */
                index_free(index);
                index = NULL;
            }
            /* Index load/rebuild failed: fall through to full scan. */
        }
    }

    if (!used_index) {
        RowScanner scan;
        Row row;
        ScanStatus sst;

        if (storage_scan_begin(table, &scan) != TABLE_OK) {
            printf("Error: Could not scan table.\n");
            storage_close_table(table);
            return EXEC_IO_ERROR;
        }

        while ((sst = storage_scan_next(&scan, &row, NULL)) == SCAN_OK) {
            if (row_matches_where(&row, sel)) {
                if (printed == 0) {
                    print_header(sel);
                }
                print_row(&row, sel);
                printed++;
            }
        }
        if (sst != SCAN_END) {
            printf("Error: Table scan failed (%s).\n",
                   sst == SCAN_CORRUPT ? "corrupt data" : "I/O error");
            storage_close_table(table);
            return (sst == SCAN_CORRUPT) ? EXEC_TABLE_CORRUPT : EXEC_IO_ERROR;
        }
    }

    if (printed == 0) {
        printf("(empty result)\n");
    } else {
        printf("\n(%d row(s) returned)\n", printed);
    }

    storage_close_table(table);
    return EXEC_OK;
}

static ExecStatus execute_insert(AST *ast) {
    InsertQuery *ins = &ast->query.insert;
    Table *table = NULL;
    TableStatus tst;
    Index *index = NULL;
    IndexStatus ist;
    Row row;
    uint64_t offset = 0;
    int64_t existing_off;
    int id_exists = 0;

    tst = storage_open_table(ins->table_name, &table);
    if (tst == TABLE_NOT_FOUND) {
        printf("Error: Table '%s' does not exist.\n", ins->table_name);
        return EXEC_TABLE_NOT_FOUND;
    }
    if (tst == TABLE_INCOMPATIBLE || tst == TABLE_CORRUPT) {
        printf("Error: Table '%s' is unreadable (%s).\n",
               ins->table_name,
               tst == TABLE_INCOMPATIBLE ? "incompatible format" : "corrupt");
        return EXEC_TABLE_CORRUPT;
    }
    if (tst == TABLE_READ_ONLY) {
        printf("Error: Table '%s' is read-only.\n", ins->table_name);
        return EXEC_READ_ONLY;
    }
    if (tst != TABLE_OK) {
        printf("Error: Could not open table '%s'.\n", ins->table_name);
        return EXEC_IO_ERROR;
    }

    /* Uniqueness: prefer valid index, else page-aware scan. */
    ist = index_load_or_rebuild(ins->table_name, &index);
    if (ist == INDEX_DUPLICATE_PRIMARY_KEY) {
        printf("Error: Table '%s' contains duplicate primary keys.\n",
               ins->table_name);
        storage_close_table(table);
        return EXEC_TABLE_CORRUPT;
    }
    if (ist == INDEX_OK && index != NULL) {
        if (index_lookup(index, ins->id, &existing_off) == INDEX_OK) {
            Row existing;
            if (index_validate_lookup(table, ins->id, existing_off, &existing) == INDEX_OK) {
                id_exists = 1;
            }
        }
    }

    if (!id_exists) {
        ScanStatus sst = storage_find_id(table, ins->id, NULL, NULL);
        if (sst == SCAN_OK) {
            id_exists = 1;
        } else if (sst != SCAN_END) {
            printf("Error: Failed while checking for duplicate id.\n");
            index_free(index);
            storage_close_table(table);
            return EXEC_IO_ERROR;
        }
    }

    if (id_exists) {
        printf("Error: Duplicate id %d (id is a unique primary key).\n", (int)ins->id);
        index_free(index);
        storage_close_table(table);
        return EXEC_DUPLICATE_ID;
    }

    memset(&row, 0, sizeof(row));
    row.id = ins->id;
    if (!util_copy_checked(row.name, sizeof(row.name), ins->name)) {
        index_free(index);
        storage_close_table(table);
        return EXEC_SEMANTIC_ERROR;
    }

    tst = storage_insert_row(table, &row, &offset);
    if (tst == TABLE_READ_ONLY) {
        printf("Error: Table '%s' is read-only.\n", ins->table_name);
        index_free(index);
        storage_close_table(table);
        return EXEC_READ_ONLY;
    }
    if (tst != TABLE_OK) {
        printf("Error: Could not insert row.\n");
        index_free(index);
        storage_close_table(table);
        return EXEC_IO_ERROR;
    }

    /* Maintain index; table row is already committed. */
    if (index == NULL) {
        index = index_create(ins->table_name);
        if (index == NULL) {
            printf("Warning: Row inserted (id=%d) but index could not be allocated; "
                   "index will rebuild on next query.\n",
                   (int)row.id);
            storage_close_table(table);
            return EXEC_INDEX_PERSIST_FAILED;
        }
    }

    ist = index_insert(index, row.id, (int64_t)offset, 0);
    if (ist == INDEX_DUPLICATE_KEY) {
        /* Should not happen after uniqueness check; replace to stay consistent. */
        ist = index_insert(index, row.id, (int64_t)offset, 1);
    }
    if (ist != INDEX_OK) {
        printf("Warning: Row inserted (id=%d) but index update failed; "
               "index will rebuild on next query.\n",
               (int)row.id);
        index_free(index);
        storage_close_table(table);
        return EXEC_INDEX_ERROR;
    }

    ist = index_persist(index);
    if (ist != INDEX_OK) {
        printf("Warning: Row inserted (id=%d) but index persistence failed; "
               "index will rebuild on next query.\n",
               (int)row.id);
        index_free(index);
        storage_close_table(table);
        return EXEC_INDEX_PERSIST_FAILED;
    }

    printf("Row inserted successfully (id=%d).\n", (int)row.id);
    index_free(index);
    storage_close_table(table);
    return EXEC_OK;
}

ExecStatus execute(AST *ast) {
    SemanticStatus sem;

    if (ast == NULL) {
        return EXEC_UNKNOWN;
    }

    sem = semantic_validate(ast);
    if (sem != SEMANTIC_OK) {
        printf("Error: Semantic error: %s.\n", semantic_status_string(sem));
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
