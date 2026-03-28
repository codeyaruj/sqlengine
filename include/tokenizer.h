#ifndef TOKENIZER_H
#define TOKENIZER_H

#include <stdio.h>

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
    char value[64];
} Token;

int tokenize(const char* input, Token* tokens, int max_tokens);

const char* token_type_to_string(TokenType type);

#endif