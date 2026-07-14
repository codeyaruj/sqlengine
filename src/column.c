#include "column.h"

#include <string.h>

ColumnType column_from_name(const char *name) {
    if (name == NULL) {
        return COLUMN_INVALID;
    }
    if (strcmp(name, "*") == 0) {
        return COLUMN_ALL;
    }
    if (strcmp(name, "id") == 0) {
        return COLUMN_ID;
    }
    if (strcmp(name, "name") == 0) {
        return COLUMN_NAME;
    }
    return COLUMN_INVALID;
}

const char *column_type_name(ColumnType col) {
    switch (col) {
        case COLUMN_ID: return "id";
        case COLUMN_NAME: return "name";
        case COLUMN_ALL: return "*";
        default: return "<invalid>";
    }
}
