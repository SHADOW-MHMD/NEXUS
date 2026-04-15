# NEXUS Inventory Manager - Makefile

CC = gcc
CFLAGS = -Wall -Wextra -O2 -I./include
LDFLAGS = -lsqlite3 -lmicrohttpd -lssl -lcrypto

# Source files
SOURCES = src/main.c src/db.c src/auth.c src/api.c
OBJECTS = $(SOURCES:.c=.o)
TARGET = nexus_server

# Default target
all: $(TARGET)

$(TARGET): $(OBJECTS)
	$(CC) $(OBJECTS) -o $(TARGET) $(LDFLAGS)
	@echo "Build successful! Run with: ./$(TARGET)"

%.o: %.c
	$(CC) $(CFLAGS) -c $< -o $@

clean:
	rm -f $(OBJECTS) $(TARGET) nexus_data.db

install-deps:
	@echo "Installing dependencies..."
	@command -v apt-get >/dev/null && apt-get install -y libsqlite3-dev libmicrohttpd-dev libssl-dev || echo "Please install: sqlite3, libmicrohttpd, openssl"

run: $(TARGET)
	./$(TARGET)

run-port: $(TARGET)
	./$(TARGET) nexus_data.db 8080

.PHONY: all clean run install-deps run-port
