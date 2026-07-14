#include "status.h"

const char *tokenize_status_string(TokenizeStatus s) {
    switch (s) {
        case TOKENIZE_OK: return "ok";
        case TOKENIZE_INVALID_CHARACTER: return "invalid character";
        case TOKENIZE_TOO_MANY_TOKENS: return "too many tokens";
        case TOKENIZE_TOKEN_TOO_LONG: return "token too long";
        case TOKENIZE_UNTERMINATED_STRING: return "unterminated string";
        case TOKENIZE_NULL_INPUT: return "null input";
        default: return "unknown tokenize error";
    }
}

const char *parse_status_string(ParseStatus s) {
    switch (s) {
        case PARSE_OK: return "ok";
        case PARSE_ERROR: return "syntax error";
        case PARSE_TRAILING_TOKENS: return "trailing tokens after statement";
        case PARSE_UNEXPECTED_EOF: return "unexpected end of input";
        case PARSE_NULL_INPUT: return "null input";
        default: return "unknown parse error";
    }
}

const char *semantic_status_string(SemanticStatus s) {
    switch (s) {
        case SEMANTIC_OK: return "ok";
        case SEMANTIC_UNKNOWN_COLUMN: return "unknown column";
        case SEMANTIC_TYPE_MISMATCH: return "type mismatch";
        case SEMANTIC_INVALID_VALUE: return "invalid value";
        default: return "unknown semantic error";
    }
}

const char *table_create_status_string(TableCreateStatus s) {
    switch (s) {
        case TABLE_CREATE_OK: return "created";
        case TABLE_CREATE_ALREADY_EXISTS: return "already exists";
        case TABLE_CREATE_IO_ERROR: return "I/O error";
        case TABLE_CREATE_INVALID_NAME: return "invalid name";
        default: return "unknown create error";
    }
}

const char *insert_status_string(InsertStatus s) {
    switch (s) {
        case INSERT_OK: return "ok";
        case INSERT_DUPLICATE_ID: return "duplicate id";
        case INSERT_IO_ERROR: return "I/O error";
        case INSERT_TABLE_CORRUPT: return "table corrupt";
        case INSERT_TABLE_NOT_FOUND: return "table not found";
        case INSERT_INDEX_ERROR: return "index error";
        case INSERT_INDEX_PERSIST_FAILED: return "index persist failed";
        case INSERT_ALLOC_ERROR: return "allocation failure";
        default: return "unknown insert error";
    }
}

const char *index_status_string(IndexStatus s) {
    switch (s) {
        case INDEX_OK: return "ok";
        case INDEX_NOT_FOUND: return "index not found";
        case INDEX_IO_ERROR: return "I/O error";
        case INDEX_CORRUPT: return "index corrupt";
        case INDEX_INCOMPATIBLE: return "incompatible index format";
        case INDEX_ALLOC_ERROR: return "allocation failure";
        case INDEX_INVALID_OFFSET: return "invalid offset";
        case INDEX_KEY_MISMATCH: return "key mismatch";
        case INDEX_DUPLICATE_KEY: return "duplicate key";
        case INDEX_PERSIST_ERROR: return "persist error";
        default: return "unknown index error";
    }
}

const char *exec_status_string(ExecStatus s) {
    switch (s) {
        case EXEC_OK: return "ok";
        case EXEC_PARSE_ERROR: return "parse error";
        case EXEC_SEMANTIC_ERROR: return "semantic error";
        case EXEC_TABLE_NOT_FOUND: return "table not found";
        case EXEC_TABLE_CORRUPT: return "table corrupt";
        case EXEC_DUPLICATE_ID: return "duplicate id";
        case EXEC_IO_ERROR: return "I/O error";
        case EXEC_INDEX_ERROR: return "index error";
        case EXEC_INDEX_PERSIST_FAILED: return "index persist failed";
        case EXEC_ALLOC_ERROR: return "allocation failure";
        case EXEC_UNKNOWN: return "unknown error";
        default: return "unknown execution error";
    }
}
