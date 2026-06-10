CC=mpicc
CFLAGS=-Wall -Wextra -O2 -Iinclude

TARGET=mpi

SRC_DIR=src
OBJ_DIR=build

SRCS=\
	$(SRC_DIR)/mpi.c \
	$(SRC_DIR)/crypto.c \
	$(SRC_DIR)/library_placeholder.c

OBJS=$(SRCS:$(SRC_DIR)/%.c=$(OBJ_DIR)/%.o)

all: $(TARGET)

$(TARGET): $(OBJS)
	$(CC) $(CFLAGS) -o $(TARGET) $(OBJS)

$(OBJ_DIR)/%.o: $(SRC_DIR)/%.c
	mkdir -p $(OBJ_DIR)
	$(CC) $(CFLAGS) -c $< -o $@

clean:
	rm -rf $(OBJ_DIR) $(TARGET) driver_packet.bin

.PHONY: all clean