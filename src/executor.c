#include "executor.h"
#include "storage.h"
#include "index.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

static int column_index(const char* col_name) {
    if (strcmp(col_name, "id") == 0) return 0;
    if (strcmp(col_name, "name") == 0) return 1;
    return -1;
}

static int eval_where(Row* row, SelectQuery* sel) {
    if (!sel->has_where) return 1;
    
    int idx = column_index(sel->where_column);
    if (idx < 0) return 0;
    
    if (idx == 0) {
        int where_val = atoi(sel->where_value);
        return row->id == where_val;
    }
    else {
        return strcmp(row->name, sel->where_value) == 0;
    }
}

static void print_row(Row* row, SelectQuery* sel) {
    if (sel->select_all) {
        printf("%-10d %s\n", row->id, row->name);
    }
    else {
        for (int i = 0; i < sel->column_count; i++) {
            if (i > 0) printf(" ");
            int idx = column_index(sel->columns[i]);
            if (idx == 0) {
                printf("%d", row->id);
            }
            else if (idx == 1) {
                printf("%s", row->name);
            }
        }
        printf("\n");
    }
}

static int print_header(SelectQuery* sel) {
    if (sel->select_all) {
        printf("%-10s %s\n", "id", "name");
        printf("%-10s %s\n", "----------", "------");
    }
    return 0;
}

static int execute_select(AST* ast) {
    SelectQuery* sel = &ast->query.select;
    
    Table* table = open_table(sel->table_name);
    if (table == NULL) {
        printf("Error: Table '%s' does not exist.\n", sel->table_name);
        return -1;
    }
    
    int printed = 0;
    int use_index = 0;
    int search_id = -1;
    
    if (sel->has_where && 
        strcmp(sel->where_column, "id") == 0) {
        search_id = atoi(sel->where_value);
        if (search_id != 0 || strcmp(sel->where_value, "0") == 0) {
            Index* index = index_load(sel->table_name);
            if (index != NULL) {
                long offset = index_lookup(index, search_id);
                if (offset >= 0) {
                    use_index = 1;
                    Row row;
                    if (select_row_at_offset(table, offset, &row) == 0) {
                        if (eval_where(&row, sel)) {
                            print_header(sel);
                            print_row(&row, sel);
                            printed = 1;
                        }
                    }
                }
                index_free(index);
            }
        }
    }
    
    if (!use_index) {
        Row* rows = NULL;
        int count = 0;
        
        if (select_all_rows(table, &rows, &count) != 0) {
            printf("Error: Could not read rows.\n");
            close_table(table);
            return -1;
        }
        
        if (count > 0) {
            print_header(sel);
        }
        
        for (int i = 0; i < count; i++) {
            if (eval_where(&rows[i], sel)) {
                print_row(&rows[i], sel);
                printed++;
            }
        }
        
        free(rows);
    }
    
    if (printed == 0) {
        printf("(empty result)\n");
    } else {
        printf("\n(%d row(s) returned)\n", printed);
    }
    
    close_table(table);
    return 0;
}

static int execute_insert(AST* ast) {
    InsertQuery* ins = &ast->query.insert;
    
    Table* table = open_table(ins->table_name);
    if (table == NULL) {
        printf("Error: Table '%s' does not exist.\n", ins->table_name);
        return -1;
    }
    
    Row row;
    row.id = ins->id;
    strncpy(row.name, ins->name, NAME_SIZE - 1);
    row.name[NAME_SIZE - 1] = '\0';
    
    long offset = -1;
    int result = insert_row(table, &row, &offset);
    
    if (result == 0) {
        Index* index = index_load(ins->table_name);
        if (index == NULL) {
            index = index_create(ins->table_name);
        }
        
        if (index != NULL) {
            index_insert(index, row.id, offset);
            index_persist(index);
            index_free(index);
        }
        
        printf("Row inserted successfully (id=%d).\n", row.id);
    }
    else {
        printf("Error: Could not insert row.\n");
    }
    
    close_table(table);
    return result;
}

int execute(AST* ast) {
    if (ast == NULL) return -1;
    
    switch (ast->type) {
        case AST_SELECT:
            return execute_select(ast);
        case AST_INSERT:
            return execute_insert(ast);
        default:
            printf("Error: Unknown query type.\n");
            return -1;
    }
}