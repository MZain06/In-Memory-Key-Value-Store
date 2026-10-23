# In-Memory Key-Value Store

[![CI](https://github.com/MZain06/In-Memory-Key-Value-Store/actions/workflows/ci.yml/badge.svg)](https://github.com/MZain06/In-Memory-Key-Value-Store/actions/workflows/ci.yml)

A hash-table key-value store in C11, in progress: separate chaining, dynamic resizing and an opaque public API, with a benchmark harness for throughput and p50/p99 latency.

**Status:** the hash table works and resizes automatically, with a documented API and an example program; randomised testing and benchmarks come next.

## Build and test

Needs Linux or WSL2 with `gcc` or `clang`, `make`, `valgrind`, `clang-format` and `cppcheck`.

| Command | What it does |
| --- | --- |
| `make test` | Builds and runs the unit tests |
| `make asan` | Runs the tests under AddressSanitizer and UBSan |
| `make valgrind` | Runs the tests under Valgrind |
| `make examples` | Builds and runs the example programs |
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

Unit tests use [Unity](https://github.com/ThrowTheSwitch/Unity) v2.7.0, vendored in `tests/unity/`.

## Licence

MIT
