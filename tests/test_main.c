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

    chdir(g_home);
    {
        char cmd[640];
        snprintf(cmd, sizeof(cmd), "rm -rf '%s'", g_tmpdir);
        system(cmd);
    }

    printf("\n%d tests completed, %d failure(s)\n", g_passed + g_failures, g_failures);
    return g_failures ? 1 : 0;
}
