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

/*
 * Tokenize input into tokens[0 .. *out_count-1], always ending with TOKEN_EOF
 * when status is TOKENIZE_OK. On error, *out_count may be partial; do not use.
 *
 * String literals: double-quoted only ("..."). No escape sequences; a quote
 * ends the string. Unterminated strings return TOKENIZE_UNTERMINATED_STRING.
 * Identifiers and keywords are case-sensitive in content; keywords are matched
 * case-insensitively.
 */
TokenizeStatus tokenize(const char *input, Token *tokens, int max_tokens, int *out_count);

const char *token_type_to_string(TokenType type);

#endif
