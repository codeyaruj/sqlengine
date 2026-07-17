#include "semantic.h"
#include "util.h"

#include <stdint.h>

SemanticStatus semantic_validate(const AST *ast) {
    int i;

    if (ast == NULL) {
        return SEMANTIC_INVALID_VALUE;
    }

    if (ast->type == AST_SELECT) {
        const SelectQuery *sel = &ast->query.select;

        if (!sel->select_all) {
            if (sel->column_count <= 0) {
                return SEMANTIC_UNKNOWN_COLUMN;
            }
            if (sel->column_count > MAX_COLUMNS) {
                return SEMANTIC_INVALID_VALUE;
            }
            for (i = 0; i < sel->column_count; i++) {
                ColumnType col = column_from_name(sel->columns[i]);
                if (col == COLUMN_INVALID || col == COLUMN_ALL) {
                    return SEMANTIC_UNKNOWN_COLUMN;
                }
            }
        }

        if (sel->has_where) {
            ColumnType wcol = column_from_name(sel->where_column);
            int32_t dummy;

            if (wcol != COLUMN_ID && wcol != COLUMN_NAME) {
                return SEMANTIC_UNKNOWN_COLUMN;
            }

            if (wcol == COLUMN_ID) {
                if (sel->where_value_type != LITERAL_NUMBER) {
                    return SEMANTIC_TYPE_MISMATCH;
                }
                if (!util_parse_int32(sel->where_value, &dummy)) {
                    return SEMANTIC_INVALID_VALUE;
                }
            } else if (wcol == COLUMN_NAME) {
                if (sel->where_value_type != LITERAL_STRING) {
                    return SEMANTIC_TYPE_MISMATCH;
                }
            }
        }
        return SEMANTIC_OK;
    }

    if (ast->type == AST_INSERT) {
        /* id and name already constrained by parser types. */
        return SEMANTIC_OK;
    }

    return SEMANTIC_INVALID_VALUE;
}
