#include "util.h"

#include <ctype.h>
#include <errno.h>
#include <fcntl.h>
#include <inttypes.h>
#include <limits.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

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

bool util_copy_checked(char *destination, size_t destination_size, const char *source) {
    size_t length;
    if (destination == NULL || destination_size == 0 || source == NULL) {
        return false;
    }
    length = strlen(source);
    if (length >= destination_size) {
        return false;
    }
    memcpy(destination, source, length + 1);
    return true;
}

bool util_parse_int32(const char *text, int32_t *out) {
    char *end = NULL;
    intmax_t v;

    if (text == NULL || out == NULL || text[0] == '\0') {
        return false;
    }

    errno = 0;
    v = strtoimax(text, &end, 10);
    if (errno == ERANGE || end == text || *end != '\0') {
        return false;
    }
    if (v < INT32_MIN || v > INT32_MAX) {
        return false;
    }

    *out = (int32_t)v;
    return true;
}

bool util_secure_temp_file(const char *destination, char *path, size_t path_size, int *fd) {
    static const char suffix[] = ".tmp.XXXXXX";
    size_t destination_length;
    size_t needed;
    int local_fd;

    if (destination == NULL || path == NULL || path_size == 0 || fd == NULL) {
        errno = EINVAL;
        return false;
    }
    destination_length = strlen(destination);
    if (!util_add_size(destination_length, sizeof(suffix), &needed) || needed > path_size) {
        errno = ENAMETOOLONG;
        return false;
    }
    memcpy(path, destination, destination_length);
    memcpy(path + destination_length, suffix, sizeof(suffix));

    local_fd = mkstemp(path);
    if (local_fd < 0) {
        return false;
    }
    if (fchmod(local_fd, S_IRUSR | S_IWUSR) != 0) {
        int saved_errno = errno;
        close(local_fd);
        unlink(path);
        errno = saved_errno;
        return false;
    }
    (void)fcntl(local_fd, F_SETFD, FD_CLOEXEC);
    *fd = local_fd;
    return true;
}

bool util_flush_and_sync(FILE *f) {
    int fd;
    if (f == NULL) {
        errno = EINVAL;
        return false;
    }
    if (fflush(f) != 0) {
        return false;
    }
    fd = fileno(f);
    if (fd < 0 || fsync(fd) != 0) {
        return false;
    }
    return true;
}

bool util_sync_parent_directory(const char *path) {
    char directory[512];
    const char *slash;
    size_t length;
    int flags = O_RDONLY;
    int fd;
    int result;
    int saved_errno;

    if (path == NULL) {
        errno = EINVAL;
        return false;
    }
    slash = strrchr(path, '/');
    if (slash == NULL) {
        directory[0] = '.';
        directory[1] = '\0';
    } else {
        if (slash == path) {
            length = 1;
        } else {
            length = (size_t)(slash - path);
        }
        if (length >= sizeof(directory)) {
            errno = ENAMETOOLONG;
            return false;
        }
        memcpy(directory, path, length);
        directory[length] = '\0';
    }
#ifdef O_DIRECTORY
    flags |= O_DIRECTORY;
#endif
    fd = open(directory, flags);
    if (fd < 0) {
        return false;
    }
    result = fsync(fd);
    saved_errno = errno;
    if (close(fd) != 0 && result == 0) {
        return false;
    }
    if (result != 0) {
        if (saved_errno == EINVAL
#ifdef ENOTSUP
            || saved_errno == ENOTSUP
#endif
        ) {
            return true;
        }
        errno = saved_errno;
        return false;
    }
    return true;
}

bool util_print_escaped_field(FILE *out, const unsigned char *bytes, size_t size) {
    size_t end = size;
    size_t i;
    static const char hex[] = "0123456789ABCDEF";

    if (out == NULL || bytes == NULL) {
        return false;
    }
    while (end > 0 && bytes[end - 1] == 0) {
        end--;
    }
    for (i = 0; i < end; i++) {
        unsigned char c = bytes[i];
        if (c == '\n') {
            if (fputs("\\n", out) == EOF) {
                return false;
            }
        } else if (c == '\r') {
            if (fputs("\\r", out) == EOF) {
                return false;
            }
        } else if (c == '\t') {
            if (fputs("\\t", out) == EOF) {
                return false;
            }
        } else if (c == '\\') {
            if (fputs("\\\\", out) == EOF) {
                return false;
            }
        } else if (c == '"') {
            if (fputs("\\\"", out) == EOF) {
                return false;
            }
        } else if (c < 0x20u || c == 0x7Fu || !isprint(c)) {
            char escaped[5] = {'\\', 'x', hex[c >> 4], hex[c & 0x0Fu], '\0'};
            if (fputs(escaped, out) == EOF) {
                return false;
            }
        } else if (fputc((int)c, out) == EOF) {
            return false;
        }
    }
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
