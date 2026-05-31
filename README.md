# MyDB

A relational database built from scratch in C++20. Supports SQL queries, B+ tree indexing, disk-backed storage with buffer pool management, and a concurrent query executor with lock-free parallel operators.

## Features

- **SQL support** — `CREATE TABLE`, `INSERT`, `SELECT` with `WHERE`, `JOIN`, `GROUP BY`, `ORDER BY`, and aggregate functions (`COUNT`, `SUM`, `AVG`, `MIN`, `MAX`)
- **B+ tree indexing** — ordered index for fast point lookups and range scans
- **Buffer pool** — LRU page cache with concurrent access via atomic pin counts
- **Parallel query execution** — Volcano-style iterator model with multi-threaded scan, join, and sort operators
- **Lock-free concurrency** — lock-free hash table for concurrent hash join builds, atomic page partitioning for parallel scans, lock-free k-way merge for parallel sort
- **Write-ahead logging** — WAL for crash recovery
- **Query optimizer** — rule-based optimizer with predicate pushdown and index scan selection
- **Interactive REPL** — SQL shell with formatted output, execution timing, and `EXPLAIN` plans

## Architecture

```
┌─────────────────────────────────────────────────────────┐
│                     REPL / CLI                          │
│              SQL input → formatted results              │
├─────────────────────────────────────────────────────────┤
│                    SQL Parser                           │
│           Lexer → Recursive Descent → AST               │
├─────────────────────────────────────────────────────────┤
│               Query Planner / Optimizer                 │
│     AST → Logical Plan → Predicate Pushdown →           │
│           Index Selection → Physical Plan               │
├─────────────────────────────────────────────────────────┤
│                  Query Executor                         │
│         Volcano Iterators (Init / Next / Close)         │
│                                                         │
│  ┌──────────┐ ┌───────────┐ ┌──────┐ ┌───────────────┐ │
│  │ Par. Scan│ │ Hash Join │ │ Sort │ │ Hash Aggregate│ │
│  │ (atomic  │ │(lock-free │ │(par. │ │  (GROUP BY +  │ │
│  │  page    │ │  build +  │ │merge)│ │   aggregates) │ │
│  │ counter) │ │  conc.    │ │      │ │               │ │
│  │          │ │  probe)   │ │      │ │               │ │
│  └──────────┘ └───────────┘ └──────┘ └───────────────┘ │
│                    Thread Pool                          │
│             (lock-free work queue)                      │
├─────────────────────────────────────────────────────────┤
│                  Storage Engine                         │
│                                                         │
│  ┌────────────┐  ┌──────────┐  ┌──────┐  ┌───────────┐ │
│  │ Buffer Pool│  │ Heap File│  │B+Tree│  │    WAL    │ │
│  │  (LRU +   │  │ (tuples  │  │Index │  │(append-   │ │
│  │  atomic   │  │  in 4KB  │  │      │  │ only log) │ │
│  │  pins)    │  │  pages)  │  │      │  │           │ │
│  └─────┬──────┘  └──────────┘  └──────┘  └───────────┘ │
│        │                                                │
│  ┌─────▼──────┐                                         │
│  │Disk Manager│  ← 4KB page read/write                  │
│  └────────────┘                                         │
└─────────────────────────────────────────────────────────┘
```

## Concurrency model

The executor parallelizes query operators across a thread pool. Shared state is managed without mutexes:

| Component | Technique | Details |
|-----------|-----------|---------|
| **Parallel scan** | Atomic page counter | Worker threads atomically claim page ranges — no coordination needed |
| **Hash join (build)** | Lock-free hash table | Open addressing with Robin Hood probing, CAS-based insertion |
| **Hash join (probe)** | Concurrent reads | Read-only probe phase is naturally thread-safe |
| **Parallel sort** | Lock-free merge | Each thread sorts a partition, then a lock-free k-way merge combines runs |
| **Buffer pool** | Atomic pin counts | Pages use atomic reference counting for concurrent access |
| **Task distribution** | Lock-free work queue | Thread pool pulls tasks from a shared MPMC queue using CAS |

All hot-path shared state is cache-line aligned (`alignas(64)`) to prevent false sharing. Memory ordering uses `acquire`/`release` semantics where possible instead of `seq_cst`.

## Benchmarks

<!-- TODO: Replace with actual benchmark results -->

Tested on a table with 10M rows (4 integer columns, ~160MB on disk).

| Operation | 1 thread | 4 threads | 8 threads | 16 threads |
|-----------|----------|-----------|-----------|------------|
| Sequential scan | — ms | — ms | — ms | — ms |
| Hash join (1M × 100K) | — ms | — ms | — ms | — ms |
| Sort (10M rows) | — ms | — ms | — ms | — ms |

<!-- TODO: Add speedup chart image -->

## Example session

```sql
mydb> CREATE TABLE users (id INT, name VARCHAR(255), age INT);
Table 'users' created.

mydb> INSERT INTO users VALUES (1, 'Alice', 28);
Inserted 1 row.

mydb> INSERT INTO users VALUES (2, 'Bob', 34);
Inserted 1 row.

mydb> SELECT name, age FROM users WHERE age > 30;
+------+-----+
| name | age |
+------+-----+
| Bob  |  34 |
+------+-----+
1 row (0.02 ms)

mydb> CREATE TABLE orders (id INT, user_id INT, amount INT);
Table 'orders' created.

mydb> SELECT u.name, SUM(o.amount)
    ...> FROM users u JOIN orders o ON u.id = o.user_id
    ...> GROUP BY u.name
    ...> ORDER BY u.name;
+-------+------------+
| name  | SUM(amount)|
+-------+------------+
| Alice |        450 |
| Bob   |        320 |
+-------+------------+
2 rows (1.34 ms)

mydb> EXPLAIN SELECT name FROM users WHERE age > 25;
Projection [name]
  └─ Filter [age > 25]
      └─ Sequential Scan [users]
```

## Build

```bash
# Prerequisites: CMake 3.20+, C++20 compiler (GCC 12+ or Clang 15+)

mkdir build && cd build
cmake .. -DCMAKE_BUILD_TYPE=Release
make -j$(nproc)

# Run
./mydb

# Tests
./mydb_test

# Benchmarks
./mydb_bench
```

## Project structure

```
mydb/
├── src/
│   ├── storage/
│   │   ├── disk_manager.cpp/.h      # 4KB page I/O
│   │   ├── buffer_pool.cpp/.h       # LRU cache + atomic pins
│   │   ├── heap_file.cpp/.h         # tuple storage
│   │   ├── bplus_tree.cpp/.h        # ordered index
│   │   └── wal.cpp/.h               # write-ahead log
│   ├── parser/
│   │   ├── lexer.cpp/.h             # tokenizer
│   │   ├── parser.cpp/.h            # recursive descent
│   │   └── ast.h                    # syntax tree nodes
│   ├── planner/
│   │   ├── logical_plan.cpp/.h      # relational algebra tree
│   │   └── optimizer.cpp/.h         # predicate pushdown, index selection
│   ├── executor/
│   │   ├── executor.h               # Init/Next/Close interface
│   │   ├── seq_scan.cpp/.h          # parallel variant included
│   │   ├── index_scan.cpp/.h        # B+ tree scan
│   │   ├── filter.cpp/.h            # predicate evaluation
│   │   ├── projection.cpp/.h        # column selection
│   │   ├── hash_join.cpp/.h         # lock-free build + concurrent probe
│   │   ├── sort.cpp/.h              # parallel sort-merge
│   │   ├── aggregate.cpp/.h         # GROUP BY + aggregate functions
│   │   └── thread_pool.cpp/.h       # lock-free MPMC work queue
│   ├── concurrency/
│   │   ├── lockfree_hashtable.cpp/.h
│   │   └── lockfree_queue.cpp/.h
│   └── main.cpp                     # REPL
├── test/
│   ├── storage_test.cpp
│   ├── parser_test.cpp
│   ├── executor_test.cpp
│   └── concurrency_test.cpp
├── bench/
│   └── query_bench.cpp
├── CMakeLists.txt
└── README.md
```

## Design decisions

- **Hand-written parser** over parser generators — demonstrates parsing fundamentals and keeps the dependency footprint at zero
- **Volcano iterator model** over materialization — memory-efficient (tuples flow one at a time), and the iterator interface naturally extends to parallel operators
- **Lock-free hash table** over concurrent `std::unordered_map` + mutex — eliminates contention in the hash join build phase, which is the bottleneck for join-heavy queries
- **B+ tree** over hash index — supports both point queries and range scans, which a hash index cannot
- **Fixed 4KB pages** — matches OS page size for optimal I/O, standard in real databases (PostgreSQL, SQLite, MySQL)

## References

- [CMU 15-445: Database Systems](https://15445.courses.cs.cmu.edu/) — Andy Pavlo's course. Architecture mirrors BusTub.
- [Database Internals](https://www.databass.dev/) — Alex Petrov. B+ tree and storage engine design.
- [The Art of Multiprocessor Programming](https://www.oreilly.com/library/view/the-art-of/9780123705914/) — Lock-free data structure theory.
