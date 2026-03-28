#include "parser.h"
#include <string.h>
#include <stdlib.h>

static int peek(Token* tokens, int pos, TokenType expected) {
    return tokens[pos].type == expected;
}

static int expect(Token* tokens, int* pos, TokenType expected) {
    if (tokens[*pos].type == expected) {
        (*pos)++;
        return 1;
    }
    return 0;
}

static int parse_select(Token* tokens, int* pos, int token_count, AST* ast) {
    int start = *pos;
    (void)start;
    
    SelectQuery* sel = &ast->query.select;
    sel->select_all = 0;
    sel->column_count = 0;
    sel->has_where = 0;
    sel->table_name[0] = '\0';
    sel->where_column[0] = '\0';
    sel->where_value[0] = '\0';
    
    if (!expect(tokens, pos, TOKEN_SELECT)) return -1;
    
    if (peek(tokens, *pos, TOKEN_STAR)) {
        sel->select_all = 1;
        (*pos)++;
    }
    else if (peek(tokens, *pos, TOKEN_IDENTIFIER)) {
        sel->select_all = 0;
        strncpy(sel->columns[0], tokens[*pos].value, 31);
        sel->columns[0][31] = '\0';
        sel->column_count = 1;
        (*pos)++;
        
        while (peek(tokens, *pos, TOKEN_COMMA)) {
            (*pos)++;
            if (!peek(tokens, *pos, TOKEN_IDENTIFIER)) return -1;
            if (sel->column_count >= MAX_COLUMNS) return -1;
            strncpy(sel->columns[sel->column_count], tokens[*pos].value, 31);
            sel->columns[sel->column_count][31] = '\0';
            sel->column_count++;
            (*pos)++;
        }
    }
    else {
        return -1;
    }
    
    if (!expect(tokens, pos, TOKEN_FROM)) return -1;
    
    if (!peek(tokens, *pos, TOKEN_IDENTIFIER)) return -1;
    strncpy(sel->table_name, tokens[*pos].value, 31);
    sel->table_name[31] = '\0';
    (*pos)++;
    
    if (peek(tokens, *pos, TOKEN_WHERE)) {
        (*pos)++;
        sel->has_where = 1;
        
        if (!peek(tokens, *pos, TOKEN_IDENTIFIER)) return -1;
        strncpy(sel->where_column, tokens[*pos].value, 31);
        sel->where_column[31] = '\0';
        (*pos)++;
        
        if (!expect(tokens, pos, TOKEN_EQUAL)) return -1;
        
        if (!peek(tokens, *pos, TOKEN_NUMBER) && !peek(tokens, *pos, TOKEN_STRING)) {
            return -1;
        }
        strncpy(sel->where_value, tokens[*pos].value, 31);
        sel->where_value[31] = '\0';
        (*pos)++;
    }
    
    if (!expect(tokens, pos, TOKEN_SEMICOLON)) return -1;
    
    return 0;
}

static int parse_insert(Token* tokens, int* pos, int token_count, AST* ast) {
    InsertQuery* ins = &ast->query.insert;
    ins->table_name[0] = '\0';
    ins->id = 0;
    ins->name[0] = '\0';
    
    if (!expect(tokens, pos, TOKEN_INSERT)) return -1;
    if (!expect(tokens, pos, TOKEN_INTO)) return -1;
    
    if (!peek(tokens, *pos, TOKEN_IDENTIFIER)) return -1;
    strncpy(ins->table_name, tokens[*pos].value, 31);
    ins->table_name[31] = '\0';
    (*pos)++;
    
    if (!expect(tokens, pos, TOKEN_VALUES)) return -1;
    if (!expect(tokens, pos, TOKEN_LPAREN)) return -1;
    
    if (!peek(tokens, *pos, TOKEN_NUMBER)) return -1;
    ins->id = atoi(tokens[*pos].value);
    (*pos)++;
    
    if (!expect(tokens, pos, TOKEN_COMMA)) return -1;
    
    if (!peek(tokens, *pos, TOKEN_STRING)) return -1;
    strncpy(ins->name, tokens[*pos].value, 31);
    ins->name[31] = '\0';
    (*pos)++;
    
    if (!expect(tokens, pos, TOKEN_RPAREN)) return -1;
    if (!expect(tokens, pos, TOKEN_SEMICOLON)) return -1;
    
    return 0;
}

int parse(Token* tokens, int token_count, AST* ast) {
    if (tokens == NULL || token_count == 0) {
        return -1;
    }
    
    ast->type = AST_UNKNOWN;
    
    if (peek(tokens, 0, TOKEN_SELECT)) {
        ast->type = AST_SELECT;
        int pos = 0;
        return parse_select(tokens, &pos, token_count, ast);
    }
    else if (peek(tokens, 0, TOKEN_INSERT)) {
        ast->type = AST_INSERT;
        int pos = 0;
        return parse_insert(tokens, &pos, token_count, ast);
    }
    
    return -1;
}

void ast_free(AST* ast) {
    (void)ast;
}