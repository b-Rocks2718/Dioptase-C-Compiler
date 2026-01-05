# Variables
CC 				:= gcc
CFLAGS_COMMON ?= -Wall
OPT_DEBUG := -O0
OPT_RELEASE := -O3
DEBUG_INFO := -g
CFLAGS_DEBUG ?= $(CFLAGS_COMMON) $(OPT_DEBUG) $(DEBUG_INFO)
CFLAGS_RELEASE ?= $(CFLAGS_COMMON) $(OPT_RELEASE)
LDFLAGS_DEBUG ?=
LDFLAGS_RELEASE ?=

SRC_DIR		:= src
BUILD_ROOT := build
DEBUG_DIR := $(BUILD_ROOT)/debug
RELEASE_DIR := $(BUILD_ROOT)/release
DEBUG_OBJ_DIR := $(DEBUG_DIR)/objfiles
RELEASE_OBJ_DIR := $(RELEASE_DIR)/objfiles
TEST_PREPROCESS_DIR := tests/preprocess
TEST_PREPROCESS_INVALID_DIR := tests/preprocess_invalid
TEST_LEXER_DIR := tests/lexer
TEST_LEXER_INVALID_DIR := tests/lexer_invalid
TEST_PARSER_DIR := tests/parser
TEST_PARSER_INVALID_DIR := tests/parser_invalid
TEST_IDENTS_DIR := tests/idents
TEST_IDENTS_INVALID_DIR := tests/idents_invalid
TEST_LABELS_DIR := tests/labels
TEST_LABELS_INVALID_DIR := tests/labels_invalid
TEST_TYPES_DIR := tests/types
TEST_TYPES_INVALID_DIR := tests/types_invalid
TAC_INTERP_TESTS := tac_interpreter
TAC_INTERP_TEST_SRC := tests/tac_interpreter_tests.c
TAC_EXEC_TESTS := tac_exec
TAC_EXEC_TEST_SRC := tests/tac_exec_tests.c

C_SRCS 		:= $(wildcard $(SRC_DIR)/*.c)
DEBUG_OBJFILES 	:= $(patsubst %.c, $(DEBUG_OBJ_DIR)/%.o, $(notdir $(C_SRCS)))
RELEASE_OBJFILES 	:= $(patsubst %.c, $(RELEASE_OBJ_DIR)/%.o, $(notdir $(C_SRCS)))
DEBUG_OBJFILES_NO_MAIN := $(filter-out $(DEBUG_OBJ_DIR)/main.o,$(DEBUG_OBJFILES))
RELEASE_OBJFILES_NO_MAIN := $(filter-out $(RELEASE_OBJ_DIR)/main.o,$(RELEASE_OBJFILES))
TAC_INTERP_TEST_OBJ_DEBUG := $(DEBUG_OBJ_DIR)/tac_interpreter_tests.o
TAC_INTERP_TEST_OBJ_RELEASE := $(RELEASE_OBJ_DIR)/tac_interpreter_tests.o
TAC_INTERP_TEST_EXEC_DEBUG := $(DEBUG_DIR)/tac_interpreter_tests
TAC_INTERP_TEST_EXEC_RELEASE := $(RELEASE_DIR)/tac_interpreter_tests
TAC_EXEC_TEST_OBJ_DEBUG := $(DEBUG_OBJ_DIR)/tac_exec_tests.o
TAC_EXEC_TEST_OBJ_RELEASE := $(RELEASE_OBJ_DIR)/tac_exec_tests.o
TAC_EXEC_TEST_EXEC_DEBUG := $(DEBUG_DIR)/tac_exec_tests
TAC_EXEC_TEST_EXEC_RELEASE := $(RELEASE_DIR)/tac_exec_tests

EXECUTABLE 			:= bcc
DEBUG_EXEC := $(DEBUG_DIR)/$(EXECUTABLE)
RELEASE_EXEC := $(RELEASE_DIR)/$(EXECUTABLE)
PREPROCESS_SRCS := $(wildcard $(TEST_PREPROCESS_DIR)/*.c)
PREPROCESS_TESTS := $(patsubst $(TEST_PREPROCESS_DIR)/%.c,%, $(PREPROCESS_SRCS))
PREPROCESS_INVALID_SRCS := $(wildcard $(TEST_PREPROCESS_INVALID_DIR)/*.c)
PREPROCESS_INVALID_TESTS := $(patsubst $(TEST_PREPROCESS_INVALID_DIR)/%.c,%, $(PREPROCESS_INVALID_SRCS))
LEXER_SRCS := $(wildcard $(TEST_LEXER_DIR)/*.c)
LEXER_TESTS := $(patsubst $(TEST_LEXER_DIR)/%.c,%, $(LEXER_SRCS))
LEXER_INVALID_SRCS := $(wildcard $(TEST_LEXER_INVALID_DIR)/*.c)
LEXER_INVALID_TESTS := $(patsubst $(TEST_LEXER_INVALID_DIR)/%.c,%, $(LEXER_INVALID_SRCS))
PARSER_SRCS := $(wildcard $(TEST_PARSER_DIR)/*.c)
PARSER_TESTS := $(patsubst $(TEST_PARSER_DIR)/%.c,%, $(PARSER_SRCS))
PARSER_INVALID_SRCS := $(wildcard $(TEST_PARSER_INVALID_DIR)/*.c)
PARSER_INVALID_TESTS := $(patsubst $(TEST_PARSER_INVALID_DIR)/%.c,%, $(PARSER_INVALID_SRCS))
IDENTS_SRCS := $(wildcard $(TEST_IDENTS_DIR)/*.c)
IDENTS_TESTS := $(patsubst $(TEST_IDENTS_DIR)/%.c,%, $(IDENTS_SRCS))
IDENTS_INVALID_SRCS := $(wildcard $(TEST_IDENTS_INVALID_DIR)/*.c)
IDENTS_INVALID_TESTS := $(patsubst $(TEST_IDENTS_INVALID_DIR)/%.c,%, $(IDENTS_INVALID_SRCS))
LABELS_SRCS := $(wildcard $(TEST_LABELS_DIR)/*.c)
LABELS_TESTS := $(patsubst $(TEST_LABELS_DIR)/%.c,%, $(LABELS_SRCS))
LABELS_INVALID_SRCS := $(wildcard $(TEST_LABELS_INVALID_DIR)/*.c)
LABELS_INVALID_TESTS := $(patsubst $(TEST_LABELS_INVALID_DIR)/%.c,%, $(LABELS_INVALID_SRCS))
TYPES_SRCS := $(wildcard $(TEST_TYPES_DIR)/*.c)
TYPES_TESTS := $(patsubst $(TEST_TYPES_DIR)/%.c,%, $(TYPES_SRCS))
TYPES_INVALID_SRCS := $(wildcard $(TEST_TYPES_INVALID_DIR)/*.c)
TYPES_INVALID_TESTS := $(patsubst $(TEST_TYPES_INVALID_DIR)/%.c,%, $(TYPES_INVALID_SRCS))

# Rule to build the main executable
all: debug

debug: $(DEBUG_EXEC)

release: $(RELEASE_EXEC)

$(DEBUG_EXEC): $(DEBUG_OBJFILES) | dirs-debug
	$(CC) $(CFLAGS_DEBUG) $(LDFLAGS_DEBUG) -o $@ $(DEBUG_OBJFILES)

$(RELEASE_EXEC): $(RELEASE_OBJFILES) | dirs-release
	$(CC) $(CFLAGS_RELEASE) $(LDFLAGS_RELEASE) -o $@ $(RELEASE_OBJFILES)

# Rule to generate .o files from .c files
$(DEBUG_OBJ_DIR)/%.o: $(SRC_DIR)/%.c | dirs-debug
	$(CC) $(CFLAGS_DEBUG) -c $< -o $@

$(RELEASE_OBJ_DIR)/%.o: $(SRC_DIR)/%.c | dirs-release
	$(CC) $(CFLAGS_RELEASE) -c $< -o $@

dirs-debug:
	@mkdir -p $(DEBUG_OBJ_DIR) $(DEBUG_DIR)

dirs-release:
	@mkdir -p $(RELEASE_OBJ_DIR) $(RELEASE_DIR)

# Rule to clean up generated files
clean:
	rm -f $(DEBUG_OBJFILES) $(RELEASE_OBJFILES) $(DEBUG_EXEC) $(RELEASE_EXEC) \
		$(TAC_INTERP_TEST_OBJ_DEBUG) $(TAC_INTERP_TEST_OBJ_RELEASE) \
		$(TAC_INTERP_TEST_EXEC_DEBUG) $(TAC_INTERP_TEST_EXEC_RELEASE) \
		$(TAC_EXEC_TEST_OBJ_DEBUG) $(TAC_EXEC_TEST_OBJ_RELEASE) \
		$(TAC_EXEC_TEST_EXEC_DEBUG) $(TAC_EXEC_TEST_EXEC_RELEASE) \
		$(TEST_PREPROCESS_DIR)/*.out \
		$(TEST_PREPROCESS_INVALID_DIR)/*.out \
		$(TEST_LEXER_DIR)/*.out \
		$(TEST_LEXER_INVALID_DIR)/*.out \
		$(TEST_PARSER_DIR)/*.out \
		$(TEST_PARSER_INVALID_DIR)/*.out \
		$(TEST_IDENTS_DIR)/*.out \
		$(TEST_IDENTS_INVALID_DIR)/*.out \
		$(TEST_LABELS_DIR)/*.out \
		$(TEST_LABELS_INVALID_DIR)/*.out \
		$(TEST_TYPES_DIR)/*.out \
		$(TEST_TYPES_INVALID_DIR)/*.out
	rm -rf $(BUILD_ROOT)/tac_exec

# Rule to clean up generated files
purge:
	rm -rf $(BUILD_ROOT)

define RUN_TESTS
	@GREEN="\033[0;32m"; \
	RED="\033[0;31m"; \
	NC="\033[0m"; \
	passed=0; total=$$(( $(words $(PREPROCESS_TESTS)) + $(words $(PREPROCESS_INVALID_TESTS)) + $(words $(LEXER_TESTS)) + $(words $(LEXER_INVALID_TESTS)) + $(words $(PARSER_TESTS)) + $(words $(PARSER_INVALID_TESTS)) + $(words $(IDENTS_TESTS)) + $(words $(IDENTS_INVALID_TESTS)) + $(words $(LABELS_TESTS)) + $(words $(LABELS_INVALID_TESTS)) + $(words $(TYPES_TESTS)) + $(words $(TYPES_INVALID_TESTS)) + $(words $(TAC_INTERP_TESTS)) + $(words $(TAC_EXEC_TESTS)) )); \
	echo "Running $(words $(PREPROCESS_TESTS)) preprocess tests:"; \
	for t in $(PREPROCESS_TESTS); do \
		printf "%s %-20s " '-' "$$t"; \
		out="$(TEST_PREPROCESS_DIR)/$$t.out"; \
		flags_file="$(TEST_PREPROCESS_DIR)/$$t.flags"; \
		flags=""; \
		if [ -f "$$flags_file" ]; then flags="$$(cat "$$flags_file")"; fi; \
		if $(TEST_EXEC) -preprocess $$flags "$(TEST_PREPROCESS_DIR)/$$t.c" > "$$out" 2>/dev/null; then \
			if diff -u "$(TEST_PREPROCESS_DIR)/$$t.ok" "$$out" >/dev/null 2>&1; then \
				echo "$$GREEN PASS $$NC"; passed=$$((passed+1)); \
			else \
				echo "$$RED FAIL $$NC"; \
			fi; \
		else \
			echo "$$RED FAIL $$NC"; \
		fi; \
	done; \
	echo "\nRunning $(words $(PREPROCESS_INVALID_TESTS)) invalid preprocess tests:"; \
	for t in $(PREPROCESS_INVALID_TESTS); do \
		printf "%s %-20s " '-' "$$t"; \
		out="$(TEST_PREPROCESS_INVALID_DIR)/$$t.out"; \
		flags_file="$(TEST_PREPROCESS_INVALID_DIR)/$$t.flags"; \
		flags=""; \
		if [ -f "$$flags_file" ]; then flags="$$(cat "$$flags_file")"; fi; \
		if $(TEST_EXEC) -preprocess $$flags "$(TEST_PREPROCESS_INVALID_DIR)/$$t.c" > "$$out" 2>&1; then \
			echo "$$RED FAIL $$NC"; \
		else \
			if [ -f "$(TEST_PREPROCESS_INVALID_DIR)/$$t.ok" ]; then \
				if diff -u "$(TEST_PREPROCESS_INVALID_DIR)/$$t.ok" "$$out" >/dev/null 2>&1; then \
					echo "$$GREEN PASS $$NC"; passed=$$((passed+1)); \
				else \
					echo "$$RED FAIL $$NC"; \
				fi; \
			else \
				echo "$$GREEN PASS $$NC"; passed=$$((passed+1)); \
			fi; \
		fi; \
	done; \
	echo "\nRunning $(words $(LEXER_TESTS)) lexer tests:"; \
	for t in $(LEXER_TESTS); do \
		printf "%s %-20s " '-' "$$t"; \
		out="$(TEST_LEXER_DIR)/$$t.out"; \
		$(TEST_EXEC) -tokens "$(TEST_LEXER_DIR)/$$t.c" > "$$out" 2>/dev/null; \
		if diff -u "$(TEST_LEXER_DIR)/$$t.ok" "$$out" >/dev/null 2>&1; then \
			echo "$$GREEN PASS $$NC"; passed=$$((passed+1)); \
		else \
			echo "$$RED FAIL $$NC"; \
		fi; \
	done; \
	echo "\nRunning $(words $(LEXER_INVALID_TESTS)) invalid lexer tests:"; \
	for t in $(LEXER_INVALID_TESTS); do \
		printf "%s %-20s " '-' "$$t"; \
		out="$(TEST_LEXER_INVALID_DIR)/$$t.out"; \
		if $(TEST_EXEC) -tokens "$(TEST_LEXER_INVALID_DIR)/$$t.c" > "$$out" 2>/dev/null; then \
			echo "$$RED FAIL $$NC"; \
		else \
			if [ -f "$(TEST_LEXER_INVALID_DIR)/$$t.ok" ]; then \
				if diff -u "$(TEST_LEXER_INVALID_DIR)/$$t.ok" "$$out" >/dev/null 2>&1; then \
					echo "$$GREEN PASS $$NC"; passed=$$((passed+1)); \
				else \
					echo "$$RED FAIL $$NC"; \
				fi; \
			else \
				echo "$$GREEN PASS $$NC"; passed=$$((passed+1)); \
			fi; \
		fi; \
	done; \
	echo "\nRunning $(words $(PARSER_TESTS)) parser tests:"; \
	for t in $(PARSER_TESTS); do \
		printf "%s %-20s " '-' "$$t"; \
		out="$(TEST_PARSER_DIR)/$$t.out"; \
		$(TEST_EXEC) -ast "$(TEST_PARSER_DIR)/$$t.c" > "$$out" 2>/dev/null; \
			if diff -u "$(TEST_PARSER_DIR)/$$t.ok" "$$out" >/dev/null 2>&1; then \
				echo "$$GREEN PASS $$NC"; passed=$$((passed+1)); \
			else \
				echo "$$RED FAIL $$NC"; \
			fi; \
	done; \
	echo "\nRunning $(words $(PARSER_INVALID_TESTS)) invalid parser tests:"; \
	for t in $(PARSER_INVALID_TESTS); do \
		printf "%s %-20s " '-' "$$t"; \
		out="$(TEST_PARSER_INVALID_DIR)/$$t.out"; \
		if $(TEST_EXEC) -ast "$(TEST_PARSER_INVALID_DIR)/$$t.c" > "$$out" 2>&1; then \
			echo "$$RED FAIL $$NC"; \
		else \
			if [ -f "$(TEST_PARSER_INVALID_DIR)/$$t.ok" ]; then \
				if diff -u "$(TEST_PARSER_INVALID_DIR)/$$t.ok" "$$out" >/dev/null 2>&1; then \
					echo "$$GREEN PASS $$NC"; passed=$$((passed+1)); \
				else \
					echo "$$RED FAIL $$NC"; \
				fi; \
			else \
				echo "$$GREEN PASS $$NC"; passed=$$((passed+1)); \
			fi; \
		fi; \
	done; \
	echo "\nRunning $(words $(IDENTS_TESTS)) identifier resolution tests:"; \
	for t in $(IDENTS_TESTS); do \
		printf "%s %-20s " '-' "$$t"; \
		out="$(TEST_IDENTS_DIR)/$$t.out"; \
		if $(TEST_EXEC) -idents "$(TEST_IDENTS_DIR)/$$t.c" > "$$out" 2>/dev/null; then \
			if diff -u "$(TEST_IDENTS_DIR)/$$t.ok" "$$out" >/dev/null 2>&1; then \
				echo "$$GREEN PASS $$NC"; passed=$$((passed+1)); \
			else \
				echo "$$RED FAIL $$NC"; \
			fi; \
		else \
			echo "$$RED FAIL $$NC"; \
		fi; \
	done; \
	echo "\nRunning $(words $(IDENTS_INVALID_TESTS)) invalid identifier resolution tests:"; \
	for t in $(IDENTS_INVALID_TESTS); do \
		printf "%s %-20s " '-' "$$t"; \
		out="$(TEST_IDENTS_INVALID_DIR)/$$t.out"; \
		if $(TEST_EXEC) -idents "$(TEST_IDENTS_INVALID_DIR)/$$t.c" > "$$out" 2>&1; then \
			echo "$$RED FAIL $$NC"; \
		else \
			if [ -f "$(TEST_IDENTS_INVALID_DIR)/$$t.ok" ]; then \
				if diff -u "$(TEST_IDENTS_INVALID_DIR)/$$t.ok" "$$out" >/dev/null 2>&1; then \
					echo "$$GREEN PASS $$NC"; passed=$$((passed+1)); \
				else \
					echo "$$RED FAIL $$NC"; \
				fi; \
			else \
				echo "$$GREEN PASS $$NC"; passed=$$((passed+1)); \
			fi; \
		fi; \
	done; \
	echo "\nRunning $(words $(LABELS_TESTS)) labeling tests:"; \
	for t in $(LABELS_TESTS); do \
		printf "%s %-20s " '-' "$$t"; \
		out="$(TEST_LABELS_DIR)/$$t.out"; \
		if $(TEST_EXEC) -labels "$(TEST_LABELS_DIR)/$$t.c" > "$$out" 2>/dev/null; then \
			if diff -u "$(TEST_LABELS_DIR)/$$t.ok" "$$out" >/dev/null 2>&1; then \
				echo "$$GREEN PASS $$NC"; passed=$$((passed+1)); \
			else \
				echo "$$RED FAIL $$NC"; \
			fi; \
		else \
			echo "$$RED FAIL $$NC"; \
		fi; \
	done; \
	echo "\nRunning $(words $(LABELS_INVALID_TESTS)) invalid labeling tests:"; \
	for t in $(LABELS_INVALID_TESTS); do \
		printf "%s %-20s " '-' "$$t"; \
		out="$(TEST_LABELS_INVALID_DIR)/$$t.out"; \
		if $(TEST_EXEC) -labels "$(TEST_LABELS_INVALID_DIR)/$$t.c" > "$$out" 2>&1; then \
			echo "$$RED FAIL $$NC"; \
		else \
			if [ -f "$(TEST_LABELS_INVALID_DIR)/$$t.ok" ]; then \
				if diff -u "$(TEST_LABELS_INVALID_DIR)/$$t.ok" "$$out" >/dev/null 2>&1; then \
					echo "$$GREEN PASS $$NC"; passed=$$((passed+1)); \
				else \
					echo "$$RED FAIL $$NC"; \
				fi; \
			else \
				echo "$$GREEN PASS $$NC"; passed=$$((passed+1)); \
			fi; \
		fi; \
	done; \
	echo "\nRunning $(words $(TYPES_TESTS)) typechecking tests:"; \
	for t in $(TYPES_TESTS); do \
		printf "%s %-20s " '-' "$$t"; \
		out="$(TEST_TYPES_DIR)/$$t.out"; \
		if $(TEST_EXEC) -types "$(TEST_TYPES_DIR)/$$t.c" > "$$out" 2>/dev/null; then \
			if diff -u "$(TEST_TYPES_DIR)/$$t.ok" "$$out" >/dev/null 2>&1; then \
				echo "$$GREEN PASS $$NC"; passed=$$((passed+1)); \
			else \
				echo "$$RED FAIL $$NC"; \
			fi; \
		else \
			echo "$$RED FAIL $$NC"; \
		fi; \
	done; \
	echo "\nRunning $(words $(TYPES_INVALID_TESTS)) invalid typechecking tests:"; \
	for t in $(TYPES_INVALID_TESTS); do \
		printf "%s %-20s " '-' "$$t"; \
		out="$(TEST_TYPES_INVALID_DIR)/$$t.out"; \
		if $(TEST_EXEC) -types "$(TEST_TYPES_INVALID_DIR)/$$t.c" > "$$out" 2>&1; then \
			echo "$$RED FAIL $$NC"; \
		else \
			if [ -f "$(TEST_TYPES_INVALID_DIR)/$$t.ok" ]; then \
				if diff -u "$(TEST_TYPES_INVALID_DIR)/$$t.ok" "$$out" >/dev/null 2>&1; then \
					echo "$$GREEN PASS $$NC"; passed=$$((passed+1)); \
				else \
					echo "$$RED FAIL $$NC"; \
				fi; \
			else \
				echo "$$GREEN PASS $$NC"; passed=$$((passed+1)); \
			fi; \
		fi; \
	done; \
	echo "\nRunning $(words $(TAC_INTERP_TESTS)) TAC interpreter tests:"; \
	for t in $(TAC_INTERP_TESTS); do \
		if $(TAC_INTERP_TEST_EXEC); then \
			echo "$$GREEN PASS $$NC"; passed=$$((passed+1)); \
		else \
			echo "$$RED FAIL $$NC"; \
		fi; \
	done; \
	echo "\nRunning $(words $(TAC_EXEC_TESTS)) TAC execution tests:"; \
	for t in $(TAC_EXEC_TESTS); do \
		if $(TAC_EXEC_TEST_EXEC); then \
			echo "$$GREEN PASS $$NC"; passed=$$((passed+1)); \
		else \
			echo "$$RED FAIL $$NC"; \
		fi; \
	done; \
	echo; \
	echo "Summary: $$passed / $$total tests passed.";
endef

test: TEST_EXEC := $(DEBUG_EXEC)
test: TAC_INTERP_TEST_EXEC := $(TAC_INTERP_TEST_EXEC_DEBUG)
test: TAC_EXEC_TEST_EXEC := $(TAC_EXEC_TEST_EXEC_DEBUG)
test: $(DEBUG_EXEC) $(TAC_INTERP_TEST_EXEC_DEBUG) $(TAC_EXEC_TEST_EXEC_DEBUG)
	$(RUN_TESTS)

test-release: TEST_EXEC := $(RELEASE_EXEC)
test-release: TAC_INTERP_TEST_EXEC := $(TAC_INTERP_TEST_EXEC_RELEASE)
test-release: TAC_EXEC_TEST_EXEC := $(TAC_EXEC_TEST_EXEC_RELEASE)
test-release: $(RELEASE_EXEC) $(TAC_INTERP_TEST_EXEC_RELEASE) $(TAC_EXEC_TEST_EXEC_RELEASE)
	$(RUN_TESTS)

# TAC interpreter test build rules
$(TAC_INTERP_TEST_OBJ_DEBUG): $(TAC_INTERP_TEST_SRC) | dirs-debug
	$(CC) $(CFLAGS_DEBUG) -I$(SRC_DIR) -c $< -o $@

$(TAC_INTERP_TEST_OBJ_RELEASE): $(TAC_INTERP_TEST_SRC) | dirs-release
	$(CC) $(CFLAGS_RELEASE) -I$(SRC_DIR) -c $< -o $@

$(TAC_INTERP_TEST_EXEC_DEBUG): $(TAC_INTERP_TEST_OBJ_DEBUG) $(DEBUG_OBJFILES_NO_MAIN) | dirs-debug
	$(CC) $(CFLAGS_DEBUG) $(LDFLAGS_DEBUG) -o $@ $(TAC_INTERP_TEST_OBJ_DEBUG) $(DEBUG_OBJFILES_NO_MAIN)

$(TAC_INTERP_TEST_EXEC_RELEASE): $(TAC_INTERP_TEST_OBJ_RELEASE) $(RELEASE_OBJFILES_NO_MAIN) | dirs-release
	$(CC) $(CFLAGS_RELEASE) $(LDFLAGS_RELEASE) -o $@ $(TAC_INTERP_TEST_OBJ_RELEASE) $(RELEASE_OBJFILES_NO_MAIN)

# TAC execution test build rules
$(TAC_EXEC_TEST_OBJ_DEBUG): $(TAC_EXEC_TEST_SRC) | dirs-debug
	$(CC) $(CFLAGS_DEBUG) -I$(SRC_DIR) -c $< -o $@

$(TAC_EXEC_TEST_OBJ_RELEASE): $(TAC_EXEC_TEST_SRC) | dirs-release
	$(CC) $(CFLAGS_RELEASE) -I$(SRC_DIR) -c $< -o $@

$(TAC_EXEC_TEST_EXEC_DEBUG): $(TAC_EXEC_TEST_OBJ_DEBUG) $(DEBUG_OBJFILES_NO_MAIN) | dirs-debug
	$(CC) $(CFLAGS_DEBUG) $(LDFLAGS_DEBUG) -o $@ $(TAC_EXEC_TEST_OBJ_DEBUG) $(DEBUG_OBJFILES_NO_MAIN)

$(TAC_EXEC_TEST_EXEC_RELEASE): $(TAC_EXEC_TEST_OBJ_RELEASE) $(RELEASE_OBJFILES_NO_MAIN) | dirs-release
	$(CC) $(CFLAGS_RELEASE) $(LDFLAGS_RELEASE) -o $@ $(TAC_EXEC_TEST_OBJ_RELEASE) $(RELEASE_OBJFILES_NO_MAIN)

# Phony targets
.PHONY: all debug release clean purge test test-release
