# Host build and tests for pc-containerd. The device build uses Android.bp.
#   make test     build and run every tests/test_*.c and tests/test_*.py
#   make          build the host binary (host trials: --socket PATH --root DIR)

CC ?= cc
CFLAGS ?= -O1 -g -Wall -Wextra -Werror -std=gnu11
CFLAGS += -Isrc
BUILD := build
CORE := $(filter-out src/main.c,$(wildcard src/*.c))
TESTS := $(patsubst tests/%.c,$(BUILD)/%,$(wildcard tests/test_*.c))

all: $(BUILD)/pc-containerd

$(BUILD):
	mkdir -p $@

$(BUILD)/pc-containerd: src/main.c $(CORE) $(wildcard src/*.h) | $(BUILD)
	$(CC) $(CFLAGS) -o $@ src/main.c $(CORE)

$(BUILD)/test_%: tests/test_%.c $(CORE) $(wildcard src/*.h) $(wildcard tests/*.h) | $(BUILD)
	$(CC) $(CFLAGS) -o $@ $< $(CORE)

test: $(TESTS) $(BUILD)/pc-containerd
	@set -e; for t in $(TESTS); do echo "== $$t"; $$t; done
	python3 -m unittest discover -s tests -p 'test_*.py'

clean:
	rm -rf $(BUILD)

.PHONY: all test clean
