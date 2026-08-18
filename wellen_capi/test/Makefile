REPO_ROOT := $(abspath ../..)
TARGET_DIR := $(REPO_ROOT)/target/debug
TEST_BIN := $(CURDIR)/test_capi
FIXTURE ?= $(REPO_ROOT)/wellen/inputs/scope_with_comment.vcd.fst

.PHONY: all build test clean

all: test

build:
	cargo build --manifest-path $(REPO_ROOT)/Cargo.toml -p wellen-capi
	$(CC) -std=c11 -Wall -Wextra -Werror \
		-I$(REPO_ROOT)/wellen_capi/include \
		$(CURDIR)/test_capi.c \
		-L$(TARGET_DIR) -Wl,-rpath,$(TARGET_DIR) -lwellen_capi \
		-o $(TEST_BIN)

test: build
	$(TEST_BIN) $(FIXTURE)

clean:
	$(RM) $(TEST_BIN)
