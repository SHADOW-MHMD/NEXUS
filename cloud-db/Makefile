# NEXUS Inventory Manager - Makefile

CC = gcc
LIBPQ_CFLAGS := $(shell pkg-config --cflags libpq 2>/dev/null)
LIBPQ_LIBS := $(shell pkg-config --libs libpq 2>/dev/null)

ifeq ($(strip $(LIBPQ_LIBS)),)
$(error libpq not found via pkg-config. Install libpq-dev and pkg-config before building)
endif

CFLAGS = -Wall -Wextra -O2 -I./include $(LIBPQ_CFLAGS)
LDFLAGS = $(LIBPQ_LIBS) -lmicrohttpd -lssl -lcrypto -lcurl

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
	@command -v apt-get >/dev/null && apt-get install -y pkg-config libpq-dev libmicrohttpd-dev libssl-dev libcurl4-openssl-dev || echo "Please install: pkg-config, libpq-dev, libmicrohttpd-dev, openssl, libcurl"

run: $(TARGET)
	./$(TARGET)

run-port: $(TARGET)
	./$(TARGET) nexus_data.db 8080

.PHONY: all clean run install-deps run-port
