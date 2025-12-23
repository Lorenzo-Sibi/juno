CC = gcc
CFLAGS = -Wall -Wextra -std=c99 -g

OBJ_DIR  = obj
BIN_DIR = bin
TEST_DIR = tests
JSON_TEST_DIR = $(TEST_DIR)/json_files_test

SRCS = jsonp.c
OBJS = $(patsubst %.c, $(OBJ_DIR)/%.o, $(SRCS))

TEST_SRCS = $(TEST_DIR)/test_main.c
TEST_OBJS = $(OBJ_DIR)/test_main.o

all: parser

$(BIN_DIR):
	mkdir -p $(BIN_DIR)

$(OBJ_DIR):
	mkdir -p $(OBJ_DIR)

$(OBJ_DIR)/%.o: %.c | $(OBJ_DIR)
	$(CC) $(CFLAGS) -I. -c $< -o $@
	
tokenizer: jsont.h
	$(CC) -Wall -Wextra -Og -g jsont.h -o jsont

parser: $(BIN_DIR) jsonp.c jsont.h
	$(CC) -Wall -Wextra -Og -g -DJSONP_DEMO jsonp.c -o $(BIN_DIR)/jsonp

# Test suite: builds the library objects + tests/test_main.c
test: $(BIN_DIR) $(OBJS) $(TEST_OBJS)
	$(CC) $(CFLAGS) -o $(BIN_DIR)/test_runner $(TEST_OBJS) $(OBJS)
	$(BIN_DIR)/test_runner

$(OBJ_DIR)/test_main.o: $(TEST_DIR)/test_main.c | $(OBJ_DIR)
	$(CC) $(CFLAGS) -I. -c $< -o $@

clean:
	rm -f jsont jsonp test_runner
	rm -rf $(BIN_DIR)
	rm -rf $(OBJ_DIR)

.PHONY: all clean test tokenizer parser