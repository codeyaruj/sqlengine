#ifndef COLUMN_H
#define COLUMN_H

typedef enum {
    COLUMN_ID,
    COLUMN_NAME,
    COLUMN_ALL,
    COLUMN_INVALID
} ColumnType;

/* Map a column name (or "*") to a schema column. Case-sensitive identifiers. */
ColumnType column_from_name(const char *name);

const char *column_type_name(ColumnType col);

#endif
