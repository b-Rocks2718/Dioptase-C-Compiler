# Variables
CC 				:= gcc
CFLAGS 		:= -Wall -g

SRC_DIR		:= src
BUILD_DIR := build
TEST_PREPROCESS_DIR := tests/preprocess
TEST_PREPROCESS_INVALID_DIR := tests/preprocess_invalid
TEST_LEXER_DIR := tests/lexer
TEST_LEXER_INVALID_DIR := tests/lexer_invalid
TEST_PARSER_DIR := tests/parser
TEST_PARSER_INVALID_DIR := tests/parser_invalid
TEST_IDENTS_DIR := tests/idents
TEST_IDENTS_INVALID_DIR := tests/idents_invalid

C_SRCS 		:= $(wildcard $(SRC_DIR)/*.c)
OBJFILES 	:= $(patsubst %.c, $(BUILD_DIR)/objfiles/%.o, $(notdir $(C_SRCS)))

EXEC 			:= bcc
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

# Rule to build the main executable
all: $(BUILD_DIR)/$(EXEC)

$(BUILD_DIR)/$(EXEC): $(OBJFILES)
	$(CC) $(CFLAGS) -o $@ $(OBJFILES)

# Rule to generate .o files from .c files
$(BUILD_DIR)/objfiles/%.o: $(SRC_DIR)/%.c | dirs
	$(CC) $(CFLAGS) -c $< -o $@

dirs:
	@mkdir -p $(BUILD_DIR)/objfiles

# Rule to clean up generated files
clean:
	rm -f $(OBJFILES) $(BUILD_DIR)/$(EXEC) \
		$(TEST_PREPROCESS_DIR)/*.out \
		$(TEST_PREPROCESS_INVALID_DIR)/*.out \
		$(TEST_LEXER_DIR)/*.out \
		$(TEST_LEXER_INVALID_DIR)/*.out \
		$(TEST_PARSER_DIR)/*.out \
		$(TEST_PARSER_INVALID_DIR)/*.out \
		$(TEST_IDENTS_DIR)/*.out \
		$(TEST_IDENTS_INVALID_DIR)/*.out

# Rule to clean up generated files
purge:
	rm -rf $(BUILD_DIR)

test: $(BUILD_DIR)/$(EXEC)
	@GREEN="\033[0;32m"; \
	RED="\033[0;31m"; \
	NC="\033[0m"; \
	passed=0; total=$$(( $(words $(PREPROCESS_TESTS)) + $(words $(PREPROCESS_INVALID_TESTS)) + $(words $(LEXER_TESTS)) + $(words $(LEXER_INVALID_TESTS)) + $(words $(PARSER_TESTS)) + $(words $(PARSER_INVALID_TESTS)) + $(words $(IDENTS_TESTS)) + $(words $(IDENTS_INVALID_TESTS)) )); \
	echo "Running $(words $(PREPROCESS_TESTS)) preprocess tests:"; \
	for t in $(PREPROCESS_TESTS); do \
		printf "%s %-20s " '-' "$$t"; \
		out="$(TEST_PREPROCESS_DIR)/$$t.out"; \
		flags_file="$(TEST_PREPROCESS_DIR)/$$t.flags"; \
		flags=""; \
		if [ -f "$$flags_file" ]; then flags="$$(cat "$$flags_file")"; fi; \
		if $(BUILD_DIR)/$(EXEC) -preprocess $$flags "$(TEST_PREPROCESS_DIR)/$$t.c" > "$$out" 2>/dev/null; then \
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
		if $(BUILD_DIR)/$(EXEC) -preprocess $$flags "$(TEST_PREPROCESS_INVALID_DIR)/$$t.c" > "$$out" 2>&1; then \
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
		if $(BUILD_DIR)/$(EXEC) -tokens "$(TEST_LEXER_DIR)/$$t.c" > "$$out" 2>/dev/null; then \
			if diff -u "$(TEST_LEXER_DIR)/$$t.ok" "$$out" >/dev/null 2>&1; then \
				echo "$$GREEN PASS $$NC"; passed=$$((passed+1)); \
			else \
				echo "$$RED FAIL $$NC"; \
			fi; \
		else \
			echo "$$RED FAIL $$NC"; \
		fi; \
	done; \
	echo "\nRunning $(words $(LEXER_INVALID_TESTS)) invalid lexer tests:"; \
	for t in $(LEXER_INVALID_TESTS); do \
		printf "%s %-20s " '-' "$$t"; \
		out="$(TEST_LEXER_INVALID_DIR)/$$t.out"; \
		if $(BUILD_DIR)/$(EXEC) -tokens "$(TEST_LEXER_INVALID_DIR)/$$t.c" > "$$out" 2>/dev/null; then \
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
		if $(BUILD_DIR)/$(EXEC) -ast "$(TEST_PARSER_DIR)/$$t.c" > "$$out" 2>/dev/null; then \
			if diff -u "$(TEST_PARSER_DIR)/$$t.ok" "$$out" >/dev/null 2>&1; then \
				echo "$$GREEN PASS $$NC"; passed=$$((passed+1)); \
			else \
				echo "$$RED FAIL $$NC"; \
			fi; \
		else \
			echo "$$RED FAIL $$NC"; \
		fi; \
	done; \
	echo "\nRunning $(words $(PARSER_INVALID_TESTS)) invalid parser tests:"; \
	for t in $(PARSER_INVALID_TESTS); do \
		printf "%s %-20s " '-' "$$t"; \
		out="$(TEST_PARSER_INVALID_DIR)/$$t.out"; \
		if $(BUILD_DIR)/$(EXEC) -ast "$(TEST_PARSER_INVALID_DIR)/$$t.c" > "$$out" 2>&1; then \
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
		if $(BUILD_DIR)/$(EXEC) -idents "$(TEST_IDENTS_DIR)/$$t.c" > "$$out" 2>/dev/null; then \
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
		if $(BUILD_DIR)/$(EXEC) -idents "$(TEST_IDENTS_INVALID_DIR)/$$t.c" > "$$out" 2>&1; then \
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
	echo; \
	echo "Summary: $$passed / $$total tests passed.";

# Phony targets
.PHONY: clean test
