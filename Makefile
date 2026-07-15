CC ?= cc
CFLAGS ?=
CPPFLAGS ?=
LDFLAGS ?=

# Project warning flags (appended; user CFLAGS still apply)
WARN_FLAGS = -Wall -Wextra -Wpedantic -Wshadow -Wconversion -Wstrict-prototypes -Wmissing-prototypes
STD_FLAGS = -std=c11
DEPFLAGS = -MMD -MP

SRC_DIR = src
OBJ_DIR = obj
TEST_DIR = tests
BIN = sqlengine
TEST_BIN = sqlengine_tests

INCLUDES = -Iinclude

LIB_SRCS = \
	$(SRC_DIR)/status.c \
	$(SRC_DIR)/util.c \
	$(SRC_DIR)/column.c \
	$(SRC_DIR)/storage.c \
	$(SRC_DIR)/tokenizer.c \
	$(SRC_DIR)/parser.c \
	$(SRC_DIR)/semantic.c \
	$(SRC_DIR)/index.c \
	$(SRC_DIR)/executor.c \
	$(SRC_DIR)/cli.c

MAIN_SRC = $(SRC_DIR)/main.c
TEST_SRC = $(TEST_DIR)/test_main.c

LIB_OBJS = $(LIB_SRCS:$(SRC_DIR)/%.c=$(OBJ_DIR)/%.o)
MAIN_OBJ = $(OBJ_DIR)/main.o
TEST_OBJ = $(OBJ_DIR)/test_main.o

DEPS = $(LIB_OBJS:.o=.d) $(MAIN_OBJ:.o=.d) $(TEST_OBJ:.o=.d)

ALL_CFLAGS = $(STD_FLAGS) $(WARN_FLAGS) $(INCLUDES) $(CPPFLAGS) $(CFLAGS) $(DEPFLAGS)

.PHONY: all clean test debug asan ubsan sanitize

all: $(BIN)

$(OBJ_DIR):
	mkdir -p $(OBJ_DIR)

$(OBJ_DIR)/%.o: $(SRC_DIR)/%.c | $(OBJ_DIR)
	$(CC) $(ALL_CFLAGS) -c $< -o $@

$(OBJ_DIR)/test_main.o: $(TEST_SRC) | $(OBJ_DIR)
	$(CC) $(ALL_CFLAGS) -c $< -o $@

$(BIN): $(LIB_OBJS) $(MAIN_OBJ)
	$(CC) $(LIB_OBJS) $(MAIN_OBJ) $(LDFLAGS) -o $@

$(TEST_BIN): $(LIB_OBJS) $(TEST_OBJ)
	$(CC) $(LIB_OBJS) $(TEST_OBJ) $(LDFLAGS) -o $@

test: $(TEST_BIN)
	./$(TEST_BIN)

debug: CFLAGS += -g -O0
debug: clean all

asan: CFLAGS += -g -O1 -fno-omit-frame-pointer -fsanitize=address
asan: LDFLAGS += -fsanitize=address
asan: clean $(TEST_BIN) $(BIN)
	./$(TEST_BIN)

ubsan: CFLAGS += -g -O1 -fno-omit-frame-pointer -fsanitize=undefined
ubsan: LDFLAGS += -fsanitize=undefined
ubsan: clean $(TEST_BIN) $(BIN)
	./$(TEST_BIN)

sanitize: CFLAGS += -g -O1 -fno-omit-frame-pointer -fsanitize=address,undefined
sanitize: LDFLAGS += -fsanitize=address,undefined
sanitize: clean $(TEST_BIN) $(BIN)
	./$(TEST_BIN)

clean:
	rm -rf $(OBJ_DIR) $(BIN) $(TEST_BIN) *.tbl *.tbl.journal *.idx *.tmp *.tmp.*

-include $(DEPS)
