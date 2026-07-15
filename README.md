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
* Numeric literals may be signed and must be in `[-2147483648, 2147483647]`
  (checked with `strtoimax` and complete-token validation)
* Table names and stored names are limited to 31 bytes and are rejected rather
  than silently truncated

### Fixed schema

Every table uses the same row layout:

| Column | Type                         | Notes                                      |
|--------|------------------------------|--------------------------------------------|
| `id`   | `int32`                      | Unique primary key                         |
| `name` | fixed string (`NAME_SIZE` 32)| Up to 31 bytes + NUL in memory             |
| `*`    | projection only              | Selects both columns                       |

Unsupported column names produce a **semantic error**.

### Meta commands

```text
create_table <name>
rebuild_index <name>
help
exit / quit
```

`create_table` reports whether the table was created or already existed; it
never truncates an existing table. Creation uses exclusive, no-follow semantics
where available and mode `0600`, so repeated or concurrent creation cannot both
succeed.

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

* Dynamically resized hash index on `id` with separate chaining
* A multi-step 32-bit mixer prevents the trivial low-byte collisions of the
  earlier fixed 256-bucket implementation
* Buckets double before the load factor exceeds 0.75
* **Non-authoritative**: the table file is the source of truth
* Lookups validate offset bounds, row alignment, and key match; failures fall back to a table scan
* The persisted entry count must match the validated table row count; missing,
  stale, truncated, or corrupt indexes are **rebuilt from the table**
* Persistence uses a randomly named mode-`0600` same-directory temporary file,
  `fsync`, atomic `rename`, and containing-directory `fsync`

### Uniqueness

`id` is a unique primary key. Duplicate inserts are rejected before modifying
the table or index. Rebuilding a table containing duplicate IDs fails and
reports a violated primary-key invariant; it never silently chooses one row.

### Durability and crash recovery

A successful INSERT means the row and updated table header have been flushed
and synchronized to stable storage, and removal of the rollback journal has
been synchronized in the containing directory. The commit sequence is:

1. Persist a checksummed rollback record through a secure temporary file,
   `fsync`, atomic rename to `<table>.tbl.journal`, and directory `fsync`.
2. Write and `fsync` the new row.
3. Write and `fsync` the updated table header.
4. Remove the journal and `fsync` the directory. This is the commit point.

On writable table open, a valid leftover journal means an INSERT did not reach
its durable commit point. Recovery restores the previous header, truncates the
uncommitted row, synchronizes the table, and then durably removes the journal.
It therefore exposes the old committed state or the new committed state, never
a partially initialized row. A malformed journal is treated as corruption. A
read-only open reports that recovery is required if a journal is present.

The index remains rebuildable and non-authoritative. Index loss never loses
table rows; if a SELECT cannot persist a missing index, it falls back to a
page-aware table scan.

---

## On-disk file formats

All formats use **little-endian** fixed-width fields. Native structs are not written with `fwrite(&struct, ...)`.

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

### Insert rollback journal (`<name>.tbl.journal`) — version 1

The fixed-width, little-endian journal is 128 bytes and contains magic and
version fields, the previous and intended row counts, intended row offset,
32-byte table identity, serialized 36-byte row, and an FNV-1a checksum. It
exists only while an INSERT is in progress or awaiting recovery.

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

The suite covers tokenizer and parser truncation, signed integer boundaries,
semantic validation, page-boundary storage, secure temporary files, symlink
attacks, insert fault recovery, dynamic hash resizing, adversarial IDs, index
corruption and duplicate detection, terminal escaping, read-only scans, CLI
oversized input, and case-insensitive keywords. Tests use isolated temporary
directories.

CI (GitHub Actions) builds with GCC and Clang, runs tests, and runs sanitizers on every push and pull request.

**Note:** On some recent macOS / Apple Clang combinations, AddressSanitizer can hang during process startup (dyld + ASan init deadlock). Prefer `make test` and `make ubsan` locally on macOS; use Linux CI (or a Linux VM) for `make asan` / `make sanitize`.

---

## Known limitations

* Single fixed schema; no `CREATE TABLE` SQL DDL for custom columns
* No `UPDATE`, `DELETE`, joins, multi-statement transactions, or aggregation;
  individual INSERT durability is journaled as described above
* No multi-statement scripts
* String literals: double quotes only, no escapes
* Table names, column identifiers, and stored names are limited to 31 bytes;
  longer input is rejected without modifying data
* CLI line limit: 511 characters (`CLI_MAX_INPUT - 1`)
* Index is in-memory hash rebuilt/persisted as a flat file, not a B-tree
* Concurrent access is not supported
* Stored names are terminal-escaped on output (`\n`, `\r`, `\t`, `\\`, `\"`,
  and `\xNN` for other control/non-printable bytes)

---

## License

MIT License — see [LICENSE](LICENSE).

## Author

**Aruj Singh**
