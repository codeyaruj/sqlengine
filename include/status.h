#ifndef STATUS_H
#define STATUS_H

/* Tokenizer */
typedef enum {
    TOKENIZE_OK = 0,
    TOKENIZE_INVALID_CHARACTER,
    TOKENIZE_TOO_MANY_TOKENS,
    TOKENIZE_TOKEN_TOO_LONG,
    TOKENIZE_UNTERMINATED_STRING,
    TOKENIZE_NULL_INPUT
} TokenizeStatus;

/* Parser */
typedef enum {
    PARSE_OK = 0,
    PARSE_ERROR,
    PARSE_TRAILING_TOKENS,
    PARSE_UNEXPECTED_EOF,
    PARSE_NULL_INPUT,
    PARSE_TABLE_NAME_TOO_LONG,
    PARSE_IDENTIFIER_TOO_LONG,
    PARSE_STRING_TOO_LONG,
    PARSE_INTEGER_OUT_OF_RANGE
} ParseStatus;

/* Semantic validation */
typedef enum {
    SEMANTIC_OK = 0,
    SEMANTIC_UNKNOWN_COLUMN,
    SEMANTIC_TYPE_MISMATCH,
    SEMANTIC_INVALID_VALUE
} SemanticStatus;

/* Table creation */
typedef enum {
    TABLE_CREATE_OK = 0,
    TABLE_CREATE_ALREADY_EXISTS,
    TABLE_CREATE_IO_ERROR,
    TABLE_CREATE_INVALID_NAME,
    TABLE_CREATE_NAME_TOO_LONG
} TableCreateStatus;

/* Table open / read */
typedef enum {
    TABLE_OK = 0,
    TABLE_NOT_FOUND,
    TABLE_IO_ERROR,
    TABLE_CORRUPT,
    TABLE_INCOMPATIBLE,
    TABLE_ALLOC_ERROR,
    TABLE_INVALID_OFFSET,
    TABLE_READ_ONLY,
    TABLE_RECOVERY_REQUIRED,
    TABLE_DURABILITY_ERROR
} TableStatus;

/* Row insertion */
typedef enum {
    INSERT_OK = 0,
    INSERT_DUPLICATE_ID,
    INSERT_IO_ERROR,
    INSERT_TABLE_CORRUPT,
    INSERT_TABLE_NOT_FOUND,
    INSERT_INDEX_ERROR,
    INSERT_INDEX_PERSIST_FAILED,
    INSERT_ALLOC_ERROR
} InsertStatus;

/* Row iteration / scan */
typedef enum {
    SCAN_OK = 0,
    SCAN_IO_ERROR,
    SCAN_CORRUPT,
    SCAN_ALLOC_ERROR,
    SCAN_END
} ScanStatus;

/* Index operations */
typedef enum {
    INDEX_OK = 0,
    INDEX_NOT_FOUND,
    INDEX_IO_ERROR,
    INDEX_CORRUPT,
    INDEX_INCOMPATIBLE,
    INDEX_ALLOC_ERROR,
    INDEX_INVALID_OFFSET,
    INDEX_KEY_MISMATCH,
    INDEX_DUPLICATE_KEY,
    INDEX_PERSIST_ERROR,
    INDEX_DUPLICATE_PRIMARY_KEY,
    INDEX_TEMPFILE_ERROR,
    INDEX_READ_ONLY
} IndexStatus;

/* Query execution */
typedef enum {
    EXEC_OK = 0,
    EXEC_PARSE_ERROR,
    EXEC_SEMANTIC_ERROR,
    EXEC_TABLE_NOT_FOUND,
    EXEC_TABLE_CORRUPT,
    EXEC_DUPLICATE_ID,
    EXEC_IO_ERROR,
    EXEC_INDEX_ERROR,
    EXEC_INDEX_PERSIST_FAILED,
    EXEC_ALLOC_ERROR,
    EXEC_READ_ONLY,
    EXEC_UNKNOWN
} ExecStatus;

const char *tokenize_status_string(TokenizeStatus s);
const char *parse_status_string(ParseStatus s);
const char *semantic_status_string(SemanticStatus s);
const char *table_create_status_string(TableCreateStatus s);
const char *insert_status_string(InsertStatus s);
const char *index_status_string(IndexStatus s);
const char *exec_status_string(ExecStatus s);

#endif
