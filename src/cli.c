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

static void remove_trailing_semicolon(char *word) {
    size_t length = strlen(word);

    if (length > 0 && word[length - 1] == ';') {
        word[length - 1] = '\0';
    }
}

static void handle_create_table(const char *table_name) {
    TableCreateStatus status;

    if (table_name[0] == '\0') {
        printf("Usage: create_table <table_name>\n");
        return;
    }

    status = storage_create_table(table_name);
    if (status == TABLE_CREATE_OK) {
        printf("Table '%s' created successfully.\n", table_name);
    } else if (status == TABLE_CREATE_ALREADY_EXISTS) {
        printf("Table '%s' already exists.\n", table_name);
    } else if (status == TABLE_CREATE_INVALID_NAME) {
        printf("Error: Invalid table name '%s'.\n", table_name);
    } else if (status == TABLE_CREATE_NAME_TOO_LONG) {
        printf("Error: Table name '%s' is too long (maximum %u bytes).\n",
               table_name, (unsigned int)SQL_MAX_TABLE_NAME_LENGTH);
    } else {
        printf("Error: Could not create table '%s' (%s).\n",
               table_name, table_create_status_string(status));
    }
}

static void handle_rebuild_index(const char *table_name) {
    IndexStatus status;

    if (table_name[0] == '\0') {
        printf("Usage: rebuild_index <table_name>\n");
        return;
    }

    status = index_rebuild(table_name);
    if (status == INDEX_OK) {
        printf("Index for '%s' rebuilt successfully.\n", table_name);
    } else {
        printf("Error: Could not rebuild index for '%s' (%s).\n",
               table_name, index_status_string(status));
    }
}

static void print_help(void) {
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
}

static void handle_meta_command(const char *input) {
    char command[64] = {0};
    char argument[64] = {0};

    (void)sscanf(input, "%63s %63s", command, argument);
    remove_trailing_semicolon(command);
    remove_trailing_semicolon(argument);

    if (strcmp(command, "create_table") == 0) {
        handle_create_table(argument);
    } else if (strcmp(command, "rebuild_index") == 0) {
        handle_rebuild_index(argument);
    } else if (strcmp(command, "help") == 0) {
        print_help();
    } else if (strcmp(command, "exit") == 0 || strcmp(command, "quit") == 0) {
        exit(0);
    } else {
        printf("Unknown command: %s\n", command);
        printf("Type 'help' for available commands.\n");
    }
}

static int handle_sql_input(const char *input) {
    Token token_storage[MAX_TOKENS];
    TokenBuffer tokens = { NULL, 0 };
    TokenizeStatus tokenize_status;
    ParseStatus parse_status;
    AST ast;
    ExecStatus execute_status;

    tokenize_status = tokenize(input, token_storage, MAX_TOKENS, &tokens);
    if (tokenize_status != TOKENIZE_OK) {
        printf("Error: Tokenizer: %s.\n", tokenize_status_string(tokenize_status));
        return -1;
    }

    if (tokens.count == 0 || tokens.data[0].type == TOKEN_EOF) {
        return 0;
    }

    parse_status = parse_tokens(tokens, &ast);
    if (parse_status != PARSE_OK) {
        printf("Error: Parser: %s.\n", parse_status_string(parse_status));
        return -1;
    }

    execute_status = execute(&ast);
    ast_free(&ast);
    if (execute_status == EXEC_OK) {
        return 0;
    }
    return -1;
}

int cli_read_line(FILE *stream, char *buf, size_t buflen) {
    size_t length;
    int character;

    if (stream == NULL || buf == NULL || buflen < 2) {
        return -1;
    }

    if (fgets(buf, (int)buflen, stream) == NULL) {
        return -1;
    }

    length = strlen(buf);
    if (length > 0 && buf[length - 1] == '\n') {
        buf[length - 1] = '\0';
        return 0;
    }

    /* No newline: either EOF without newline, or line longer than buffer. */
    character = fgetc(stream);
    if (character == EOF) {
        /* Last line without newline — treat as complete. */
        return 0;
    }

    /* Oversized: discard rest of line. */
    while (character != '\n' && character != EOF) {
        character = fgetc(stream);
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
        char command[64] = {0};

        (void)sscanf(input, "%63s", command);
        remove_trailing_semicolon(command);
        if (strcmp(command, "exit") == 0 || strcmp(command, "quit") == 0) {
            return 1;
        }
        handle_meta_command(input);
        return 0;
    }

    (void)handle_sql_input(input);
    return 0;
}

void cli_run(void) {
    char input[CLI_MAX_INPUT];
    int result;

    printf("SQL Engine CLI\n");
    printf("Type 'help' for available commands.\n");
    printf("> ");
    fflush(stdout);

    while (1) {
        result = cli_read_line(stdin, input, sizeof(input));
        if (result == -1) {
            break;
        }

        if (result == -2) {
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
