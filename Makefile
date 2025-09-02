CC ?= gcc
CFLAGS ?= -std=c11 -Wall -Wextra -O2
LDFLAGS ?=

# Architecture and cross-compile options
# Usage examples:
#   make                          # native build -> bin/native
#   make native                   # same as above
#   make arm64                    # uses aarch64-linux-gnu-gcc -> bin/aarch64
#   make ARCH=aarch64             # custom arch name
#   make ARCH=aarch64 CROSS_COMPILE=aarch64-linux-gnu-
ARCH ?= native
CROSS_COMPILE ?=

SRC_DIR := src
BIN_DIR := bin/$(ARCH)

CLIENT := $(BIN_DIR)/landrop
SERVER := $(BIN_DIR)/landropd

COMMON_SRC := $(SRC_DIR)/common.c
COMMON_HDR := $(SRC_DIR)/common.h

CLIENT_SRC := $(SRC_DIR)/client.c
SERVER_SRC := $(SRC_DIR)/server.c

# avoiding mixing toolchains
OBJ_SUFFIX := .$(ARCH).o
OBJS_CLIENT := $(CLIENT_SRC:%.c=%$(OBJ_SUFFIX)) $(COMMON_SRC:%.c=%$(OBJ_SUFFIX))
OBJS_SERVER := $(SERVER_SRC:%.c=%$(OBJ_SUFFIX)) $(COMMON_SRC:%.c=%$(OBJ_SUFFIX))

.PHONY: all clean dirs native arm64

all: dirs $(CLIENT) $(SERVER)

# Convenience targets
native: ARCH = native
native: CC = gcc
native: all

arm64: ARCH = aarch64
arm64: CROSS_COMPILE ?= aarch64-linux-gnu-
arm64: CC = $(CROSS_COMPILE)gcc
arm64: all

dirs:
	@mkdir -p $(BIN_DIR)

$(CLIENT): $(OBJS_CLIENT)
	$(CC) $(CFLAGS) -o $@ $(OBJS_CLIENT) $(LDFLAGS)

$(SERVER): $(OBJS_SERVER)
	$(CC) $(CFLAGS) -o $@ $(OBJS_SERVER) $(LDFLAGS)

%$(OBJ_SUFFIX): %.c $(COMMON_HDR)
	$(CC) $(CFLAGS) -c -o $@ $<

clean:
	rm -f $(SRC_DIR)/*.o $(SRC_DIR)/*.*.o
	rm -rf bin/*
