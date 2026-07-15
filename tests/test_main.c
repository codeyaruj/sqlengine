#include "cli.h"
#include "column.h"
#include "executor.h"
#include "index.h"
#include "parser.h"
#include "semantic.h"
#include "status.h"
#include "storage.h"
#include "tokenizer.h"
#include "util.h"

#include <errno.h>
#include <dirent.h>
#include <fcntl.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

static int g_failures = 0;
static int g_passed = 0;
static char g_home[512];
static char g_tmpdir[512];

#define ASSERT_TRUE(cond) do { \
    if (!(cond)) { \
        fprintf(stderr, "  FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); \
        g_failures++; \
        return; \
    } \
} while (0)

#define ASSERT_EQ_INT(a, b) do { \
    long long _a = (long long)(a); \
    long long _b = (long long)(b); \
    if (_a != _b) { \
        fprintf(stderr, "  FAIL %s:%d: %s (%lld) != %s (%lld)\n", \
                __FILE__, __LINE__, #a, _a, #b, _b); \
        g_failures++; \
        return; \
    } \
} while (0)

#define ASSERT_STREQ(a, b) do { \
    if (strcmp((a), (b)) != 0) { \
        fprintf(stderr, "  FAIL %s:%d: \"%s\" != \"%s\"\n", __FILE__, __LINE__, (a), (b)); \
        g_failures++; \
        return; \
    } \
} while (0)

static void test_begin(const char *name) {
    printf("TEST: %s\n", name);
}

static void test_end(void) {
    g_passed++;
}

static void reset_workdir(void) {
    char cmd[640];
    if (chdir(g_home) != 0) {
        perror("chdir home");
        exit(1);
    }
    snprintf(cmd, sizeof(cmd), "rm -rf '%s'", g_tmpdir);
    if (system(cmd) != 0) {
        /* ignore */
    }
    if (mkdir(g_tmpdir, 0700) != 0 && errno != EEXIST) {
        perror("mkdir tmp");
        exit(1);
    }
    if (chdir(g_tmpdir) != 0) {
        perror("chdir tmp");
        exit(1);
    }
}

/* ---- Tokenizer ---- */

static void test_tokenize_valid_select(void) {
    Token tokens[32];
    int n = 0;
    test_begin("tokenizer valid SELECT");
    ASSERT_EQ_INT(tokenize("SELECT * FROM users;", tokens, 32, &n), TOKENIZE_OK);
    ASSERT_EQ_INT(tokens[0].type, TOKEN_SELECT);
    ASSERT_EQ_INT(tokens[1].type, TOKEN_STAR);
    ASSERT_EQ_INT(tokens[2].type, TOKEN_FROM);
    ASSERT_EQ_INT(tokens[3].type, TOKEN_IDENTIFIER);
    ASSERT_STREQ(tokens[3].value, "users");
    ASSERT_EQ_INT(tokens[4].type, TOKEN_SEMICOLON);
    ASSERT_EQ_INT(tokens[5].type, TOKEN_EOF);
    test_end();
}

static void test_tokenize_valid_insert(void) {
    Token tokens[32];
    int n = 0;
    test_begin("tokenizer valid INSERT");
    ASSERT_EQ_INT(tokenize("INSERT INTO users VALUES (1, \"Aruj\");", tokens, 32, &n), TOKENIZE_OK);
    ASSERT_EQ_INT(tokens[0].type, TOKEN_INSERT);
    ASSERT_EQ_INT(tokens[1].type, TOKEN_INTO);
    ASSERT_EQ_INT(tokens[5].type, TOKEN_NUMBER);
    ASSERT_STREQ(tokens[5].value, "1");
    ASSERT_EQ_INT(tokens[7].type, TOKEN_STRING);
    ASSERT_STREQ(tokens[7].value, "Aruj");
    test_end();
}

static void test_tokenize_mixed_case(void) {
    Token tokens[32];
    int n = 0;
    test_begin("tokenizer mixed-case keywords");
    ASSERT_EQ_INT(tokenize("select * From users;", tokens, 32, &n), TOKENIZE_OK);
    ASSERT_EQ_INT(tokens[0].type, TOKEN_SELECT);
    ASSERT_EQ_INT(tokens[2].type, TOKEN_FROM);
    ASSERT_EQ_INT(tokenize("Insert Into users Values (2, \"B\");", tokens, 32, &n), TOKENIZE_OK);
    ASSERT_EQ_INT(tokens[0].type, TOKEN_INSERT);
    ASSERT_EQ_INT(tokens[1].type, TOKEN_INTO);
    ASSERT_EQ_INT(tokens[3].type, TOKEN_VALUES);
    test_end();
}

static void test_tokenize_invalid_char(void) {
    Token tokens[32];
    int n = 0;
    test_begin("tokenizer invalid character");
    ASSERT_EQ_INT(tokenize("SELECT @ FROM users;", tokens, 32, &n), TOKENIZE_INVALID_CHARACTER);
    test_end();
}

static void test_tokenize_long_token(void) {
    char long_ident[TOKEN_VALUE_MAX + 8];
    char too_long[TOKEN_VALUE_MAX + 16];
    char sql[256];
    Token tokens[16];
    int n = 0;
    size_t i;

    test_begin("tokenizer max length and overflow");
    for (i = 0; i < TOKEN_VALUE_MAX; i++) {
        long_ident[i] = 'a';
    }
    long_ident[TOKEN_VALUE_MAX] = '\0';
    snprintf(sql, sizeof(sql), "SELECT %s FROM t;", long_ident);
    ASSERT_EQ_INT(tokenize(sql, tokens, 16, &n), TOKENIZE_OK);

    for (i = 0; i < TOKEN_VALUE_MAX + 1; i++) {
        too_long[i] = 'b';
    }
    too_long[TOKEN_VALUE_MAX + 1] = '\0';
    snprintf(sql, sizeof(sql), "SELECT %s FROM t;", too_long);
    ASSERT_EQ_INT(tokenize(sql, tokens, 16, &n), TOKENIZE_TOKEN_TOO_LONG);
    test_end();
}

static void test_tokenize_long_number_string(void) {
    char num[TOKEN_VALUE_MAX + 4];
    char strbody[TOKEN_VALUE_MAX + 4];
    char sql[256];
    Token tokens[16];
    int n = 0;
    size_t i;

    test_begin("tokenizer long number and string");
    for (i = 0; i < TOKEN_VALUE_MAX + 1; i++) {
        num[i] = '9';
    }
    num[TOKEN_VALUE_MAX + 1] = '\0';
    snprintf(sql, sizeof(sql), "SELECT * FROM t WHERE id = %s;", num);
    ASSERT_EQ_INT(tokenize(sql, tokens, 16, &n), TOKENIZE_TOKEN_TOO_LONG);

    for (i = 0; i < TOKEN_VALUE_MAX + 1; i++) {
        strbody[i] = 'x';
    }
    strbody[TOKEN_VALUE_MAX + 1] = '\0';
    snprintf(sql, sizeof(sql), "INSERT INTO t VALUES (1, \"%s\");", strbody);
    ASSERT_EQ_INT(tokenize(sql, tokens, 16, &n), TOKENIZE_TOKEN_TOO_LONG);
    test_end();
}

static void test_tokenize_unterminated(void) {
    Token tokens[16];
    int n = 0;
    test_begin("tokenizer unterminated string");
    ASSERT_EQ_INT(tokenize("INSERT INTO t VALUES (1, \"abc);", tokens, 16, &n),
                  TOKENIZE_UNTERMINATED_STRING);
    test_end();
}

static void test_tokenize_capacity(void) {
    Token tokens[4];
    int n = 0;
    test_begin("tokenizer token capacity exhaustion");
    /* SELECT * FROM users ; EOF needs 6 slots */
    ASSERT_EQ_INT(tokenize("SELECT * FROM users;", tokens, 4, &n), TOKENIZE_TOO_MANY_TOKENS);
    test_end();
}

/* ---- Parser ---- */

static ParseStatus parse_sql(const char *sql, AST *ast) {
    Token tokens[64];
    int n = 0;
    TokenizeStatus ts = tokenize(sql, tokens, 64, &n);
    if (ts != TOKENIZE_OK) {
        return PARSE_ERROR;
    }
    return parse(tokens, n, ast);
}

static uint64_t table_row_count(const char *name) {
    Table *table = NULL;
    uint64_t count;
    if (storage_open_table_readonly(name, &table) != TABLE_OK || table == NULL) {
        g_failures++;
        return UINT64_MAX;
    }
    count = table->row_count;
    storage_close_table(table);
    return count;
}

static int count_secure_temp_files(void) {
    DIR *directory = opendir(".");
    struct dirent *entry;
    int count = 0;
    if (directory == NULL) {
        g_failures++;
        return -1;
    }
    while ((entry = readdir(directory)) != NULL) {
        if (strstr(entry->d_name, ".tmp.") != NULL) {
            count++;
        }
    }
    closedir(directory);
    return count;
}

static ExecStatus capture_execute(AST *ast, char *buffer, size_t buffer_size) {
    int saved_stdout;
    FILE *capture;
    ExecStatus status;
    size_t amount;

    if (buffer == NULL || buffer_size == 0) {
        g_failures++;
        return EXEC_UNKNOWN;
    }
    fflush(stdout);
    saved_stdout = dup(STDOUT_FILENO);
    if (saved_stdout < 0) {
        g_failures++;
        return EXEC_UNKNOWN;
    }
    capture = tmpfile();
    if (capture == NULL || dup2(fileno(capture), STDOUT_FILENO) < 0) {
        close(saved_stdout);
        g_failures++;
        return EXEC_UNKNOWN;
    }
    status = execute(ast);
    fflush(stdout);
    if (dup2(saved_stdout, STDOUT_FILENO) < 0) {
        g_failures++;
    }
    close(saved_stdout);
    if (fseek(capture, 0, SEEK_SET) != 0) {
        fclose(capture);
        g_failures++;
        return EXEC_UNKNOWN;
    }
    amount = fread(buffer, 1, buffer_size - 1, capture);
    buffer[amount] = '\0';
    fclose(capture);
    return status;
}

static void test_parser_valid(void) {
    AST ast;
    test_begin("parser valid statements");
    ASSERT_EQ_INT(parse_sql("SELECT * FROM users;", &ast), PARSE_OK);
    ASSERT_EQ_INT(ast.type, AST_SELECT);
    ASSERT_EQ_INT(parse_sql("INSERT INTO users VALUES (1, \"A\");", &ast), PARSE_OK);
    ASSERT_EQ_INT(ast.type, AST_INSERT);
    ASSERT_EQ_INT(ast.query.insert.id, 1);
    ASSERT_STREQ(ast.query.insert.name, "A");
    test_end();
}

static void test_parser_incomplete(void) {
    AST ast;
    test_begin("parser incomplete statements");
    ASSERT_TRUE(parse_sql("SELECT", &ast) != PARSE_OK);
    ASSERT_TRUE(parse_sql("SELECT *", &ast) != PARSE_OK);
    ASSERT_TRUE(parse_sql("SELECT * FROM", &ast) != PARSE_OK);
    ASSERT_TRUE(parse_sql("SELECT * FROM users WHERE", &ast) != PARSE_OK);
    ASSERT_TRUE(parse_sql("SELECT * FROM users WHERE id =", &ast) != PARSE_OK);
    ASSERT_TRUE(parse_sql("INSERT", &ast) != PARSE_OK);
    ASSERT_TRUE(parse_sql("INSERT INTO", &ast) != PARSE_OK);
    ASSERT_TRUE(parse_sql("INSERT INTO users VALUES", &ast) != PARSE_OK);
    ASSERT_TRUE(parse_sql("INSERT INTO users VALUES (", &ast) != PARSE_OK);
    test_end();
}

static void test_parser_trailing(void) {
    AST ast;
    test_begin("parser trailing tokens");
    ASSERT_EQ_INT(parse_sql("SELECT * FROM users; garbage", &ast), PARSE_TRAILING_TOKENS);
    ASSERT_EQ_INT(parse_sql("INSERT INTO users VALUES (1, \"A\"); SELECT * FROM users;", &ast),
                  PARSE_TRAILING_TOKENS);
    ASSERT_EQ_INT(parse_sql("SELECT * FROM users; ;", &ast), PARSE_TRAILING_TOKENS);
    test_end();
}

static void test_parser_bounds_safe(void) {
    /* Empty token stream with only EOF */
    Token tokens[2];
    AST ast;
    int n = 0;
    test_begin("parser safe EOF handling");
    ASSERT_EQ_INT(tokenize("", tokens, 2, &n), TOKENIZE_OK);
    ASSERT_TRUE(parse(tokens, n, &ast) != PARSE_OK);
    ASSERT_TRUE(parse(NULL, 0, &ast) != PARSE_OK);
    test_end();
}

/* ---- Semantic ---- */

static void test_semantic_columns(void) {
    AST ast;
    test_begin("semantic column validation");
    ASSERT_EQ_INT(parse_sql("SELECT * FROM users;", &ast), PARSE_OK);
    ASSERT_EQ_INT(semantic_validate(&ast), SEMANTIC_OK);
    ASSERT_EQ_INT(parse_sql("SELECT id FROM users;", &ast), PARSE_OK);
    ASSERT_EQ_INT(semantic_validate(&ast), SEMANTIC_OK);
    ASSERT_EQ_INT(parse_sql("SELECT nonexistent FROM users;", &ast), PARSE_OK);
    ASSERT_EQ_INT(semantic_validate(&ast), SEMANTIC_UNKNOWN_COLUMN);
    ASSERT_EQ_INT(parse_sql("SELECT * FROM users WHERE nonexistent = 1;", &ast), PARSE_OK);
    ASSERT_EQ_INT(semantic_validate(&ast), SEMANTIC_UNKNOWN_COLUMN);
    ASSERT_EQ_INT(parse_sql("SELECT * FROM users WHERE id = \"abc\";", &ast), PARSE_OK);
    ASSERT_EQ_INT(semantic_validate(&ast), SEMANTIC_TYPE_MISMATCH);
    ASSERT_EQ_INT(parse_sql("SELECT * FROM users WHERE name = 1;", &ast), PARSE_OK);
    ASSERT_EQ_INT(semantic_validate(&ast), SEMANTIC_TYPE_MISMATCH);
    test_end();
}

static void test_util_parse_int(void) {
    int32_t v;
    test_begin("util parse int32");
    ASSERT_TRUE(util_parse_int32("0", &v));
    ASSERT_EQ_INT(v, 0);
    ASSERT_TRUE(util_parse_int32("42", &v));
    ASSERT_EQ_INT(v, 42);
    ASSERT_TRUE(!util_parse_int32("abc", &v));
    ASSERT_TRUE(!util_parse_int32("12abc", &v));
    ASSERT_TRUE(!util_parse_int32("999999999999999999999", &v));
    ASSERT_TRUE(!util_parse_int32("", &v));
    test_end();
}

/* ---- Storage ---- */

static void insert_n_rows(const char *table, int n) {
    Table *t = NULL;
    int i;
    ASSERT_EQ_INT(storage_open_table(table, &t), TABLE_OK);
    for (i = 1; i <= n; i++) {
        Row r;
        char name[32];
        memset(&r, 0, sizeof(r));
        r.id = (int32_t)i;
        snprintf(name, sizeof(name), "user%d", i);
        strncpy(r.name, name, STORAGE_NAME_SIZE - 1);
        ASSERT_EQ_INT(storage_insert_row(t, &r, NULL), TABLE_OK);
    }
    storage_close_table(t);
}

static void test_storage_page_boundaries(void) {
    size_t rpp = storage_rows_per_page();
    Row *rows = NULL;
    uint64_t count = 0;
    Table *t = NULL;
    int i;
    uint64_t seen_ids[400];
    int cases[] = {1, (int)rpp, (int)rpp + 1, (int)rpp * 2, (int)rpp * 2 + 7};
    int c;

    test_begin("storage page-aware scan");
    ASSERT_EQ_INT((int)rpp, 113);

    for (c = 0; c < (int)(sizeof(cases) / sizeof(cases[0])); c++) {
        int n = cases[c];
        reset_workdir();
        ASSERT_EQ_INT(storage_create_table("users"), TABLE_CREATE_OK);
        insert_n_rows("users", n);

        ASSERT_EQ_INT(storage_open_table("users", &t), TABLE_OK);
        ASSERT_EQ_INT(storage_select_all_rows(t, &rows, &count), TABLE_OK);
        ASSERT_EQ_INT((int)count, n);

        memset(seen_ids, 0, sizeof(seen_ids));
        for (i = 0; i < (int)count; i++) {
            ASSERT_TRUE(rows[i].id >= 1 && rows[i].id <= n);
            ASSERT_TRUE(seen_ids[rows[i].id] == 0);
            seen_ids[rows[i].id] = 1;
        }
        free(rows);
        rows = NULL;
        storage_close_table(t);
        t = NULL;
    }
    test_end();
}

static void test_storage_header_validation(void) {
    FILE *f;
    test_begin("storage header validation");
    reset_workdir();

    ASSERT_EQ_INT(storage_create_table("users"), TABLE_CREATE_OK);
    ASSERT_EQ_INT(storage_create_table("users"), TABLE_CREATE_ALREADY_EXISTS);

    /* Wrong magic */
    f = fopen("bad.tbl", "wb");
    ASSERT_TRUE(f != NULL);
    {
        unsigned char z[64];
        memset(z, 0, sizeof(z));
        z[0] = 'X';
        fwrite(z, 1, 64, f);
    }
    fclose(f);
    {
        Table *t = NULL;
        /* open by name "bad" */
        ASSERT_TRUE(storage_open_table("bad", &t) == TABLE_INCOMPATIBLE ||
                    storage_open_table("bad", &t) == TABLE_CORRUPT);
    }

    /* Truncated header */
    f = fopen("trunc.tbl", "wb");
    ASSERT_TRUE(f != NULL);
    fwrite("SQLT", 1, 4, f);
    fclose(f);
    {
        Table *t = NULL;
        TableStatus st = storage_open_table("trunc", &t);
        ASSERT_TRUE(st == TABLE_INCOMPATIBLE || st == TABLE_CORRUPT);
        ASSERT_TRUE(t == NULL);
    }

    /* Unsupported version */
    f = fopen("ver.tbl", "wb");
    ASSERT_TRUE(f != NULL);
    util_write_u32_le(f, STORAGE_TABLE_MAGIC);
    util_write_u32_le(f, 99);
    util_write_u32_le(f, STORAGE_HEADER_SIZE);
    util_write_u32_le(f, STORAGE_PAGE_SIZE);
    util_write_u32_le(f, STORAGE_ROW_SIZE);
    util_write_u32_le(f, 0);
    util_write_u64_le(f, 0);
    {
        unsigned char pad[32];
        memset(pad, 0, sizeof(pad));
        fwrite(pad, 1, 32, f);
    }
    fclose(f);
    {
        Table *t = NULL;
        ASSERT_EQ_INT(storage_open_table("ver", &t), TABLE_INCOMPATIBLE);
    }

    /* Impossible row count */
    f = fopen("cnt.tbl", "wb");
    ASSERT_TRUE(f != NULL);
    util_write_u32_le(f, STORAGE_TABLE_MAGIC);
    util_write_u32_le(f, STORAGE_TABLE_VERSION);
    util_write_u32_le(f, STORAGE_HEADER_SIZE);
    util_write_u32_le(f, STORAGE_PAGE_SIZE);
    util_write_u32_le(f, STORAGE_ROW_SIZE);
    util_write_u32_le(f, 0);
    util_write_u64_le(f, 1000000);
    {
        unsigned char pad[32];
        memset(pad, 0, sizeof(pad));
        fwrite(pad, 1, 32, f);
    }
    fclose(f);
    {
        Table *t = NULL;
        ASSERT_EQ_INT(storage_open_table("cnt", &t), TABLE_CORRUPT);
    }
    test_end();
}

/* ---- Indexing ---- */

static void test_index_duplicate_and_query(void) {
    AST ast;
    test_begin("index duplicate rejection and query equivalence");
    reset_workdir();
    ASSERT_EQ_INT(storage_create_table("users"), TABLE_CREATE_OK);

    ASSERT_EQ_INT(parse_sql("INSERT INTO users VALUES (1, \"A\");", &ast), PARSE_OK);
    ASSERT_EQ_INT(execute(&ast), EXEC_OK);
    ASSERT_EQ_INT(parse_sql("INSERT INTO users VALUES (1, \"B\");", &ast), PARSE_OK);
    ASSERT_EQ_INT(execute(&ast), EXEC_DUPLICATE_ID);

    /* Second row must not exist */
    {
        Table *t = NULL;
        Row *rows = NULL;
        uint64_t count = 0;
        ASSERT_EQ_INT(storage_open_table("users", &t), TABLE_OK);
        ASSERT_EQ_INT(storage_select_all_rows(t, &rows, &count), TABLE_OK);
        ASSERT_EQ_INT((int)count, 1);
        ASSERT_STREQ(rows[0].name, "A");
        free(rows);
        storage_close_table(t);
    }

    /* Insert more and compare index vs scan */
    ASSERT_EQ_INT(parse_sql("INSERT INTO users VALUES (2, \"B\");", &ast), PARSE_OK);
    ASSERT_EQ_INT(execute(&ast), EXEC_OK);
    ASSERT_EQ_INT(parse_sql("INSERT INTO users VALUES (3, \"C\");", &ast), PARSE_OK);
    ASSERT_EQ_INT(execute(&ast), EXEC_OK);

    {
        Table *t = NULL;
        Index *idx = NULL;
        int64_t off;
        Row r;
        ASSERT_EQ_INT(storage_open_table("users", &t), TABLE_OK);
        ASSERT_EQ_INT(index_load("users", &idx), INDEX_OK);
        ASSERT_EQ_INT(index_lookup(idx, 2, &off), INDEX_OK);
        ASSERT_EQ_INT(index_validate_lookup(t, 2, off, &r), INDEX_OK);
        ASSERT_STREQ(r.name, "B");
        index_free(idx);
        storage_close_table(t);
    }
    test_end();
}

static void test_index_rebuild_scenarios(void) {
    AST ast;
    Index *idx = NULL;
    Table *t = NULL;
    FILE *f;
    char path[64];

    test_begin("index missing/corrupt/stale recovery");
    reset_workdir();
    ASSERT_EQ_INT(storage_create_table("users"), TABLE_CREATE_OK);
    ASSERT_EQ_INT(parse_sql("INSERT INTO users VALUES (10, \"Ten\");", &ast), PARSE_OK);
    ASSERT_EQ_INT(execute(&ast), EXEC_OK);
    ASSERT_EQ_INT(parse_sql("INSERT INTO users VALUES (20, \"Twenty\");", &ast), PARSE_OK);
    ASSERT_EQ_INT(execute(&ast), EXEC_OK);

    /* Delete index */
    remove("users.idx");
    ASSERT_EQ_INT(index_load_or_rebuild("users", &idx), INDEX_OK);
    ASSERT_TRUE(idx != NULL);
    {
        int64_t off;
        ASSERT_EQ_INT(index_lookup(idx, 10, &off), INDEX_OK);
        ASSERT_EQ_INT(index_lookup(idx, 20, &off), INDEX_OK);
    }
    index_free(idx);
    idx = NULL;

    /* Truncate index */
    f = fopen("users.idx", "wb");
    ASSERT_TRUE(f != NULL);
    fwrite("xx", 1, 2, f);
    fclose(f);
    ASSERT_EQ_INT(index_load_or_rebuild("users", &idx), INDEX_OK);
    index_free(idx);
    idx = NULL;

    /* Corrupt offset in a valid-looking index: rebuild path via load_or_rebuild on corrupt */
    ASSERT_EQ_INT(index_rebuild("users"), INDEX_OK);
    ASSERT_EQ_INT(index_load("users", &idx), INDEX_OK);
    /* Manually rewrite with wrong offset for key 10 */
    {
        Index *bad = index_create("users");
        ASSERT_TRUE(bad != NULL);
        ASSERT_EQ_INT(index_insert(bad, 10, 999999, 1), INDEX_OK);
        ASSERT_EQ_INT(index_insert(bad, 20, 0, 1), INDEX_OK); /* possibly wrong */
        ASSERT_EQ_INT(index_persist(bad), INDEX_OK);
        index_free(bad);
    }
    index_free(idx);
    idx = NULL;

    ASSERT_EQ_INT(storage_open_table("users", &t), TABLE_OK);
    ASSERT_EQ_INT(index_load("users", &idx), INDEX_OK);
    {
        int64_t off;
        Row r;
        ASSERT_EQ_INT(index_lookup(idx, 10, &off), INDEX_OK);
        /* Validation should fail for bogus offset */
        ASSERT_TRUE(index_validate_lookup(t, 10, off, &r) != INDEX_OK);
        /* Scan still finds the row */
        {
            ScanStatus st = storage_find_id(t, 10, &r, NULL);
            ASSERT_EQ_INT(st, SCAN_OK);
            ASSERT_STREQ(r.name, "Ten");
        }
    }
    index_free(idx);
    storage_close_table(t);

    /* Stale index: append row via storage without updating index */
    ASSERT_EQ_INT(storage_open_table("users", &t), TABLE_OK);
    {
        Row r;
        memset(&r, 0, sizeof(r));
        r.id = 30;
        strcpy(r.name, "Thirty");
        ASSERT_EQ_INT(storage_insert_row(t, &r, NULL), TABLE_OK);
    }
    storage_close_table(t);

    /* Query by id uses rebuild/load and falls back to scan if needed */
    ASSERT_EQ_INT(parse_sql("SELECT * FROM users WHERE id = 30;", &ast), PARSE_OK);
    ASSERT_EQ_INT(execute(&ast), EXEC_OK);

    (void)path;
    test_end();
}

static void test_index_header_formats(void) {
    FILE *f;
    Index *idx = NULL;
    test_begin("index header validation");
    reset_workdir();
    ASSERT_EQ_INT(storage_create_table("users"), TABLE_CREATE_OK);

    f = fopen("users.idx", "wb");
    ASSERT_TRUE(f != NULL);
    util_write_u32_le(f, 0xDEADBEEF);
    util_write_u32_le(f, 1);
    util_write_u32_le(f, 32);
    util_write_u32_le(f, 12);
    util_write_u32_le(f, 0);
    util_write_u32_le(f, 0);
    {
        unsigned char pad[8];
        memset(pad, 0, sizeof(pad));
        fwrite(pad, 1, 8, f);
    }
    fclose(f);
    ASSERT_EQ_INT(index_load("users", &idx), INDEX_INCOMPATIBLE);

    f = fopen("users.idx", "wb");
    ASSERT_TRUE(f != NULL);
    util_write_u32_le(f, INDEX_MAGIC);
    util_write_u32_le(f, 99);
    util_write_u32_le(f, INDEX_HEADER_SIZE);
    util_write_u32_le(f, INDEX_ENTRY_ONDISK);
    util_write_u32_le(f, 0);
    util_write_u32_le(f, 0);
    {
        unsigned char pad[8];
        memset(pad, 0, sizeof(pad));
        fwrite(pad, 1, 8, f);
    }
    fclose(f);
    ASSERT_EQ_INT(index_load("users", &idx), INDEX_INCOMPATIBLE);

    /* Truncated entries: full header pad, claim 5 entries, write none */
    f = fopen("users.idx", "wb");
    ASSERT_TRUE(f != NULL);
    util_write_u32_le(f, INDEX_MAGIC);
    util_write_u32_le(f, INDEX_VERSION);
    util_write_u32_le(f, INDEX_HEADER_SIZE);
    util_write_u32_le(f, INDEX_ENTRY_ONDISK);
    util_write_u32_le(f, 5);
    util_write_u32_le(f, 0);
    {
        unsigned char pad[8];
        memset(pad, 0, sizeof(pad));
        fwrite(pad, 1, 8, f);
    }
    /* no entries written */
    fclose(f);
    ASSERT_EQ_INT(index_load("users", &idx), INDEX_CORRUPT);
    test_end();
}

/* ---- CLI helpers ---- */

static void test_cli_word_boundary(void) {
    /* SELECTOR should not be SQL; use cli_handle_line which routes meta as unknown */
    test_begin("CLI SQL prefix word boundary");
    reset_workdir();
    /* These should not crash; they are meta/unknown, not parse as SELECT */
    ASSERT_EQ_INT(cli_handle_line("SELECTOR"), 0);
    ASSERT_EQ_INT(cli_handle_line("INSERTION"), 0);
    test_end();
}

static void test_cli_oversized_input(void) {
    char path[256];
    FILE *f;
    char buf[CLI_MAX_INPUT];
    int rc;
    size_t i;

    test_begin("CLI oversized input");
    reset_workdir();
    snprintf(path, sizeof(path), "input.txt");
    f = fopen(path, "w");
    ASSERT_TRUE(f != NULL);

    /* Exactly within limit: CLI_MAX_INPUT-1 chars + newline fits in buffer of CLI_MAX_INPUT
     * fgets reads at most buflen-1 chars. A line of (CLI_MAX_INPUT-2) payload + \n is OK. */
    for (i = 0; i < (size_t)(CLI_MAX_INPUT - 2); i++) {
        fputc('a', f);
    }
    fputc('\n', f);

    /* One beyond: CLI_MAX_INPUT-1 chars without newline then more */
    for (i = 0; i < (size_t)CLI_MAX_INPUT + 10; i++) {
        fputc('b', f);
    }
    fputc('\n', f);

    /* Valid next command */
    fputs("help\n", f);
    fclose(f);

    f = fopen(path, "r");
    ASSERT_TRUE(f != NULL);

    rc = cli_read_line(f, buf, sizeof(buf));
    ASSERT_EQ_INT(rc, 0);
    ASSERT_EQ_INT((int)strlen(buf), CLI_MAX_INPUT - 2);

    rc = cli_read_line(f, buf, sizeof(buf));
    ASSERT_EQ_INT(rc, -2);

    rc = cli_read_line(f, buf, sizeof(buf));
    ASSERT_EQ_INT(rc, 0);
    ASSERT_STREQ(buf, "help");

    fclose(f);
    test_end();
}

static void test_case_insensitive_sql_exec(void) {
    AST ast;
    Table *t = NULL;
    Row *rows = NULL;
    uint64_t count = 0;

    test_begin("case-insensitive SQL execution");
    reset_workdir();
    ASSERT_EQ_INT(storage_create_table("users"), TABLE_CREATE_OK);
    ASSERT_EQ_INT(parse_sql("insert into users values (1, \"A\");", &ast), PARSE_OK);
    ASSERT_EQ_INT(execute(&ast), EXEC_OK);
    ASSERT_EQ_INT(parse_sql("SELECT * FROM users;", &ast), PARSE_OK);
    ASSERT_EQ_INT(execute(&ast), EXEC_OK);
    ASSERT_EQ_INT(parse_sql("Select * From users Where id = 1;", &ast), PARSE_OK);
    ASSERT_EQ_INT(execute(&ast), EXEC_OK);

    ASSERT_EQ_INT(storage_open_table("users", &t), TABLE_OK);
    ASSERT_EQ_INT(storage_select_all_rows(t, &rows, &count), TABLE_OK);
    ASSERT_EQ_INT((int)count, 1);
    free(rows);
    storage_close_table(t);
    test_end();
}

static void test_invalid_insert_numbers(void) {
    AST ast;
    test_begin("reject invalid integer literals");
    reset_workdir();
    ASSERT_EQ_INT(storage_create_table("users"), TABLE_CREATE_OK);
    /* String as id rejected at parse (expects NUMBER) */
    ASSERT_TRUE(parse_sql("INSERT INTO users VALUES (\"abc\", \"A\");", &ast) != PARSE_OK);
    /* Overflow number tokenizes if fits token length but parse fails */
    ASSERT_TRUE(parse_sql("INSERT INTO users VALUES (999999999999999999999, \"A\");", &ast) != PARSE_OK);
    test_end();
}

static void test_layout_helpers(void) {
    uint64_t off0, off112, off113, rn;
    test_begin("storage layout helpers");
    ASSERT_EQ_INT((int)storage_row_size(), 36);
    ASSERT_EQ_INT((int)storage_page_size(), 4096);
    ASSERT_EQ_INT((int)storage_rows_per_page(), 113);
    ASSERT_TRUE(storage_offset_for_row(0, &off0));
    ASSERT_EQ_INT((int)off0, (int)STORAGE_HEADER_SIZE);
    ASSERT_TRUE(storage_offset_for_row(112, &off112));
    ASSERT_TRUE(storage_offset_for_row(113, &off113));
    ASSERT_EQ_INT((int)(off113 - STORAGE_HEADER_SIZE), (int)STORAGE_PAGE_SIZE);
    ASSERT_TRUE(storage_row_number_for_offset(off113, &rn));
    ASSERT_EQ_INT((int)rn, 113);
    /* Padding offset mid-page gap after 113 rows should be invalid */
    ASSERT_TRUE(!storage_row_number_for_offset(STORAGE_HEADER_SIZE + 113 * 36, &rn));
    test_end();
}

static void test_secure_files_and_atomic_creation(void) {
    struct stat st;
    FILE *victim;
    char contents[16] = {0};
    Index *index;
    Table *table = NULL;
    Row row;

    test_begin("secure temp files and atomic table creation");
    reset_workdir();
    ASSERT_EQ_INT(storage_create_table("users"), TABLE_CREATE_OK);
    ASSERT_EQ_INT(stat("users.tbl", &st), 0);
    ASSERT_EQ_INT((int)(st.st_mode & 0777), 0600);

    memset(&row, 0, sizeof(row));
    row.id = 1;
    strcpy(row.name, "original");
    ASSERT_EQ_INT(storage_open_table("users", &table), TABLE_OK);
    ASSERT_EQ_INT(storage_insert_row(table, &row, NULL), TABLE_OK);
    storage_close_table(table);
    ASSERT_EQ_INT(storage_create_table("users"), TABLE_CREATE_ALREADY_EXISTS);
    ASSERT_EQ_INT((int)table_row_count("users"), 1);

    victim = fopen("victim", "wb");
    ASSERT_TRUE(victim != NULL);
    ASSERT_EQ_INT((int)fwrite("UNCHANGED", 1, 9, victim), 9);
    fclose(victim);
    ASSERT_EQ_INT(symlink("victim", "users.idx.tmp"), 0);

    ASSERT_EQ_INT(index_rebuild("users"), INDEX_OK);
    victim = fopen("victim", "rb");
    ASSERT_TRUE(victim != NULL);
    ASSERT_EQ_INT((int)fread(contents, 1, sizeof(contents) - 1, victim), 9);
    fclose(victim);
    ASSERT_STREQ(contents, "UNCHANGED");
    ASSERT_EQ_INT(lstat("users.idx.tmp", &st), 0);
    ASSERT_TRUE(S_ISLNK(st.st_mode));
    ASSERT_EQ_INT(stat("users.idx", &st), 0);
    ASSERT_EQ_INT((int)(st.st_mode & 0777), 0600);

    index = index_create("users");
    ASSERT_TRUE(index != NULL);
    ASSERT_EQ_INT(index_insert(index, 1, (int64_t)STORAGE_HEADER_SIZE, 0), INDEX_OK);
    index_set_fault_point(INDEX_FAULT_DURING_TEMP_WRITE);
    ASSERT_TRUE(index_persist(index) != INDEX_OK);
    ASSERT_EQ_INT(count_secure_temp_files(), 0);
    index_set_fault_point(INDEX_FAULT_BEFORE_RENAME);
    ASSERT_TRUE(index_persist(index) != INDEX_OK);
    ASSERT_EQ_INT(count_secure_temp_files(), 0);
    index_set_fault_point(INDEX_FAULT_AFTER_RENAME_BEFORE_DIR_SYNC);
    ASSERT_TRUE(index_persist(index) != INDEX_OK);
    {
        Index *loaded = NULL;
        ASSERT_EQ_INT(index_load("users", &loaded), INDEX_OK);
        ASSERT_TRUE(loaded != NULL);
        index_free(loaded);
    }
    index_free(index);
    test_end();
}

static void test_checked_sql_lengths(void) {
    char valid_name[SQL_TABLE_NAME_CAPACITY];
    char invalid_name[SQL_TABLE_NAME_CAPACITY + 1u];
    char valid_value[SQL_STORED_NAME_CAPACITY];
    char invalid_value[SQL_STORED_NAME_CAPACITY + 1u];
    char sql[256];
    AST ast;
    Table *table = NULL;
    Row *rows = NULL;
    uint64_t count = 0;
    struct stat index_before;
    struct stat index_after;

    test_begin("checked SQL identifier and value lengths");
    reset_workdir();
    memset(valid_name, 'a', SQL_MAX_TABLE_NAME_LENGTH);
    valid_name[SQL_MAX_TABLE_NAME_LENGTH] = '\0';
    memset(invalid_name, 'a', SQL_TABLE_NAME_CAPACITY);
    invalid_name[SQL_TABLE_NAME_CAPACITY] = '\0';
    memset(valid_value, 'v', SQL_MAX_STORED_NAME_LENGTH);
    valid_value[SQL_MAX_STORED_NAME_LENGTH] = '\0';
    memset(invalid_value, 'v', SQL_STORED_NAME_CAPACITY);
    invalid_value[SQL_STORED_NAME_CAPACITY] = '\0';

    ASSERT_EQ_INT(storage_create_table(valid_name), TABLE_CREATE_OK);
    ASSERT_EQ_INT(storage_create_table(invalid_name), TABLE_CREATE_NAME_TOO_LONG);
    ASSERT_EQ_INT(index_rebuild(valid_name), INDEX_OK);
    {
        char index_path[96];
        ASSERT_TRUE(storage_index_path(valid_name, index_path, sizeof(index_path)));
        ASSERT_EQ_INT(stat(index_path, &index_before), 0);
    }
    snprintf(sql, sizeof(sql), "SELECT * FROM %s;", valid_name);
    ASSERT_EQ_INT(parse_sql(sql, &ast), PARSE_OK);
    snprintf(sql, sizeof(sql), "SELECT * FROM %s;", invalid_name);
    ASSERT_EQ_INT(parse_sql(sql, &ast), PARSE_TABLE_NAME_TOO_LONG);

    snprintf(sql, sizeof(sql), "INSERT INTO %s VALUES (1, \"%s\");",
             valid_name, invalid_value);
    ASSERT_EQ_INT(parse_sql(sql, &ast), PARSE_STRING_TOO_LONG);
    ASSERT_EQ_INT((int)table_row_count(valid_name), 0);

    snprintf(sql, sizeof(sql), "INSERT INTO %s VALUES (1, \"short\");", invalid_name);
    ASSERT_EQ_INT(parse_sql(sql, &ast), PARSE_TABLE_NAME_TOO_LONG);
    ASSERT_EQ_INT((int)table_row_count(valid_name), 0);
    {
        char index_path[96];
        ASSERT_TRUE(storage_index_path(valid_name, index_path, sizeof(index_path)));
        ASSERT_EQ_INT(stat(index_path, &index_after), 0);
        ASSERT_EQ_INT((int)index_before.st_size, (int)index_after.st_size);
    }

    snprintf(sql, sizeof(sql), "INSERT INTO %s VALUES (1, \"%s\");",
             valid_name, valid_value);
    ASSERT_EQ_INT(parse_sql(sql, &ast), PARSE_OK);
    ASSERT_EQ_INT(execute(&ast), EXEC_OK);
    ASSERT_EQ_INT(storage_open_table_readonly(valid_name, &table), TABLE_OK);
    ASSERT_EQ_INT(storage_select_all_rows(table, &rows, &count), TABLE_OK);
    ASSERT_EQ_INT((int)count, 1);
    ASSERT_EQ_INT((int)strlen(rows[0].name), SQL_MAX_STORED_NAME_LENGTH);
    ASSERT_STREQ(rows[0].name, valid_value);
    free(rows);
    storage_close_table(table);
    test_end();
}

static void test_dynamic_hash_and_index_bounds(void) {
    Index *index;
    Table *table = NULL;
    Row row;
    int i;
    int64_t offset;
    FILE *f;

    test_begin("dynamic hash and authoritative index bounds");
    reset_workdir();
    ASSERT_EQ_INT(storage_create_table("users"), TABLE_CREATE_OK);
    index = index_create("users");
    ASSERT_TRUE(index != NULL);
    for (i = 0; i < 2000; i++) {
        ASSERT_EQ_INT(index_insert(index, (int32_t)(i * 256), (int64_t)i, 0), INDEX_OK);
    }
    ASSERT_TRUE(index_bucket_count(index) > INDEX_INITIAL_BUCKETS);
    for (i = 0; i < 2000; i++) {
        ASSERT_EQ_INT(index_lookup(index, (int32_t)(i * 256), &offset), INDEX_OK);
        ASSERT_EQ_INT((int)offset, i);
    }
    index_free(index);

    ASSERT_EQ_INT(storage_open_table("users", &table), TABLE_OK);
    for (i = 0; i < 128; i++) {
        memset(&row, 0, sizeof(row));
        row.id = (int32_t)(i * 256);
        snprintf(row.name, sizeof(row.name), "row%d", i);
        ASSERT_EQ_INT(storage_insert_row(table, &row, NULL), TABLE_OK);
    }
    storage_close_table(table);
    ASSERT_EQ_INT(index_rebuild("users"), INDEX_OK);
    ASSERT_EQ_INT(index_load("users", &index), INDEX_OK);
    ASSERT_TRUE(index_bucket_count(index) > INDEX_INITIAL_BUCKETS);
    for (i = 0; i < 128; i++) {
        ASSERT_EQ_INT(index_lookup(index, (int32_t)(i * 256), &offset), INDEX_OK);
    }
    index_free(index);

    f = fopen("users.idx", "wb");
    ASSERT_TRUE(f != NULL);
    util_write_u32_le(f, INDEX_MAGIC);
    util_write_u32_le(f, INDEX_VERSION);
    util_write_u32_le(f, INDEX_HEADER_SIZE);
    util_write_u32_le(f, INDEX_ENTRY_ONDISK);
    util_write_u32_le(f, 129);
    util_write_u32_le(f, 0);
    {
        unsigned char pad[8] = {0};
        fwrite(pad, 1, sizeof(pad), f);
    }
    fclose(f);
    ASSERT_EQ_INT(index_load("users", &index), INDEX_CORRUPT);

    f = fopen("users.idx", "wb");
    ASSERT_TRUE(f != NULL);
    util_write_u32_le(f, INDEX_MAGIC);
    util_write_u32_le(f, INDEX_VERSION);
    util_write_u32_le(f, INDEX_HEADER_SIZE);
    util_write_u32_le(f, INDEX_ENTRY_ONDISK);
    util_write_u32_le(f, UINT32_MAX);
    util_write_u32_le(f, 0);
    {
        unsigned char pad[8] = {0};
        fwrite(pad, 1, sizeof(pad), f);
    }
    fclose(f);
    ASSERT_EQ_INT(index_load("users", &index), INDEX_CORRUPT);
    test_end();
}

static void test_duplicate_rebuild_detection(void) {
    Table *table = NULL;
    Row row;
    struct stat before;
    struct stat after;
    Index *index = NULL;
    AST ast;
    char output[512];

    test_begin("duplicate primary key detection during rebuild");
    reset_workdir();
    ASSERT_EQ_INT(storage_create_table("users"), TABLE_CREATE_OK);
    memset(&row, 0, sizeof(row));
    row.id = 7;
    strcpy(row.name, "first");
    ASSERT_EQ_INT(storage_open_table("users", &table), TABLE_OK);
    ASSERT_EQ_INT(storage_insert_row(table, &row, NULL), TABLE_OK);
    storage_close_table(table);
    ASSERT_EQ_INT(index_rebuild("users"), INDEX_OK);
    ASSERT_EQ_INT(stat("users.idx", &before), 0);

    strcpy(row.name, "second");
    ASSERT_EQ_INT(storage_open_table("users", &table), TABLE_OK);
    ASSERT_EQ_INT(storage_insert_row(table, &row, NULL), TABLE_OK);
    storage_close_table(table);
    ASSERT_EQ_INT(index_rebuild("users"), INDEX_DUPLICATE_PRIMARY_KEY);
    ASSERT_EQ_INT(stat("users.idx", &after), 0);
    ASSERT_EQ_INT((int)before.st_size, (int)after.st_size);
    ASSERT_EQ_INT(index_load("users", &index), INDEX_CORRUPT);
    ASSERT_TRUE(index == NULL);
    ASSERT_EQ_INT(parse_sql("SELECT * FROM users WHERE id = 7;", &ast), PARSE_OK);
    ASSERT_EQ_INT(capture_execute(&ast, output, sizeof(output)), EXEC_TABLE_CORRUPT);
    ASSERT_TRUE(strstr(output, "duplicate primary keys") != NULL);
    test_end();
}

static void test_insert_crash_recovery(void) {
    const StorageFaultPoint points[] = {
        STORAGE_FAULT_BEFORE_ROW_WRITE,
        STORAGE_FAULT_DURING_ROW_WRITE,
        STORAGE_FAULT_AFTER_ROW_WRITE_BEFORE_SYNC,
        STORAGE_FAULT_BEFORE_METADATA_UPDATE,
        STORAGE_FAULT_DURING_METADATA_UPDATE
    };
    size_t i;

    test_begin("insert rollback journal fault recovery");
    for (i = 0; i < sizeof(points) / sizeof(points[0]); i++) {
        Table *table = NULL;
        Row row;
        char journal_path[128];
        reset_workdir();
        ASSERT_EQ_INT(storage_create_table("users"), TABLE_CREATE_OK);
        memset(&row, 0, sizeof(row));
        row.id = 9;
        strcpy(row.name, "journaled");
        ASSERT_EQ_INT(storage_open_table("users", &table), TABLE_OK);
        storage_set_fault_point(points[i]);
        ASSERT_TRUE(storage_insert_row(table, &row, NULL) != TABLE_OK);
        storage_close_table(table);
        ASSERT_TRUE(storage_journal_path("users", journal_path, sizeof(journal_path)));
        ASSERT_EQ_INT(access(journal_path, F_OK), 0);

        ASSERT_EQ_INT(storage_open_table("users", &table), TABLE_OK);
        ASSERT_EQ_INT((int)table->row_count, 0);
        storage_close_table(table);
        ASSERT_TRUE(access(journal_path, F_OK) != 0 && errno == ENOENT);
        ASSERT_EQ_INT((int)table_row_count("users"), 0);
    }
    test_end();
}

static void test_terminal_escaping(void) {
    unsigned char field[STORAGE_NAME_SIZE];
    FILE *capture;
    char output[256];
    size_t amount;
    Table *table = NULL;
    Row row;
    AST ast;
    char query_output[1024];

    test_begin("terminal-safe stored value escaping");
    reset_workdir();
    memset(field, 0, sizeof(field));
    field[0] = 'A';
    field[1] = 0x1Bu;
    memcpy(field + 2, "[31m", 4);
    field[6] = '\n';
    field[7] = '\r';
    field[8] = '\t';
    field[9] = 0x7Fu;
    memcpy(field + 10, "%s%n", 4);
    field[14] = 0;
    field[15] = 'X';
    capture = tmpfile();
    ASSERT_TRUE(capture != NULL);
    ASSERT_TRUE(util_print_escaped_field(capture, field, sizeof(field)));
    rewind(capture);
    amount = fread(output, 1, sizeof(output) - 1, capture);
    output[amount] = '\0';
    fclose(capture);
    ASSERT_TRUE(strchr(output, '\x1b') == NULL);
    ASSERT_TRUE(strstr(output, "\\x1B[31m\\n\\r\\t\\x7F%s%n\\x00X") != NULL);

    ASSERT_EQ_INT(storage_create_table("users"), TABLE_CREATE_OK);
    memset(&row, 0, sizeof(row));
    row.id = 1;
    memcpy(row.name, field, sizeof(row.name));
    ASSERT_EQ_INT(storage_open_table("users", &table), TABLE_OK);
    ASSERT_EQ_INT(storage_insert_row(table, &row, NULL), TABLE_OK);
    storage_close_table(table);
    ASSERT_EQ_INT(parse_sql("SELECT * FROM users;", &ast), PARSE_OK);
    ASSERT_EQ_INT(capture_execute(&ast, query_output, sizeof(query_output)), EXEC_OK);
    ASSERT_TRUE(strchr(query_output, '\x1b') == NULL);
    ASSERT_TRUE(strstr(query_output, "\\x1B") != NULL);
    test_end();
}

static void test_read_only_select_and_index_fallback(void) {
    AST ast;
    char output[1024];

    test_begin("read-only SELECT and missing-index fallback");
    reset_workdir();
    ASSERT_EQ_INT(storage_create_table("users"), TABLE_CREATE_OK);
    ASSERT_EQ_INT(parse_sql("INSERT INTO users VALUES (1, \"readable\");", &ast), PARSE_OK);
    ASSERT_EQ_INT(execute(&ast), EXEC_OK);
    ASSERT_EQ_INT(unlink("users.idx"), 0);
    ASSERT_EQ_INT(chmod("users.tbl", 0400), 0);
    ASSERT_EQ_INT(parse_sql("SELECT * FROM users WHERE id = 1;", &ast), PARSE_OK);
    ASSERT_EQ_INT(capture_execute(&ast, output, sizeof(output)), EXEC_OK);
    ASSERT_TRUE(strstr(output, "readable") != NULL);
    ASSERT_EQ_INT(parse_sql("INSERT INTO users VALUES (2, \"blocked\");", &ast), PARSE_OK);
    ASSERT_EQ_INT(capture_execute(&ast, output, sizeof(output)), EXEC_READ_ONLY);

    ASSERT_TRUE(unlink("users.idx") == 0 || errno == ENOENT);
    ASSERT_EQ_INT(chmod(".", 0500), 0);
    ASSERT_EQ_INT(parse_sql("SELECT * FROM users WHERE id = 1;", &ast), PARSE_OK);
    ASSERT_EQ_INT(capture_execute(&ast, output, sizeof(output)), EXEC_OK);
    ASSERT_TRUE(strstr(output, "readable") != NULL);
    ASSERT_EQ_INT(chmod(".", 0700), 0);
    ASSERT_EQ_INT(chmod("users.tbl", 0600), 0);
    test_end();
}

static void test_signed_int32_ids(void) {
    Token tokens[32];
    int token_count = 0;
    AST ast;
    char output[1024];

    test_begin("signed int32 IDs and boundaries");
    reset_workdir();
    ASSERT_EQ_INT(tokenize("INSERT INTO users VALUES (-1, \"negative\");",
                           tokens, 32, &token_count), TOKENIZE_OK);
    ASSERT_STREQ(tokens[5].value, "-1");
    ASSERT_EQ_INT(storage_create_table("users"), TABLE_CREATE_OK);
    ASSERT_EQ_INT(parse(tokens, token_count, &ast), PARSE_OK);
    ASSERT_EQ_INT(execute(&ast), EXEC_OK);
    ASSERT_EQ_INT(parse_sql("INSERT INTO users VALUES (-2147483648, \"min\");", &ast), PARSE_OK);
    ASSERT_EQ_INT(execute(&ast), EXEC_OK);
    ASSERT_EQ_INT(parse_sql("INSERT INTO users VALUES (2147483647, \"max\");", &ast), PARSE_OK);
    ASSERT_EQ_INT(execute(&ast), EXEC_OK);
    ASSERT_EQ_INT(parse_sql("SELECT * FROM users WHERE id = -1;", &ast), PARSE_OK);
    ASSERT_EQ_INT(capture_execute(&ast, output, sizeof(output)), EXEC_OK);
    ASSERT_TRUE(strstr(output, "negative") != NULL);
    ASSERT_EQ_INT(parse_sql("INSERT INTO users VALUES (-2147483649, \"bad\");", &ast),
                  PARSE_INTEGER_OUT_OF_RANGE);
    ASSERT_EQ_INT(parse_sql("INSERT INTO users VALUES (2147483648, \"bad\");", &ast),
                  PARSE_INTEGER_OUT_OF_RANGE);
    ASSERT_TRUE(tokenize("SELECT * FROM users WHERE id = -;", tokens, 32, &token_count) != TOKENIZE_OK);
    ASSERT_TRUE(tokenize("SELECT * FROM users WHERE id = --1;", tokens, 32, &token_count) != TOKENIZE_OK);
    ASSERT_TRUE(tokenize("SELECT * FROM users WHERE id = -abc;", tokens, 32, &token_count) != TOKENIZE_OK);
    ASSERT_TRUE(parse_sql("SELECT * FROM users WHERE id = 1-2;", &ast) != PARSE_OK);
    test_end();
}

static void test_parser_truncated_arrays(void) {
    static const char *statements[] = {
        "SELECT", "SELECT *", "SELECT * FROM", "SELECT * FROM users WHERE",
        "SELECT * FROM users WHERE id", "SELECT * FROM users WHERE id =",
        "INSERT", "INSERT INTO", "INSERT INTO users", "INSERT INTO users VALUES",
        "INSERT INTO users VALUES (", "INSERT INTO users VALUES (1",
        "INSERT INTO users VALUES (1,"
    };
    Token no_eof[3] = {
        {TOKEN_SELECT, "SELECT"},
        {TOKEN_STAR, "*"},
        {TOKEN_FROM, "FROM"}
    };
    Parser parser;
    AST ast;
    size_t i;

    test_begin("parser truncated arrays and missing EOF");
    for (i = 0; i < sizeof(statements) / sizeof(statements[0]); i++) {
        ASSERT_TRUE(parse_sql(statements[i], &ast) != PARSE_OK);
    }
    ASSERT_EQ_INT(parse(no_eof, 0, &ast), PARSE_NULL_INPUT);
    ASSERT_TRUE(parse(no_eof, 1, &ast) != PARSE_OK);
    ASSERT_TRUE(parse(no_eof, 3, &ast) != PARSE_OK);
    ASSERT_TRUE(parse(no_eof, 2, &ast) != PARSE_OK);
    parser_init(&parser, no_eof, 2);
    parser.pos = 3;
    ASSERT_EQ_INT(parser_peek(&parser)->type, TOKEN_EOF);
    ASSERT_EQ_INT(parser_previous(&parser)->type, TOKEN_EOF);
    test_end();
}

int main(void) {
    char template[] = "/tmp/sqlengine_test_XXXXXX";
    char *dir;

    setvbuf(stdout, NULL, _IOLBF, 0);
    setvbuf(stderr, NULL, _IOLBF, 0);

    if (getcwd(g_home, sizeof(g_home)) == NULL) {
        perror("getcwd");
        return 1;
    }

    dir = mkdtemp(template);
    if (dir == NULL) {
        perror("mkdtemp");
        return 1;
    }
    strncpy(g_tmpdir, dir, sizeof(g_tmpdir) - 1);

    test_layout_helpers();
    test_util_parse_int();

    test_tokenize_valid_select();
    test_tokenize_valid_insert();
    test_tokenize_mixed_case();
    test_tokenize_invalid_char();
    test_tokenize_long_token();
    test_tokenize_long_number_string();
    test_tokenize_unterminated();
    test_tokenize_capacity();

    test_parser_valid();
    test_parser_incomplete();
    test_parser_trailing();
    test_parser_bounds_safe();

    test_semantic_columns();

    test_storage_page_boundaries();
    test_storage_header_validation();

    test_index_duplicate_and_query();
    test_index_rebuild_scenarios();
    test_index_header_formats();

    test_cli_word_boundary();
    test_cli_oversized_input();
    test_case_insensitive_sql_exec();
    test_invalid_insert_numbers();

    test_secure_files_and_atomic_creation();
    test_checked_sql_lengths();
    test_dynamic_hash_and_index_bounds();
    test_duplicate_rebuild_detection();
    test_insert_crash_recovery();
    test_terminal_escaping();
    test_read_only_select_and_index_fallback();
    test_signed_int32_ids();
    test_parser_truncated_arrays();

    chdir(g_home);
    {
        char cmd[640];
        snprintf(cmd, sizeof(cmd), "rm -rf '%s'", g_tmpdir);
        system(cmd);
    }

    printf("\n%d tests completed, %d failure(s)\n", g_passed + g_failures, g_failures);
    return g_failures ? 1 : 0;
}
