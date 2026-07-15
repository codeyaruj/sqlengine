#ifndef TOKENIZER_H
#define TOKENIZER_H

#include "status.h"

#include <stddef.h>

/* Maximum characters stored in a token value (excluding NUL). */
#define TOKEN_VALUE_MAX 63

typedef enum {
    TOKEN_SELECT,
    TOKEN_INSERT,
    TOKEN_FROM,
    TOKEN_WHERE,
    TOKEN_INTO,
    TOKEN_VALUES,
    TOKEN_IDENTIFIER,
    TOKEN_NUMBER,
    TOKEN_STRING,
    TOKEN_STAR,
    TOKEN_COMMA,
    TOKEN_EQUAL,
    TOKEN_LPAREN,
    TOKEN_RPAREN,
    TOKEN_SEMICOLON,
    TOKEN_EOF,
    TOKEN_UNKNOWN
} TokenType;

typedef struct {
    TokenType type;
    char value[TOKEN_VALUE_MAX + 1];
} Token;

typedef struct {
    const Token *data;
    size_t count;
} TokenBuffer;

/* Use only with an actual Token array, never with a Token pointer. */
#define TOKEN_BUFFER_FROM_ARRAY(array) \
    ((TokenBuffer){ .data = (array), .count = sizeof(array) / sizeof((array)[0]) })

/*
 * Tokenize input into storage and return the exact initialized range in result.
 * On success, result includes the final TOKEN_EOF. On failure, result is reset
 * to the safe empty state { NULL, 0 }, even if storage was partially written.
 *
 * String literals: double-quoted only ("..."). No escape sequences; a quote
 * ends the string. Unterminated strings return TOKENIZE_UNTERMINATED_STRING.
 * Identifiers and keywords are case-sensitive in content; keywords are matched
 * case-insensitively.
 */
TokenizeStatus tokenize(const char *input, Token *storage, size_t storage_capacity,
                        TokenBuffer *result);

const char *token_type_to_string(TokenType type);

#endif
