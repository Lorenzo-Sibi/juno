CC = gcc
AR = ar
CFLAGS = -Wall -Wextra -std=c99 -g

OBJ_DIR = obj
BIN_DIR = bin
LIB_DIR = lib

INCLUDES = -I include -I src

LIB_NAME = juno
LIB_A = $(LIB_DIR)/lib$(LIB_NAME).a

LIB_SRCS = src/juno.c src/juno_lex.c
LIB_OBJS = $(patsubst %.c, $(OBJ_DIR)/%.o, $(LIB_SRCS))

DEMO_SRC := examples/parse_demo.c
DEMO_OBJ := $(OBJ_DIR)/$(DEMO_SRC:.c=.o)

TEST_SRC := tests/test_main.c
TEST_OBJ := $(OBJ_DIR)/$(TEST_SRC:.c=.o)

all: $(LIB_A) demo

$(BIN_DIR) $(OBJ_DIR) $(LIB_DIR):
	mkdir -p $@

$(OBJ_DIR)/%.o: %.c | $(OBJ_DIR)
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) $(INCLUDES) -c $< -o $@

$(LIB_A): $(LIB_OBJS) | $(LIB_DIR)
	$(AR) rcs $@ $(LIB_OBJS)
	
demo: $(LIB_A) $(DEMO_OBJ) | $(BIN_DIR)
	$(CC) $(CFLAGS) -o $(BIN_DIR)/demo $(DEMO_OBJ) $(LIB_A)

test: $(LIB_A) $(TEST_OBJ) | $(BIN_DIR)
	$(CC) $(CFLAGS) -o $(BIN_DIR)/test_runner $(TEST_OBJ) $(LIB_A)
	$(BIN_DIR)/test_runner

clean:
	rm -rf $(OBJ_DIR) $(BIN_DIR) $(LIB_DIR)

.PHONY: all clean demo test