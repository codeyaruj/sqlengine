#include "cli.h"
#include "tokenizer.h"
#include "parser.h"
#include "executor.h"
#include "index.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <ctype.h>

#define MAX_INPUT 512
#define MAX_TOKENS 64

static int is_sql_statement(const char* input) {
    while (isspace((unsigned char)*input)) input++;
    
    if (strncasecmp(input, "SELECT", 6) == 0) return 1;
    if (strncasecmp(input, "INSERT", 6) == 0) return 1;
    return 0;
}

static void handle_meta_command(const char* input) {
    char cmd[32] = {0};
    char arg[32] = {0};
    
    sscanf(input, "%31s %31s", cmd, arg);
    
    size_t cmd_len = strlen(cmd);
    if (cmd_len > 0 && cmd[cmd_len - 1] == ';') {
        cmd[cmd_len - 1] = '\0';
    }
    
    size_t arg_len = strlen(arg);
    if (arg_len > 0 && arg[arg_len - 1] == ';') {
        arg[arg_len - 1] = '\0';
    }
    
    if (strcmp(cmd, "create_table") == 0) {
        if (arg[0] == '\0') {
            printf("Usage: create_table <table_name>\n");
        } else if (create_table(arg) == 0) {
            printf("Table '%s' created successfully.\n", arg);
        } else {
            printf("Error: Could not create table '%s'.\n", arg);
        }
    }
    else if (strcmp(cmd, "rebuild_index") == 0) {
        if (arg[0] == '\0') {
            printf("Usage: rebuild_index <table_name>\n");
        } else if (index_rebuild(arg) == 0) {
            printf("Index for '%s' rebuilt successfully.\n", arg);
        } else {
            printf("Error: Could not rebuild index for '%s'.\n", arg);
        }
    }
    else if (strcmp(cmd, "help") == 0) {
        printf("SQL Engine CLI (Phase 3 - Indexing & Optimization)\n");
        printf("\nSQL Commands:\n");
        printf("  SELECT * FROM <table> WHERE id = <value>\n");
        printf("  SELECT * FROM <table> WHERE name = \"value\"\n");
        printf("  INSERT INTO <table> VALUES (<id>, \"name\")\n");
        printf("\nMeta Commands:\n");
        printf("  create_table <name>    - Create a new table\n");
        printf("  rebuild_index <name>   - Rebuild index for table\n");
        printf("  help                   - Show this help\n");
        printf("  exit, quit             - Exit the CLI\n");
    }
    else if (strcmp(cmd, "exit") == 0 || strcmp(cmd, "quit") == 0) {
        exit(0);
    }
    else {
        printf("Unknown command: %s\n", cmd);
        printf("Type 'help' for available commands.\n");
    }
}

static int handle_sql_input(const char* input) {
    Token tokens[MAX_TOKENS];
    int token_count = tokenize(input, tokens, MAX_TOKENS);
    
    if (token_count == 0 || tokens[0].type == TOKEN_EOF) {
        return 0;
    }
    
    AST ast;
    if (parse(tokens, token_count, &ast) != 0) {
        printf("Error: Failed to parse SQL query.\n");
        return -1;
    }
    
    int result = execute(&ast);
    ast_free(&ast);
    
    return result;
}

void cli_run(void) {
    char input[MAX_INPUT];
    
    printf("SQL Engine CLI (Phase 3 - Indexing & Optimization)\n");
    printf("Type 'help' for available commands.\n");
    printf("> ");
    
    while (fgets(input, sizeof(input), stdin) != NULL) {
        input[strcspn(input, "\n")] = '\0';
        
        if (strlen(input) == 0) {
            printf("> ");
            continue;
        }
        
        if (!is_sql_statement(input)) {
            handle_meta_command(input);
        } else {
            handle_sql_input(input);
        }
        
        printf("> ");
    }
}