CC = gcc
CFLAGS = -Wall -Wextra -std=c99 -Iinclude
LDFLAGS =

SRC_DIR = src
OBJ_DIR = obj
BIN = sqlengine

SRC = $(SRC_DIR)/main.c \
      $(SRC_DIR)/storage.c \
      $(SRC_DIR)/tokenizer.c \
      $(SRC_DIR)/parser.c \
      $(SRC_DIR)/executor.c \
      $(SRC_DIR)/cli.c \
      $(SRC_DIR)/index.c

OBJ = $(SRC:$(SRC_DIR)/%.c=$(OBJ_DIR)/%.o)

all: $(BIN)

$(OBJ_DIR):
	mkdir -p $(OBJ_DIR)

$(OBJ_DIR)/%.o: $(SRC_DIR)/%.c | $(OBJ_DIR)
	$(CC) $(CFLAGS) -c $< -o $@

$(BIN): $(OBJ)
	$(CC) $(OBJ) $(LDFLAGS) -o $(BIN)

clean:
	rm -rf $(OBJ_DIR) $(BIN) *.tbl *.idx

.PHONY: all clean