![License: MIT](https://img.shields.io/badge/License-MIT-yellow.svg)

# ⚙️ Mini SQL Engine in C

A lightweight, modular SQL database engine built from scratch in C.

This project implements core database internals including:

* Page-based storage engine
* SQL parsing (tokenizer + AST)
* Query execution engine
* Hash indexing for fast lookups

Designed as a systems-level project to understand how real databases like SQLite and PostgreSQL work under the hood.

---

## 🚀 Features

### 🧱 Storage Engine

* File-based storage (`.tbl` files)
* Fixed-size page system (4KB pages)
* Manual row serialization (no struct padding issues)
* Full table scan support

### 🧠 SQL Engine

* Custom tokenizer and parser (no external libraries)
* Abstract Syntax Tree (AST) generation
* Execution pipeline:

  ```
  Query → Tokens → AST → Execution → Result
  ```

### 🔍 Supported Queries

```sql
INSERT INTO users VALUES (1, "Aruj");
SELECT * FROM users;
SELECT name FROM users;
SELECT * FROM users WHERE id = 1;
```

### ⚡ Indexing

* Hash index on `id` column
* O(1) average lookup time
* Collision handling using chaining
* Automatic index updates on insert

### 🖥 CLI Interface

* Interactive shell
* Simple command execution
* Tabular output formatting

---


## 🧠 How It Works

### 1. Storage Layer

* Tables are stored as `.tbl` files
* Data is organized into fixed-size pages (4096 bytes)
* Rows are serialized manually to ensure consistent layout

---

### 2. Query Processing Pipeline

```
SQL Query
   ↓
Tokenizer → breaks query into tokens
   ↓
Parser → builds AST
   ↓
Executor → runs query on storage engine
   ↓
Result
```

---

### 3. Index Optimization

* For queries like:

  ```sql
  SELECT * FROM users WHERE id = 3;
  ```
* Engine uses hash index instead of scanning full table

---

## 🛠 Build & Run

### Compile

```bash
make
```

### Run

```bash
./mini_sql
```

---

## 💻 Example Session

```
> create_table users
> insert users 1 Aruj
> insert users 2 Rahul
> select users

1 Aruj
2 Rahul
```

---

## 🧪 Testing

* Insert multiple rows across page boundaries
* Restart program to verify persistence
* Run indexed queries to validate performance

---

## ⚠️ Limitations

* No JOIN support
* No transactions or concurrency control
* Limited SQL grammar
* Fixed schema (id, name)

---

## 🔮 Future Improvements

* B-Tree indexing
* JOIN support
* ORDER BY / LIMIT
* Transaction system (WAL)
* Variable-length records
* Query planner

---

## 🧠 What I Learned

* How databases manage disk storage using pages
* Manual memory management in C
* Building a tokenizer and parser from scratch
* Designing execution pipelines
* Implementing indexing for performance

---

## 📌 Why This Project?

Most DBMS projects are CRUD apps using existing databases.

This project goes deeper:

> building a database engine itself.

---

## 📜 License

MIT License

---

## 👤 Author

**Aruj Singh**

---
