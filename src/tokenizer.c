#include "tokenizer.h"
#include <string.h>
#include <ctype.h>
#include <stdlib.h>

static TokenType keyword_lookup(const char* word) {
    if (strcmp(word, "SELECT") == 0) return TOKEN_SELECT;
    if (strcmp(word, "INSERT") == 0) return TOKEN_INSERT;
    if (strcmp(word, "FROM") == 0) return TOKEN_FROM;
    if (strcmp(word, "WHERE") == 0) return TOKEN_WHERE;
    if (strcmp(word, "INTO") == 0) return TOKEN_INTO;
    if (strcmp(word, "VALUES") == 0) return TOKEN_VALUES;
    return TOKEN_IDENTIFIER;
}

int tokenize(const char* input, Token* tokens, int max_tokens) {
    int count = 0;
    const char* p = input;
    
    while (*p != '\0' && count < max_tokens - 1) {
        while (isspace((unsigned char)*p)) p++;
        
        if (*p == '\0') break;
        
        Token token;
        token.value[0] = '\0';
        
        if (*p == ',') {
            token.type = TOKEN_COMMA;
            token.value[0] = ',';
            token.value[1] = '\0';
            p++;
        }
        else if (*p == '*') {
            token.type = TOKEN_STAR;
            token.value[0] = '*';
            token.value[1] = '\0';
            p++;
        }
        else if (*p == '=') {
            token.type = TOKEN_EQUAL;
            token.value[0] = '=';
            token.value[1] = '\0';
            p++;
        }
        else if (*p == '(') {
            token.type = TOKEN_LPAREN;
            token.value[0] = '(';
            token.value[1] = '\0';
            p++;
        }
        else if (*p == ')') {
            token.type = TOKEN_RPAREN;
            token.value[0] = ')';
            token.value[1] = '\0';
            p++;
        }
        else if (*p == ';') {
            token.type = TOKEN_SEMICOLON;
            token.value[0] = ';';
            token.value[1] = '\0';
            p++;
        }
        else if (*p == '"') {
            token.type = TOKEN_STRING;
            p++;
            int i = 0;
            while (*p != '"' && *p != '\0' && i < 62) {
                token.value[i++] = *p++;
            }
            token.value[i] = '\0';
            if (*p == '"') p++;
        }
        else if (isdigit((unsigned char)*p)) {
            token.type = TOKEN_NUMBER;
            int i = 0;
            while (isdigit((unsigned char)*p) && i < 62) {
                token.value[i++] = *p++;
            }
            token.value[i] = '\0';
        }
        else if (isalpha((unsigned char)*p) || *p == '_') {
            int i = 0;
            while (isalnum((unsigned char)*p) || *p == '_') {
                if (i < 62) {
                    token.value[i++] = *p;
                }
                p++;
            }
            token.value[i] = '\0';
            token.type = keyword_lookup(token.value);
        }
        else {
            token.type = TOKEN_UNKNOWN;
            token.value[0] = *p;
            token.value[1] = '\0';
            p++;
        }
        
        tokens[count++] = token;
    }
    
    tokens[count].type = TOKEN_EOF;
    tokens[count].value[0] = '\0';
    
    return count + 1;
}

const char* token_type_to_string(TokenType type) {
    switch (type) {
        case TOKEN_SELECT: return "SELECT";
        case TOKEN_INSERT: return "INSERT";
        case TOKEN_FROM: return "FROM";
        case TOKEN_WHERE: return "WHERE";
        case TOKEN_INTO: return "INTO";
        case TOKEN_VALUES: return "VALUES";
        case TOKEN_IDENTIFIER: return "IDENTIFIER";
        case TOKEN_NUMBER: return "NUMBER";
        case TOKEN_STRING: return "STRING";
        case TOKEN_STAR: return "STAR";
        case TOKEN_COMMA: return "COMMA";
        case TOKEN_EQUAL: return "EQUAL";
        case TOKEN_LPAREN: return "LPAREN";
        case TOKEN_RPAREN: return "RPAREN";
        case TOKEN_SEMICOLON: return "SEMICOLON";
        case TOKEN_EOF: return "EOF";
        default: return "UNKNOWN";
    }
}