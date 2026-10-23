# In-Memory Key-Value Store

[![CI](https://github.com/MZain06/In-Memory-Key-Value-Store/actions/workflows/ci.yml/badge.svg)](https://github.com/MZain06/In-Memory-Key-Value-Store/actions/workflows/ci.yml)

A hash-table key-value store in C11, in progress: separate chaining, dynamic resizing and an opaque public API, with a benchmark harness for throughput and p50/p99 latency.

**Status:** v0.2.0, the tested hash-table library plus a benchmark harness; measured results come next.

## Build and test

Needs Linux or WSL2 with `gcc` or `clang`, `make`, `valgrind`, `clang-format` and `cppcheck`.

| Command | What it does |
| --- | --- |
| `make check` | Runs every check that CI runs |
| `make test` | Builds and runs the unit tests |
| `make asan` | Runs the tests under AddressSanitizer and UBSan |
| `make valgrind` | Runs the tests under Valgrind |
| `make examples` | Builds and runs the example programs |
| `make bench` | Builds the benchmark with `-O2 -DNDEBUG` |
| `make bench-smoke` | Runs a tiny benchmark, normally and under Valgrind |
| `make format` | Formats the code with clang-format |
| `make format-check` | Fails if any file is not formatted |
| `make lint` | Runs cppcheck static analysis |
| `make clean` | Deletes the `build/` folder |

Use `make CC=clang test` to build with clang instead of gcc.

## Using the API

```c
#include "kv.h"

kv_store *store = kv_create(NULL);
kv_put(store, "language", "C11", 3);

const void *value;
size_t len;
if (kv_get(store, "language", &value, &len) == KV_OK) {
    printf("%.*s\n", (int)len, (const char *)value);
}

kv_destroy(store);
```

`include/kv.h` documents the full contract, and `examples/basic.c` is a complete program.

## Testing

- `tests/test_kv.c`: unit tests for every API function, covering edge cases (empty keys and values, zero bytes inside values, 1 KB keys), long chains, resizing and pointer stability.
- `tests/test_model.c`: a randomised model-based test. It runs 3 million random puts, gets and deletes against a simple array model and checks every result. One run alternates filling and emptying phases so the table resizes over 200 times; another uses a load factor of 4 to force long chains.

The random test uses a fixed seed (42), so every run is reproducible. A failure names the operation and the seed. After `make test`, replay a seed or try a new one with:

```bash
./build/debug/bin/test_model 1234
```

CI runs every test with gcc and clang, under AddressSanitizer and UBSan, and under Valgrind. Tests use [Unity](https://github.com/ThrowTheSwitch/Unity) v2.7.0, vendored in `tests/unity/`.

## Benchmarks

`bench/bench.c` measures four workloads at 10,000, 100,000 and 1,000,000 keys:

| Workload | What it measures |
| --- | --- |
| `insert` | Building a table from empty, including every resize |
| `get_hit` | Looking up keys that exist, in random order, and reading the value |
| `get_miss` | Looking up keys that don't exist |
| `mixed` | 90% lookups and 10% overwrites of existing keys |

Each configuration runs once to warm up, then 5 measured runs. Every run builds its own table and makes two passes over the same 5 million operations: one timed as a whole for throughput, and one that times each operation for p50, p99 and p99.9 latency. Keys and lookup orders come from a fixed seed and are generated before timing starts, every page of memory is touched beforehand, and every pass checks its own results. The cost of the timer itself is measured at startup and recorded in every row.

```bash
./scripts/run_bench.sh
python3 scripts/compare.py results/results.csv
```

The script builds with `-O2 -DNDEBUG`, pins the benchmark to one CPU, records the machine in `results/env.txt`, and adds a load-factor sweep (0.25 to 4) at 1M keys. `compare.py` summarises one results file, or compares the medians of two.

## Licence

MIT
