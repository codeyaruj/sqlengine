#include "util.h"

#include <ctype.h>
#include <errno.h>
#include <limits.h>
#include <stdlib.h>
#include <string.h>

bool util_mul_size(size_t a, size_t b, size_t *out) {
    if (out == NULL) {
        return false;
    }
    if (a != 0 && b > SIZE_MAX / a) {
        return false;
    }
    *out = a * b;
    return true;
}

bool util_add_size(size_t a, size_t b, size_t *out) {
    if (out == NULL) {
        return false;
    }
    if (b > SIZE_MAX - a) {
        return false;
    }
    *out = a + b;
    return true;
}

bool util_mul_u64(uint64_t a, uint64_t b, uint64_t *out) {
    if (out == NULL) {
        return false;
    }
    if (a != 0 && b > UINT64_MAX / a) {
        return false;
    }
    *out = a * b;
    return true;
}

bool util_add_u64(uint64_t a, uint64_t b, uint64_t *out) {
    if (out == NULL) {
        return false;
    }
    if (b > UINT64_MAX - a) {
        return false;
    }
    *out = a + b;
    return true;
}

bool util_parse_int32(const char *text, int32_t *out) {
    char *end = NULL;
    long v;

    if (text == NULL || out == NULL || text[0] == '\0') {
        return false;
    }

    errno = 0;
    v = strtol(text, &end, 10);
    if (errno == ERANGE || end == text || *end != '\0') {
        return false;
    }
    if (v < INT32_MIN || v > INT32_MAX) {
        return false;
    }

    *out = (int32_t)v;
    return true;
}

bool util_write_u32_le(FILE *f, uint32_t v) {
    unsigned char b[4];
    b[0] = (unsigned char)(v & 0xFFu);
    b[1] = (unsigned char)((v >> 8) & 0xFFu);
    b[2] = (unsigned char)((v >> 16) & 0xFFu);
    b[3] = (unsigned char)((v >> 24) & 0xFFu);
    return fwrite(b, 1, 4, f) == 4;
}

bool util_write_u64_le(FILE *f, uint64_t v) {
    unsigned char b[8];
    int i;
    for (i = 0; i < 8; i++) {
        b[i] = (unsigned char)((v >> (8 * i)) & 0xFFu);
    }
    return fwrite(b, 1, 8, f) == 8;
}

bool util_write_i32_le(FILE *f, int32_t v) {
    return util_write_u32_le(f, (uint32_t)v);
}

bool util_write_i64_le(FILE *f, int64_t v) {
    return util_write_u64_le(f, (uint64_t)v);
}

bool util_read_u32_le(FILE *f, uint32_t *v) {
    unsigned char b[4];
    if (v == NULL || fread(b, 1, 4, f) != 4) {
        return false;
    }
    *v = (uint32_t)b[0]
       | ((uint32_t)b[1] << 8)
       | ((uint32_t)b[2] << 16)
       | ((uint32_t)b[3] << 24);
    return true;
}

bool util_read_u64_le(FILE *f, uint64_t *v) {
    unsigned char b[8];
    int i;
    if (v == NULL || fread(b, 1, 8, f) != 8) {
        return false;
    }
    *v = 0;
    for (i = 0; i < 8; i++) {
        *v |= ((uint64_t)b[i]) << (8 * i);
    }
    return true;
}

bool util_read_i32_le(FILE *f, int32_t *v) {
    uint32_t u;
    if (!util_read_u32_le(f, &u)) {
        return false;
    }
    *v = (int32_t)u;
    return true;
}

bool util_read_i64_le(FILE *f, int64_t *v) {
    uint64_t u;
    if (!util_read_u64_le(f, &u)) {
        return false;
    }
    *v = (int64_t)u;
    return true;
}

int util_strncasecmp(const char *a, const char *b, size_t n) {
    size_t i;
    for (i = 0; i < n; i++) {
        unsigned char ca = (unsigned char)a[i];
        unsigned char cb = (unsigned char)b[i];
        if (ca == '\0' || cb == '\0') {
            return (int)ca - (int)cb;
        }
        ca = (unsigned char)tolower(ca);
        cb = (unsigned char)tolower(cb);
        if (ca != cb) {
            return (int)ca - (int)cb;
        }
    }
    return 0;
}

bool util_is_word_boundary(char c) {
    if (c == '\0') {
        return true;
    }
    if (isalnum((unsigned char)c) || c == '_') {
        return false;
    }
    return true;
}
