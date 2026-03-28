#ifndef PARSER_H
#define PARSER_H

#include "tokenizer.h"

#define MAX_COLUMNS 10

typedef enum {
    AST_SELECT,
    AST_INSERT,
    AST_UNKNOWN
} ASTType;

typedef struct {
    char table_name[32];
    int select_all;
    int column_count;
    char columns[MAX_COLUMNS][32];
    int has_where;
    char where_column[32];
    char where_value[32];
} SelectQuery;

typedef struct {
    char table_name[32];
    int id;
    char name[32];
} InsertQuery;

typedef struct {
    ASTType type;
    union {
        SelectQuery select;
        InsertQuery insert;
    } query;
} AST;

int parse(Token* tokens, int token_count, AST* ast);

void ast_free(AST* ast);

#endif