#ifndef STORAGE_H
#define STORAGE_H

#define PAGE_SIZE 4096
#define NAME_SIZE 32

#include <stdio.h>

typedef struct {
    int id;
    char name[NAME_SIZE];
} Row;

typedef struct {
    char data[PAGE_SIZE];
} Page;

typedef struct {
    char name[32];
    FILE* file;
} Table;

void serialize_row(Row* src, void* dest);
void deserialize_row(void* src, Row* dest);

int create_table(const char* table_name);
Table* open_table(const char* table_name);
void close_table(Table* table);

int insert_row(Table* table, Row* row, long* out_offset);
int select_all_rows(Table* table, Row** rows, int* count);
int select_row_at_offset(Table* table, long offset, Row* row);

#endif