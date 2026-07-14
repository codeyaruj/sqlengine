#ifndef CLI_H
#define CLI_H

#include <stdio.h>

#define CLI_MAX_INPUT 512

void cli_run(void);

/* Process one complete input line (no trailing newline required). Returns 0 to continue, 1 to exit. */
int cli_handle_line(const char *input);

/*
 * Read a line from stream into buf (size buflen).
 * Returns:
 *   0  - success (line stored, newline stripped)
 *  -1  - EOF
 *  -2  - input too long (remainder discarded)
 */
int cli_read_line(FILE *stream, char *buf, size_t buflen);

#endif
