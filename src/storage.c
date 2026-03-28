#include "storage.h"
#include <string.h>
#include <stdlib.h>
#include <stdint.h>

void serialize_row(Row* src, void* dest) {
    uint8_t* ptr = (uint8_t*)dest;
    memcpy(ptr, &src->id, sizeof(int));
    ptr += sizeof(int);
    memcpy(ptr, src->name, NAME_SIZE);
}

void deserialize_row(void* src, Row* dest) {
    uint8_t* ptr = (uint8_t*)src;
    memcpy(&dest->id, ptr, sizeof(int));
    ptr += sizeof(int);
    memcpy(dest->name, ptr, NAME_SIZE);
}

int create_table(const char* table_name) {
    char filename[64];
    snprintf(filename, sizeof(filename), "%s.tbl", table_name);
    
    FILE* file = fopen(filename, "rb+");
    if (file != NULL) {
        fclose(file);
        return 0;
    }
    
    file = fopen(filename, "wb+");
    if (file == NULL) {
        return -1;
    }
    
    fclose(file);
    return 0;
}

Table* open_table(const char* table_name) {
    Table* table = malloc(sizeof(Table));
    if (table == NULL) {
        return NULL;
    }
    
    strncpy(table->name, table_name, 31);
    table->name[31] = '\0';
    
    char filename[64];
    snprintf(filename, sizeof(filename), "%s.tbl", table_name);
    
    table->file = fopen(filename, "rb+");
    if (table->file == NULL) {
        free(table);
        return NULL;
    }
    
    return table;
}

void close_table(Table* table) {
    if (table != NULL) {
        if (table->file != NULL) {
            fclose(table->file);
        }
        free(table);
    }
}

int insert_row(Table* table, Row* row, long* out_offset) {
    if (table == NULL || table->file == NULL || row == NULL) {
        return -1;
    }
    
    size_t row_size = sizeof(int) + NAME_SIZE;
    size_t rows_per_page = PAGE_SIZE / row_size;
    
    fseek(table->file, 0, SEEK_END);
    long file_size = ftell(table->file);
    
    long page_index = file_size / PAGE_SIZE;
    size_t offset_in_page = (size_t)(file_size % PAGE_SIZE);
    size_t rows_in_page = offset_in_page / row_size;
    
    if (rows_in_page >= rows_per_page) {
        page_index++;
        offset_in_page = 0;
    }
    
    char slot[36];
    serialize_row(row, slot);
    
    long pos = page_index * PAGE_SIZE + offset_in_page;
    if (fseek(table->file, pos, SEEK_SET) != 0) {
        return -1;
    }
    
    if (fwrite(slot, row_size, 1, table->file) != 1) {
        return -1;
    }
    
    fflush(table->file);
    
    if (out_offset != NULL) {
        *out_offset = pos;
    }
    
    return 0;
}

int select_all_rows(Table* table, Row** rows, int* count) {
    if (table == NULL || table->file == NULL || rows == NULL || count == NULL) {
        return -1;
    }
    
    size_t row_size = sizeof(int) + NAME_SIZE;
    
    fseek(table->file, 0, SEEK_END);
    long file_size = ftell(table->file);
    
    *count = (int)(file_size / row_size);
    
    if (*count == 0) {
        *rows = NULL;
        return 0;
    }
    
    *rows = malloc((*count) * sizeof(Row));
    if (*rows == NULL) {
        return -1;
    }
    
    fseek(table->file, 0, SEEK_SET);
    
    char buffer[row_size];
    int idx = 0;
    long pos = 0;
    
    while (pos < file_size && idx < *count) {
        if (fseek(table->file, pos, SEEK_SET) != 0) {
            free(*rows);
            *rows = NULL;
            return -1;
        }
        
        if (fread(buffer, row_size, 1, table->file) != 1) {
            free(*rows);
            *rows = NULL;
            return -1;
        }
        
        deserialize_row(buffer, &(*rows)[idx]);
        idx++;
        pos += row_size;
    }
    
    *count = idx;
    return 0;
}

int select_row_at_offset(Table* table, long offset, Row* row) {
    if (table == NULL || table->file == NULL || row == NULL) {
        return -1;
    }
    
    size_t row_size = sizeof(int) + NAME_SIZE;
    
    if (fseek(table->file, offset, SEEK_SET) != 0) {
        return -1;
    }
    
    char buffer[36];
    if (fread(buffer, row_size, 1, table->file) != 1) {
        return -1;
    }
    
    deserialize_row(buffer, row);
    return 0;
}