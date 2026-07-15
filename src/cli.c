#include "cli.h"
#include "executor.h"
#include "index.h"
#include "parser.h"
#include "status.h"
#include "storage.h"
#include "tokenizer.h"
#include "util.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define MAX_TOKENS 64

static int is_sql_statement(const char *input) {
    while (isspace((unsigned char)*input)) {
        input++;
    }

    /* Require word boundary so SELECTOR / INSERTION are not treated as SQL. */
    if (util_strncasecmp(input, "SELECT", 6) == 0 && util_is_word_boundary(input[6])) {
        return 1;
    }
    if (util_strncasecmp(input, "INSERT", 6) == 0 && util_is_word_boundary(input[6])) {
        return 1;
    }
    return 0;
}

static void handle_meta_command(const char *input) {
    char cmd[64] = {0};
    char arg[64] = {0};
    size_t cmd_len;
    size_t arg_len;

    sscanf(input, "%63s %63s", cmd, arg);

    cmd_len = strlen(cmd);
    if (cmd_len > 0 && cmd[cmd_len - 1] == ';') {
        cmd[cmd_len - 1] = '\0';
    }
    arg_len = strlen(arg);
    if (arg_len > 0 && arg[arg_len - 1] == ';') {
        arg[arg_len - 1] = '\0';
    }

    if (strcmp(cmd, "create_table") == 0) {
        TableCreateStatus st;
        if (arg[0] == '\0') {
            printf("Usage: create_table <table_name>\n");
            return;
        }
        st = storage_create_table(arg);
        if (st == TABLE_CREATE_OK) {
            printf("Table '%s' created successfully.\n", arg);
        } else if (st == TABLE_CREATE_ALREADY_EXISTS) {
            printf("Table '%s' already exists.\n", arg);
        } else if (st == TABLE_CREATE_INVALID_NAME) {
            printf("Error: Invalid table name '%s'.\n", arg);
        } else if (st == TABLE_CREATE_NAME_TOO_LONG) {
            printf("Error: Table name '%s' is too long (maximum %u bytes).\n",
                   arg, (unsigned int)SQL_MAX_TABLE_NAME_LENGTH);
        } else {
            printf("Error: Could not create table '%s' (%s).\n",
                   arg, table_create_status_string(st));
        }
    } else if (strcmp(cmd, "rebuild_index") == 0) {
        IndexStatus st;
        if (arg[0] == '\0') {
            printf("Usage: rebuild_index <table_name>\n");
            return;
        }
        st = index_rebuild(arg);
        if (st == INDEX_OK) {
            printf("Index for '%s' rebuilt successfully.\n", arg);
        } else {
            printf("Error: Could not rebuild index for '%s' (%s).\n",
                   arg, index_status_string(st));
        }
    } else if (strcmp(cmd, "help") == 0) {
        printf("SQL Engine CLI\n");
        printf("\nSQL (keywords are case-insensitive):\n");
        printf("  SELECT * FROM <table>;\n");
        printf("  SELECT id, name FROM <table> WHERE id = <n>;\n");
        printf("  SELECT * FROM <table> WHERE name = \"value\";\n");
        printf("  INSERT INTO <table> VALUES (<id>, \"name\");\n");
        printf("\nMeta commands:\n");
        printf("  create_table <name>    Create a new table\n");
        printf("  rebuild_index <name>   Rebuild index from table data\n");
        printf("  help                   Show this help\n");
        printf("  exit, quit             Exit the CLI\n");
        printf("\nSchema: columns id (unique int32 primary key), name (string, max 31 chars).\n");
    } else if (strcmp(cmd, "exit") == 0 || strcmp(cmd, "quit") == 0) {
        exit(0);
    } else {
        printf("Unknown command: %s\n", cmd);
        printf("Type 'help' for available commands.\n");
    }
}

static int handle_sql_input(const char *input) {
    Token token_storage[MAX_TOKENS];
    TokenBuffer tokens = { NULL, 0 };
    TokenizeStatus tstat;
    ParseStatus pstat;
    AST ast;
    ExecStatus estat;

    tstat = tokenize(input, token_storage, MAX_TOKENS, &tokens);
    if (tstat != TOKENIZE_OK) {
        printf("Error: Tokenizer: %s.\n", tokenize_status_string(tstat));
        return -1;
    }

    if (tokens.count == 0 || tokens.data[0].type == TOKEN_EOF) {
        return 0;
    }

    pstat = parse_tokens(tokens, &ast);
    if (pstat != PARSE_OK) {
        printf("Error: Parser: %s.\n", parse_status_string(pstat));
        return -1;
    }

    estat = execute(&ast);
    ast_free(&ast);
    return (estat == EXEC_OK) ? 0 : -1;
}

int cli_read_line(FILE *stream, char *buf, size_t buflen) {
    size_t len;
    int c;

    if (stream == NULL || buf == NULL || buflen < 2) {
        return -1;
    }

    if (fgets(buf, (int)buflen, stream) == NULL) {
        return -1;
    }

    len = strlen(buf);
    if (len > 0 && buf[len - 1] == '\n') {
        buf[len - 1] = '\0';
        return 0;
    }

    /* No newline: either EOF without newline, or line longer than buffer. */
    c = fgetc(stream);
    if (c == EOF) {
        /* Last line without newline — treat as complete. */
        return 0;
    }

    /* Oversized: discard rest of line. */
    while (c != '\n' && c != EOF) {
        c = fgetc(stream);
    }
    buf[0] = '\0';
    return -2;
}

int cli_handle_line(const char *input) {
    if (input == NULL) {
        return 0;
    }
    while (isspace((unsigned char)*input)) {
        input++;
    }
    if (*input == '\0') {
        return 0;
    }

    if (!is_sql_statement(input)) {
        char cmd[64] = {0};
        sscanf(input, "%63s", cmd);
        {
            size_t n = strlen(cmd);
            if (n > 0 && cmd[n - 1] == ';') {
                cmd[n - 1] = '\0';
            }
        }
        if (strcmp(cmd, "exit") == 0 || strcmp(cmd, "quit") == 0) {
            return 1;
        }
        handle_meta_command(input);
        return 0;
    }

    handle_sql_input(input);
    return 0;
}

void cli_run(void) {
    char input[CLI_MAX_INPUT];
    int rc;

    printf("SQL Engine CLI\n");
    printf("Type 'help' for available commands.\n");
    printf("> ");
    fflush(stdout);

    while ((rc = cli_read_line(stdin, input, sizeof(input))) != -1) {
        if (rc == -2) {
            printf("Error: Input too long (maximum %d characters per line).\n",
                   CLI_MAX_INPUT - 1);
            printf("> ");
            fflush(stdout);
            continue;
        }

        if (cli_handle_line(input) == 1) {
            break;
        }

        printf("> ");
        fflush(stdout);
    }
}
