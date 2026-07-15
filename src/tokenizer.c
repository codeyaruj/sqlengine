#include "tokenizer.h"
#include "util.h"

#include <ctype.h>
#include <string.h>

static TokenType keyword_lookup(const char *word) {
    /* Keywords are matched case-insensitively; identifier spelling is preserved by caller. */
    if (util_strncasecmp(word, "SELECT", 6) == 0 && word[6] == '\0') return TOKEN_SELECT;
    if (util_strncasecmp(word, "INSERT", 6) == 0 && word[6] == '\0') return TOKEN_INSERT;
    if (util_strncasecmp(word, "FROM", 4) == 0 && word[4] == '\0') return TOKEN_FROM;
    if (util_strncasecmp(word, "WHERE", 5) == 0 && word[5] == '\0') return TOKEN_WHERE;
    if (util_strncasecmp(word, "INTO", 4) == 0 && word[4] == '\0') return TOKEN_INTO;
    if (util_strncasecmp(word, "VALUES", 6) == 0 && word[6] == '\0') return TOKEN_VALUES;
    return TOKEN_IDENTIFIER;
}

/* Consume rest of an overlong token of the given kind so we do not split it. */
static void consume_rest_ident(const char **pp) {
    const char *p = *pp;
    while (isalnum((unsigned char)*p) || *p == '_') {
        p++;
    }
    *pp = p;
}

static void consume_rest_number(const char **pp) {
    const char *p = *pp;
    if (*p == '-') {
        p++;
    }
    while (isdigit((unsigned char)*p)) {
        p++;
    }
    *pp = p;
}

static void consume_rest_string(const char **pp) {
    const char *p = *pp;
    while (*p != '\0' && *p != '"') {
        p++;
    }
    if (*p == '"') {
        p++;
    }
    *pp = p;
}

TokenizeStatus tokenize(const char *input, Token *tokens, int max_tokens, int *out_count) {
    int count = 0;
    const char *p;

    if (out_count != NULL) {
        *out_count = 0;
    }
    if (input == NULL || tokens == NULL || max_tokens < 1) {
        return TOKENIZE_NULL_INPUT;
    }

    p = input;

    while (*p != '\0') {
        Token token;

        while (isspace((unsigned char)*p)) {
            p++;
        }
        if (*p == '\0') {
            break;
        }

        /* Need room for this token plus a final EOF. */
        if (count >= max_tokens - 1) {
            return TOKENIZE_TOO_MANY_TOKENS;
        }

        memset(&token, 0, sizeof(token));

        if (*p == ',') {
            token.type = TOKEN_COMMA;
            token.value[0] = ',';
            token.value[1] = '\0';
            p++;
        } else if (*p == '*') {
            token.type = TOKEN_STAR;
            token.value[0] = '*';
            token.value[1] = '\0';
            p++;
        } else if (*p == '=') {
            token.type = TOKEN_EQUAL;
            token.value[0] = '=';
            token.value[1] = '\0';
            p++;
        } else if (*p == '(') {
            token.type = TOKEN_LPAREN;
            token.value[0] = '(';
            token.value[1] = '\0';
            p++;
        } else if (*p == ')') {
            token.type = TOKEN_RPAREN;
            token.value[0] = ')';
            token.value[1] = '\0';
            p++;
        } else if (*p == ';') {
            token.type = TOKEN_SEMICOLON;
            token.value[0] = ';';
            token.value[1] = '\0';
            p++;
        } else if (*p == '"') {
            int i = 0;
            p++; /* opening quote */
            while (*p != '"' && *p != '\0') {
                if (i >= TOKEN_VALUE_MAX) {
                    /* Too long: consume remainder of the string then fail. */
                    consume_rest_string(&p);
                    return TOKENIZE_TOKEN_TOO_LONG;
                }
                token.value[i++] = *p++;
            }
            token.value[i] = '\0';
            if (*p != '"') {
                return TOKENIZE_UNTERMINATED_STRING;
            }
            p++; /* closing quote */
            token.type = TOKEN_STRING;
        } else if (isdigit((unsigned char)*p) ||
                   (*p == '-' && isdigit((unsigned char)p[1]))) {
            int i = 0;
            if (*p == '-') {
                token.value[i++] = *p++;
            }
            while (isdigit((unsigned char)*p)) {
                if (i >= TOKEN_VALUE_MAX) {
                    consume_rest_number(&p);
                    return TOKENIZE_TOKEN_TOO_LONG;
                }
                token.value[i++] = *p++;
            }
            token.value[i] = '\0';
            token.type = TOKEN_NUMBER;
        } else if (isalpha((unsigned char)*p) || *p == '_') {
            int i = 0;
            while (isalnum((unsigned char)*p) || *p == '_') {
                if (i >= TOKEN_VALUE_MAX) {
                    consume_rest_ident(&p);
                    return TOKENIZE_TOKEN_TOO_LONG;
                }
                token.value[i++] = *p++;
            }
            token.value[i] = '\0';
            token.type = keyword_lookup(token.value);
        } else {
            return TOKENIZE_INVALID_CHARACTER;
        }

        tokens[count++] = token;
    }

    if (count >= max_tokens) {
        return TOKENIZE_TOO_MANY_TOKENS;
    }

    tokens[count].type = TOKEN_EOF;
    tokens[count].value[0] = '\0';
    count++;

    if (out_count != NULL) {
        *out_count = count;
    }
    return TOKENIZE_OK;
}

const char *token_type_to_string(TokenType type) {
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
