# In-Memory Key-Value Store

[![CI](https://github.com/MZain06/In-Memory-Key-Value-Store/actions/workflows/ci.yml/badge.svg)](https://github.com/MZain06/In-Memory-Key-Value-Store/actions/workflows/ci.yml)

A hash-table key-value store in C11, in progress: separate chaining, dynamic resizing and an opaque public API, with a benchmark harness for throughput and p50/p99 latency.

**Status:** v0.1.0, a complete and tested hash-table library; the benchmark harness comes next.

## Build and test

Needs Linux or WSL2 with `gcc` or `clang`, `make`, `valgrind`, `clang-format` and `cppcheck`.

| Command | What it does |
| --- | --- |
| `make check` | Runs every check that CI runs |
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

## Testing

- `tests/test_kv.c`: unit tests for every API function, covering edge cases (empty keys and values, zero bytes inside values, 1 KB keys), long chains, resizing and pointer stability.
- `tests/test_model.c`: a randomised model-based test. It runs 3 million random puts, gets and deletes against a simple array model and checks every result. One run alternates filling and emptying phases so the table resizes over 200 times; another uses a load factor of 4 to force long chains.

The random test uses a fixed seed (42), so every run is reproducible. A failure names the operation and the seed. After `make test`, replay a seed or try a new one with:

```bash
./build/debug/bin/test_model 1234
```

CI runs every test with gcc and clang, under AddressSanitizer and UBSan, and under Valgrind. Tests use [Unity](https://github.com/ThrowTheSwitch/Unity) v2.7.0, vendored in `tests/unity/`.

## Licence

MIT
