# In-Memory Key-Value Store

[![CI](https://github.com/MZain06/In-Memory-Key-Value-Store/actions/workflows/ci.yml/badge.svg)](https://github.com/MZain06/In-Memory-Key-Value-Store/actions/workflows/ci.yml)

A hash-table key-value store in C11, in progress: separate chaining, dynamic resizing and an opaque public API, with a benchmark harness for throughput and p50/p99 latency.

**Status:** the build, test and CI foundations are in place; the hash table comes next.

## Build and test

Needs Linux or WSL2 with `gcc` or `clang`, `make`, `valgrind`, `clang-format` and `cppcheck`.

| Command | What it does |
| --- | --- |
| `make test` | Builds and runs the unit tests |
| `make asan` | Runs the tests under AddressSanitizer and UBSan |
| `make valgrind` | Runs the tests under Valgrind |
| `make format` | Formats the code with clang-format |
| `make format-check` | Fails if any file is not formatted |
| `make lint` | Runs cppcheck static analysis |
| `make clean` | Deletes the `build/` folder |

Use `make CC=clang test` to build with clang instead of gcc.

Unit tests use [Unity](https://github.com/ThrowTheSwitch/Unity) v2.7.0, vendored in `tests/unity/`.

## Licence

MIT
