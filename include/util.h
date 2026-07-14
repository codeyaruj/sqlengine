#ifndef UTIL_H
#define UTIL_H

#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>
#include <stdio.h>

/* Checked arithmetic: return false on overflow. */
bool util_mul_size(size_t a, size_t b, size_t *out);
bool util_add_size(size_t a, size_t b, size_t *out);
bool util_mul_u64(uint64_t a, uint64_t b, uint64_t *out);
bool util_add_u64(uint64_t a, uint64_t b, uint64_t *out);

/* Parse a full token as int32. Rejects partial consumption and out-of-range. */
bool util_parse_int32(const char *text, int32_t *out);

/* Little-endian fixed-width I/O (portable on-disk format). */
bool util_write_u32_le(FILE *f, uint32_t v);
bool util_write_u64_le(FILE *f, uint64_t v);
bool util_write_i32_le(FILE *f, int32_t v);
bool util_write_i64_le(FILE *f, int64_t v);

bool util_read_u32_le(FILE *f, uint32_t *v);
bool util_read_u64_le(FILE *f, uint64_t *v);
bool util_read_i32_le(FILE *f, int32_t *v);
bool util_read_i64_le(FILE *f, int64_t *v);

/* Case-insensitive ASCII compare of exactly n characters of a against b (null-terminated). */
int util_strncasecmp(const char *a, const char *b, size_t n);

/* True if c is a word boundary for SQL keyword detection. */
bool util_is_word_boundary(char c);

#endif
