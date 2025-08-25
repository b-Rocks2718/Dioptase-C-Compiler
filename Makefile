# Variables
CC 				:= gcc
CFLAGS 		:= -Wall -g

SRC_DIR		:= src
BUILD_DIR := build

C_SRCS 		:= $(wildcard $(SRC_DIR)/*.c)
OBJFILES 	:= $(patsubst %.c, $(BUILD_DIR)/objfiles/%.o, $(notdir $(C_SRCS)))

EXEC 			:= main

# Rule to build the main executable
$(BUILD_DIR)/$(EXEC): $(OBJFILES)
	$(CC) $(CFLAGS) -o $@ $(OBJFILES)

# Rule to generate .o files from .c files
$(BUILD_DIR)/objfiles/%.o: $(SRC_DIR)/%.c | dirs
	$(CC) $(CFLAGS) -c $< -o $@

dirs:
	mkdir -p $(BUILD_DIR)/objfiles

# Rule to clean up generated files
clean:
	rm -f $(OBJFILES) $(EXEC)

# Rule to clean up generated files
purge:
	rm -rf $(BUILD_DIR)

# Phony targets
.PHONY: clean