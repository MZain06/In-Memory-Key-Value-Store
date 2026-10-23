ifeq ($(origin CC),default)
CC := gcc
endif

CPPFLAGS := -Iinclude -Itests/unity -D_POSIX_C_SOURCE=200809L -MMD -MP
CFLAGS := -std=c11 -Wall -Wextra -Wpedantic -Werror
DEBUG_FLAGS := -g -O0
ASAN_FLAGS := -g -O1 -fno-omit-frame-pointer -fsanitize=address,undefined -fno-sanitize-recover=all

SRC := $(wildcard src/*.c)
TEST_SRC := $(wildcard tests/test_*.c)
EXAMPLE_SRC := $(wildcard examples/*.c)
UNITY_SRC := tests/unity/unity.c
STYLE_SRC := $(wildcard include/*.h src/*.c tests/*.c bench/*.c examples/*.c)
LINT_SRC := $(wildcard src/*.c tests/*.c bench/*.c examples/*.c)

DEBUG_DIR := build/debug
ASAN_DIR := build/asan

DEBUG_TESTS := $(patsubst tests/%.c,$(DEBUG_DIR)/bin/%,$(TEST_SRC))
ASAN_TESTS := $(patsubst tests/%.c,$(ASAN_DIR)/bin/%,$(TEST_SRC))
DEBUG_LIB := $(patsubst %.c,$(DEBUG_DIR)/%.o,$(SRC) $(UNITY_SRC))
ASAN_LIB := $(patsubst %.c,$(ASAN_DIR)/%.o,$(SRC) $(UNITY_SRC))
DEBUG_SRC_OBJ := $(patsubst %.c,$(DEBUG_DIR)/%.o,$(SRC))
EXAMPLES := $(patsubst examples/%.c,$(DEBUG_DIR)/bin/%,$(EXAMPLE_SRC))

VALGRIND := valgrind --quiet --leak-check=full --show-leak-kinds=all --errors-for-leak-kinds=all --error-exitcode=1
CPPCHECK := cppcheck --std=c11 --enable=warning,style,performance,portability --error-exitcode=1 --inline-suppr --quiet

.PHONY: all check test asan valgrind examples format format-check lint clean
.SECONDARY:

all: $(DEBUG_TESTS) $(EXAMPLES)

check: format-check lint test asan valgrind examples


test: $(DEBUG_TESTS)
	@for t in $^; do echo "== $$t"; $$t || exit 1; done

asan: $(ASAN_TESTS)
	@for t in $^; do echo "== $$t (ASan + UBSan)"; UBSAN_OPTIONS=print_stacktrace=1 $$t || exit 1; done

valgrind: $(DEBUG_TESTS)
	@for t in $^; do echo "== $$t (Valgrind)"; $(VALGRIND) $$t || exit 1; done

examples: $(EXAMPLES)
	@for e in $^; do echo "== $$e"; $$e || exit 1; done

format:
	clang-format -i $(STYLE_SRC)

format-check:
	clang-format --dry-run --Werror $(STYLE_SRC)

lint:
	$(CPPCHECK) -Iinclude $(LINT_SRC)

clean:
	rm -rf build

$(DEBUG_DIR)/tests/unity/%.o: tests/unity/%.c
	@mkdir -p $(@D)
	$(CC) $(CPPFLAGS) -std=c11 $(DEBUG_FLAGS) -c $< -o $@

$(ASAN_DIR)/tests/unity/%.o: tests/unity/%.c
	@mkdir -p $(@D)
	$(CC) $(CPPFLAGS) -std=c11 $(ASAN_FLAGS) -c $< -o $@

$(DEBUG_DIR)/%.o: %.c
	@mkdir -p $(@D)
	$(CC) $(CPPFLAGS) $(CFLAGS) $(DEBUG_FLAGS) -c $< -o $@

$(ASAN_DIR)/%.o: %.c
	@mkdir -p $(@D)
	$(CC) $(CPPFLAGS) $(CFLAGS) $(ASAN_FLAGS) -c $< -o $@

$(DEBUG_DIR)/bin/%: $(DEBUG_DIR)/tests/%.o $(DEBUG_LIB)
	@mkdir -p $(@D)
	$(CC) $(DEBUG_FLAGS) $^ -o $@

$(ASAN_DIR)/bin/%: $(ASAN_DIR)/tests/%.o $(ASAN_LIB)
	@mkdir -p $(@D)
	$(CC) $(ASAN_FLAGS) $^ -o $@

$(EXAMPLES): $(DEBUG_DIR)/bin/%: $(DEBUG_DIR)/examples/%.o $(DEBUG_SRC_OBJ)
	@mkdir -p $(@D)
	$(CC) $(DEBUG_FLAGS) $^ -o $@

-include $(shell find build -name '*.d' 2>/dev/null)
