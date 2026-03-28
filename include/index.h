#ifndef INDEX_H
#define INDEX_H

#include <stdio.h>

#define INDEX_SIZE 256

typedef struct {
    int key;
    long file_offset;
} IndexEntry;

typedef struct IndexNode {
    IndexEntry entry;
    struct IndexNode* next;
} IndexNode;

typedef struct {
    char table_name[32];
    IndexNode* buckets[INDEX_SIZE];
} Index;

unsigned int hash(int key);

Index* index_create(const char* table_name);
int index_insert(Index* index, int key, long file_offset);
long index_lookup(Index* index, int key);
void index_free(Index* index);

int index_build(Index* index, FILE* table_file);
int index_persist(Index* index);
Index* index_load(const char* table_name);

int index_rebuild(const char* table_name);

#endif