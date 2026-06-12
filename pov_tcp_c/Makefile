CC=gcc
CFLAGS=-Wall -Wextra -O2 -std=c11 -Iinclude
LDFLAGS=-lm

SRC_DIR=src
BUILD_DIR=build

COMMON=$(SRC_DIR)/pov_tcp_protocol.c

CLIENT_SRC=$(SRC_DIR)/pov_tcp_client.c $(COMMON)
SERVER_SRC=$(SRC_DIR)/pov_tcp_server.c $(COMMON)

CLIENT_OBJ=$(CLIENT_SRC:$(SRC_DIR)/%.c=$(BUILD_DIR)/%.o)
SERVER_OBJ=$(SERVER_SRC:$(SRC_DIR)/%.c=$(BUILD_DIR)/%.o)

all: pov_tcp_client pov_tcp_server

pov_tcp_client: $(CLIENT_OBJ)
	$(CC) $(CFLAGS) -o $@ $(CLIENT_OBJ) $(LDFLAGS)

pov_tcp_server: $(SERVER_OBJ)
	$(CC) $(CFLAGS) -o $@ $(SERVER_OBJ) $(LDFLAGS)

$(BUILD_DIR)/%.o: $(SRC_DIR)/%.c
	mkdir -p $(BUILD_DIR)
	$(CC) $(CFLAGS) -c $< -o $@

clean:
	rm -rf $(BUILD_DIR) pov_tcp_client pov_tcp_server payload.bin generated_input.pbm client_results

.PHONY: all clean
