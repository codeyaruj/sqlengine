#include "index.h"
#include "storage.h"
#include <stdlib.h>
#include <string.h>

unsigned int hash(int key) {
    return ((unsigned int)key * 31) % INDEX_SIZE;
}

Index* index_create(const char* table_name) {
    Index* index = malloc(sizeof(Index));
    if (index == NULL) {
        return NULL;
    }
    
    strncpy(index->table_name, table_name, 31);
    index->table_name[31] = '\0';
    
    for (int i = 0; i < INDEX_SIZE; i++) {
        index->buckets[i] = NULL;
    }
    
    return index;
}

int index_insert(Index* index, int key, long file_offset) {
    if (index == NULL) {
        return -1;
    }
    
    unsigned int bucket = hash(key);
    
    IndexNode* node = index->buckets[bucket];
    while (node != NULL) {
        if (node->entry.key == key) {
            node->entry.file_offset = file_offset;
            return 0;
        }
        node = node->next;
    }
    
    IndexNode* new_node = malloc(sizeof(IndexNode));
    if (new_node == NULL) {
        return -1;
    }
    
    new_node->entry.key = key;
    new_node->entry.file_offset = file_offset;
    new_node->next = index->buckets[bucket];
    index->buckets[bucket] = new_node;
    
    return 0;
}

long index_lookup(Index* index, int key) {
    if (index == NULL) {
        return -1;
    }
    
    unsigned int bucket = hash(key);
    IndexNode* node = index->buckets[bucket];
    
    while (node != NULL) {
        if (node->entry.key == key) {
            return node->entry.file_offset;
        }
        node = node->next;
    }
    
    return -1;
}

void index_free(Index* index) {
    if (index == NULL) {
        return;
    }
    
    for (int i = 0; i < INDEX_SIZE; i++) {
        IndexNode* node = index->buckets[i];
        while (node != NULL) {
            IndexNode* next = node->next;
            free(node);
            node = next;
        }
    }
    
    free(index);
}

int index_build(Index* index, FILE* table_file) {
    if (index == NULL || table_file == NULL) {
        return -1;
    }
    
    size_t row_size = sizeof(int) + NAME_SIZE;
    
    fseek(table_file, 0, SEEK_END);
    long file_size = ftell(table_file);
    
    fseek(table_file, 0, SEEK_SET);
    long pos = 0;
    char buffer[PAGE_SIZE];
    
    while (pos < file_size) {
        fseek(table_file, pos, SEEK_SET);
        
        long bytes_to_read = (file_size - pos) < PAGE_SIZE ? (file_size - pos) : PAGE_SIZE;
        if (fread(buffer, 1, bytes_to_read, table_file) != (size_t)bytes_to_read) {
            return -1;
        }
        
        long row_offset = 0;
        while (row_offset + (long)row_size <= bytes_to_read && pos + row_offset + (long)row_size <= file_size) {
            int key;
            memcpy(&key, buffer + row_offset, sizeof(int));
            
            long file_offset = pos + row_offset;
            if (index_insert(index, key, file_offset) != 0) {
                return -1;
            }
            
            row_offset += row_size;
        }
        
        pos += bytes_to_read;
    }
    
    return 0;
}

int index_persist(Index* index) {
    if (index == NULL) {
        return -1;
    }
    
    char filename[64];
    snprintf(filename, sizeof(filename), "%s.idx", index->table_name);
    
    FILE* file = fopen(filename, "wb");
    if (file == NULL) {
        return -1;
    }
    
    int entry_count = 0;
    for (int i = 0; i < INDEX_SIZE; i++) {
        IndexNode* node = index->buckets[i];
        while (node != NULL) {
            entry_count++;
            node = node->next;
        }
    }
    
    if (fwrite(&entry_count, sizeof(int), 1, file) != 1) {
        fclose(file);
        return -1;
    }
    
    for (int i = 0; i < INDEX_SIZE; i++) {
        IndexNode* node = index->buckets[i];
        while (node != NULL) {
            if (fwrite(&node->entry, sizeof(IndexEntry), 1, file) != 1) {
                fclose(file);
                return -1;
            }
            node = node->next;
        }
    }
    
    fclose(file);
    return 0;
}

Index* index_load(const char* table_name) {
    char filename[64];
    snprintf(filename, sizeof(filename), "%s.idx", table_name);
    
    FILE* file = fopen(filename, "rb");
    if (file == NULL) {
        return NULL;
    }
    
    Index* index = index_create(table_name);
    if (index == NULL) {
        fclose(file);
        return NULL;
    }
    
    int entry_count;
    if (fread(&entry_count, sizeof(int), 1, file) != 1) {
        index_free(index);
        fclose(file);
        return NULL;
    }
    
    for (int i = 0; i < entry_count; i++) {
        IndexEntry entry;
        if (fread(&entry, sizeof(IndexEntry), 1, file) != 1) {
            index_free(index);
            fclose(file);
            return NULL;
        }
        
        if (index_insert(index, entry.key, entry.file_offset) != 0) {
            index_free(index);
            fclose(file);
            return NULL;
        }
    }
    
    fclose(file);
    return index;
}

int index_rebuild(const char* table_name) {
    char tbl_filename[64];
    snprintf(tbl_filename, sizeof(tbl_filename), "%s.tbl", table_name);
    
    FILE* tbl_file = fopen(tbl_filename, "rb");
    if (tbl_file == NULL) {
        return -1;
    }
    
    Index* index = index_create(table_name);
    if (index == NULL) {
        fclose(tbl_file);
        return -1;
    }
    
    if (index_build(index, tbl_file) != 0) {
        index_free(index);
        fclose(tbl_file);
        return -1;
    }
    
    if (index_persist(index) != 0) {
        index_free(index);
        fclose(tbl_file);
        return -1;
    }
    
    index_free(index);
    fclose(tbl_file);
    return 0;
}