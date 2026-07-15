#ifndef PARSER_H
#define PARSER_H

#include "status.h"
#include "sql_limits.h"
#include "tokenizer.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define MAX_COLUMNS 10

typedef enum {
    AST_SELECT,
    AST_INSERT,
    AST_UNKNOWN
} ASTType;

typedef enum {
    LITERAL_NUMBER,
    LITERAL_STRING
} LiteralType;

typedef struct {
    char table_name[SQL_TABLE_NAME_CAPACITY];
    int select_all;
    int column_count;
    char columns[MAX_COLUMNS][SQL_IDENTIFIER_CAPACITY];
    int has_where;
    char where_column[SQL_IDENTIFIER_CAPACITY];
    char where_value[SQL_STORED_NAME_CAPACITY];
    LiteralType where_value_type;
} SelectQuery;

typedef struct {
    char table_name[SQL_TABLE_NAME_CAPACITY];
    int32_t id;
    char name[SQL_STORED_NAME_CAPACITY];
} InsertQuery;

typedef struct {
    ASTType type;
    union {
        SelectQuery select;
        InsertQuery insert;
    } query;
} AST;

typedef struct {
    TokenBuffer tokens;
    size_t position;
} Parser;

/*
 * Token-buffer contract:
 *
 * - tokens.data may be NULL only when tokens.count is zero.
 * - When tokens.count is nonzero, tokens.data points to at least tokens.count
 *   initialized Token objects.
 * - The parser reads only [tokens.data, tokens.data + tokens.count).
 * - TOKEN_EOF is optional for memory safety. Reaching tokens.count without one
 *   is reported as unexpected end of input.
 *
 * A count larger than the accessible allocation violates this contract and
 * cannot be detected portably from a C pointer alone.
 */
void parser_init(Parser *p, TokenBuffer tokens);
bool parser_at_end(const Parser *p);
const Token *parser_peek(const Parser *p);
const Token *parser_previous(const Parser *p);
const Token *parser_advance(Parser *p);
bool parser_check(const Parser *p, TokenType type);
bool parser_match(Parser *p, TokenType type);
bool parser_expect(Parser *p, TokenType type);

ParseStatus parse_tokens(TokenBuffer tokens, AST *ast);
void ast_free(AST *ast);

#endif
