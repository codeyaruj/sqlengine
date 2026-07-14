![License: MIT](https://img.shields.io/badge/License-MIT-yellow.svg)

# Mini SQL Engine in C

A lightweight, modular SQL database engine built from scratch in C for learning database internals.

This project implements:

* Page-based storage with a versioned on-disk format
* SQL tokenizer and bounds-safe parser
* Semantic validation against a fixed schema
* Query execution (SELECT / INSERT only)
* Hash index on `id` as a rebuildable optimization
* Interactive CLI

It is **not** a full SQL database. There are no joins, transactions, UPDATE/DELETE, aggregation, or variable-width schemas.

---

## Supported SQL grammar

Keywords are **case-insensitive**. Identifiers and string contents are **case-sensitive**.

```sql
INSERT INTO <table> VALUES (<int32_id>, "<name>");
SELECT * FROM <table>;
SELECT <col>[, <col>...] FROM <table>;
SELECT * FROM <table> WHERE <col> = <value>;
SELECT <col> FROM <table> WHERE <col> = <value>;
```

Rules:

* Exactly one statement per input line; a trailing semicolon is required
* Trailing tokens after the semicolon are rejected (no multi-statement execution)
* String literals use double quotes only (`"..."`); no escape sequences
* Numeric literals must be valid `int32` values (checked with `strtol`)

### Fixed schema

Every table uses the same row layout:

| Column | Type                         | Notes                                      |
|--------|------------------------------|--------------------------------------------|
| `id`   | `int32`                      | Unique primary key                         |
| `name` | fixed string (`NAME_SIZE` 32)| Up to 31 characters + NUL in memory        |
| `*`    | projection only              | Selects both columns                       |

Unsupported column names produce a **semantic error**.

### Meta commands

```text
create_table <name>
rebuild_index <name>
help
exit / quit
```

`create_table` reports whether the table was created or already existed; it never truncates an existing table.

---

## Architecture

```text
SQL text
  → tokenizer
  → parser (AST)
  → semantic validation
  → executor
       → storage (page-aware rows)
       → index (optional, rebuildable)
  → CLI output
```

### Storage

* Page size: **4096** bytes
* Row size: **36** bytes (`int32` id + 32-byte name), little-endian on disk
* Rows per page: **113** (page padding at the end is never read as data)
* All full-table scans, filters, duplicate checks, and index rebuilds use a single **page-aware row iterator**

### Index

* Hash index on `id` with chaining
* **Non-authoritative**: the table file is the source of truth
* Lookups validate offset bounds, row alignment, and key match; failures fall back to a table scan
* Missing, truncated, stale, or corrupt indexes are **rebuilt from the table**
* Persistence is atomic: write to `*.idx.tmp`, flush, `rename` over `*.idx`

### Uniqueness

`id` is a unique primary key. Duplicate inserts are rejected before modifying the table or index.

---

## On-disk file formats

Both formats use **little-endian** fixed-width fields. Native structs are not written with `fwrite(&struct, ...)`.

### Table file (`<name>.tbl`) — version 1

| Field         | Type   | Value                          |
|---------------|--------|--------------------------------|
| magic         | u32    | `0x544C5153` (`SQLT`)          |
| version       | u32    | `1`                            |
| header_size   | u32    | `64`                           |
| page_size     | u32    | `4096`                         |
| row_size      | u32    | `36`                           |
| reserved      | u32    | `0`                            |
| row_count     | u64    | number of logical rows         |
| padding       | bytes  | zero-filled to 64 bytes        |

Data begins at offset 64. Page `p` starts at `64 + p * 4096`. Slot `s` in a page is at `s * 36` (`0 <= s < 113`).

### Index file (`<name>.idx`) — version 1

| Field         | Type   | Value                          |
|---------------|--------|--------------------------------|
| magic         | u32    | `0x494C5153` (`SQLI`)          |
| version       | u32    | `1`                            |
| header_size   | u32    | `32`                           |
| entry_size    | u32    | `12`                           |
| entry_count   | u32    | number of entries              |
| reserved      | u32    | `0`                            |
| entries       | …      | `entry_count × (i32 key, i64 offset)` |

### Legacy compatibility

Files without the version-1 header (including earlier project dumps of raw rows or raw `IndexEntry` arrays) are **rejected** with an incompatible/corrupt format error. There is no automatic migration. Recreate tables with `create_table` and re-insert data.

---

## Build

```bash
make          # release-style build with strict warnings
make clean
make debug    # -g -O0
make test     # build and run unit tests
make asan     # AddressSanitizer + tests
make ubsan    # UndefinedBehaviorSanitizer + tests
make sanitize # ASan + UBSan + tests
```

Variables: `CC`, `CFLAGS`, `CPPFLAGS`, `LDFLAGS`.

Warning flags include `-Wall -Wextra -Wpedantic -Wshadow -Wconversion -Wstrict-prototypes -Wmissing-prototypes`. Header dependencies use `-MMD -MP`.

---

## Tests

```bash
make test
make sanitize
```

The suite covers tokenizer, parser (including incomplete and trailing SQL), semantic validation, page-boundary storage (113 / 114 / multi-page), index rebuild and corruption recovery, CLI oversized input, and case-insensitive keywords. Tests use isolated temporary directories.

CI (GitHub Actions) builds with GCC and Clang, runs tests, and runs sanitizers on every push and pull request.

**Note:** On some recent macOS / Apple Clang combinations, AddressSanitizer can hang during process startup (dyld + ASan init deadlock). Prefer `make test` and `make ubsan` locally on macOS; use Linux CI (or a Linux VM) for `make asan` / `make sanitize`.

---

## Known limitations

* Single fixed schema; no `CREATE TABLE` SQL DDL for custom columns
* No `UPDATE`, `DELETE`, joins, transactions, or aggregation
* No multi-statement scripts
* String literals: double quotes only, no escapes
* Identifiers limited to 63 characters; numbers/strings beyond that are tokenizer errors
* CLI line limit: 511 characters (`CLI_MAX_INPUT - 1`)
* Index is in-memory hash rebuilt/persisted as a flat file, not a B-tree
* Concurrent access is not supported

---

## License

MIT License — see [LICENSE](LICENSE).

## Author

**Aruj Singh**
