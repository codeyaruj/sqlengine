#include "parser.h"
#include "util.h"

#include <string.h>

void parser_init(Parser *p, TokenBuffer tokens) {
    if (p == NULL) {
        return;
    }
    p->tokens = tokens;
    p->position = 0;
}

bool parser_at_end(const Parser *p) {
    if (p == NULL || p->tokens.data == NULL || p->tokens.count == 0) {
        return true;
    }
    if (p->position >= p->tokens.count) {
        return true;
    }
    return p->tokens.data[p->position].type == TOKEN_EOF;
}

const Token *parser_peek(const Parser *p) {
    static const Token eof_token = { TOKEN_EOF, "" };
    if (p == NULL || p->tokens.data == NULL || p->tokens.count == 0) {
        return &eof_token;
    }
    if (p->position >= p->tokens.count) {
        return &eof_token;
    }
    return &p->tokens.data[p->position];
}

const Token *parser_previous(const Parser *p) {
    static const Token eof_token = { TOKEN_EOF, "" };
    if (p == NULL || p->tokens.data == NULL || p->tokens.count == 0 ||
        p->position == 0 || p->position > p->tokens.count) {
        return &eof_token;
    }
    return &p->tokens.data[p->position - 1];
}

const Token *parser_advance(Parser *p) {
    const Token *cur = parser_peek(p);
    if (!parser_at_end(p) && p->position < p->tokens.count) {
        p->position++;
    }
    return cur;
}

bool parser_check(const Parser *p, TokenType type) {
    return parser_peek(p)->type == type;
}

bool parser_match(Parser *p, TokenType type) {
    if (parser_check(p, type)) {
        parser_advance(p);
        return true;
    }
    return false;
}

bool parser_expect(Parser *p, TokenType type) {
    if (parser_check(p, type)) {
        parser_advance(p);
        return true;
    }
    return false;
}

static ParseStatus missing_token_status(const Parser *p) {
    if (parser_at_end(p)) {
        return PARSE_UNEXPECTED_EOF;
    }
    return PARSE_ERROR;
}

static ParseStatus parse_select(Parser *p, AST *ast) {
    SelectQuery *sel = &ast->query.select;

    sel->select_all = 0;
    sel->column_count = 0;
    sel->has_where = 0;
    sel->table_name[0] = '\0';
    sel->where_column[0] = '\0';
    sel->where_value[0] = '\0';
    sel->where_value_type = LITERAL_NUMBER;

    if (!parser_expect(p, TOKEN_SELECT)) {
        return PARSE_ERROR;
    }

    if (parser_match(p, TOKEN_STAR)) {
        sel->select_all = 1;
    } else if (parser_check(p, TOKEN_IDENTIFIER)) {
        sel->select_all = 0;
        if (!util_copy_checked(sel->columns[0], sizeof(sel->columns[0]),
                               parser_peek(p)->value)) {
            return PARSE_IDENTIFIER_TOO_LONG;
        }
        sel->column_count = 1;
        parser_advance(p);

        while (parser_match(p, TOKEN_COMMA)) {
            if (!parser_check(p, TOKEN_IDENTIFIER)) {
                return missing_token_status(p);
            }
            if (sel->column_count >= MAX_COLUMNS) {
                return PARSE_ERROR;
            }
            if (!util_copy_checked(sel->columns[sel->column_count],
                                   sizeof(sel->columns[sel->column_count]),
                                   parser_peek(p)->value)) {
                return PARSE_IDENTIFIER_TOO_LONG;
            }
            sel->column_count++;
            parser_advance(p);
        }
    } else {
        return missing_token_status(p);
    }

    if (!parser_expect(p, TOKEN_FROM)) {
        return missing_token_status(p);
    }

    if (!parser_check(p, TOKEN_IDENTIFIER)) {
        return missing_token_status(p);
    }
    if (!util_copy_checked(sel->table_name, sizeof(sel->table_name), parser_peek(p)->value)) {
        return PARSE_TABLE_NAME_TOO_LONG;
    }
    parser_advance(p);

    if (parser_match(p, TOKEN_WHERE)) {
        sel->has_where = 1;

        if (!parser_check(p, TOKEN_IDENTIFIER)) {
            return missing_token_status(p);
        }
        if (!util_copy_checked(sel->where_column, sizeof(sel->where_column),
                               parser_peek(p)->value)) {
            return PARSE_IDENTIFIER_TOO_LONG;
        }
        parser_advance(p);

        if (!parser_expect(p, TOKEN_EQUAL)) {
            return missing_token_status(p);
        }

        if (parser_check(p, TOKEN_NUMBER)) {
            sel->where_value_type = LITERAL_NUMBER;
            if (!util_copy_checked(sel->where_value, sizeof(sel->where_value),
                                   parser_peek(p)->value)) {
                return PARSE_INTEGER_OUT_OF_RANGE;
            }
            parser_advance(p);
        } else if (parser_check(p, TOKEN_STRING)) {
            sel->where_value_type = LITERAL_STRING;
            if (!util_copy_checked(sel->where_value, sizeof(sel->where_value),
                                   parser_peek(p)->value)) {
                return PARSE_STRING_TOO_LONG;
            }
            parser_advance(p);
        } else {
            return missing_token_status(p);
        }
    }

    if (!parser_expect(p, TOKEN_SEMICOLON)) {
        return missing_token_status(p);
    }

    if (!parser_at_end(p)) {
        return PARSE_TRAILING_TOKENS;
    }
    return PARSE_OK;
}

static ParseStatus parse_insert(Parser *p, AST *ast) {
    InsertQuery *ins = &ast->query.insert;
    int32_t id;

    ins->table_name[0] = '\0';
    ins->id = 0;
    ins->name[0] = '\0';

    if (!parser_expect(p, TOKEN_INSERT)) {
        return PARSE_ERROR;
    }
    if (!parser_expect(p, TOKEN_INTO)) {
        return missing_token_status(p);
    }

    if (!parser_check(p, TOKEN_IDENTIFIER)) {
        return missing_token_status(p);
    }
    if (!util_copy_checked(ins->table_name, sizeof(ins->table_name), parser_peek(p)->value)) {
        return PARSE_TABLE_NAME_TOO_LONG;
    }
    parser_advance(p);

    if (!parser_expect(p, TOKEN_VALUES)) {
        return missing_token_status(p);
    }
    if (!parser_expect(p, TOKEN_LPAREN)) {
        return missing_token_status(p);
    }

    if (!parser_check(p, TOKEN_NUMBER)) {
        return missing_token_status(p);
    }
    if (!util_parse_int32(parser_peek(p)->value, &id)) {
        return PARSE_INTEGER_OUT_OF_RANGE;
    }
    ins->id = id;
    parser_advance(p);

    if (!parser_expect(p, TOKEN_COMMA)) {
        return missing_token_status(p);
    }

    if (!parser_check(p, TOKEN_STRING)) {
        return missing_token_status(p);
    }
    if (!util_copy_checked(ins->name, sizeof(ins->name), parser_peek(p)->value)) {
        return PARSE_STRING_TOO_LONG;
    }
    parser_advance(p);

    if (!parser_expect(p, TOKEN_RPAREN)) {
        return missing_token_status(p);
    }
    if (!parser_expect(p, TOKEN_SEMICOLON)) {
        return missing_token_status(p);
    }

    if (!parser_at_end(p)) {
        return PARSE_TRAILING_TOKENS;
    }
    return PARSE_OK;
}

ParseStatus parse_tokens(TokenBuffer tokens, AST *ast) {
    Parser p;

    if (ast == NULL || (tokens.data == NULL && tokens.count != 0)) {
        return PARSE_NULL_INPUT;
    }

    ast->type = AST_UNKNOWN;
    parser_init(&p, tokens);

    if (parser_check(&p, TOKEN_SELECT)) {
        ast->type = AST_SELECT;
        return parse_select(&p, ast);
    }
    if (parser_check(&p, TOKEN_INSERT)) {
        ast->type = AST_INSERT;
        return parse_insert(&p, ast);
    }
    if (parser_at_end(&p)) {
        return PARSE_UNEXPECTED_EOF;
    }
    return PARSE_ERROR;
}

void ast_free(AST *ast) {
    (void)ast;
}
