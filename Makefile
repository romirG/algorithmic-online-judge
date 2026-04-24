# ──────────────────────────────────────────────────────────
#  Makefile - Algorithmic Online Judge
#
#  Targets:
#    all      : Builds init_db, server, and client
#    init_db  : Standalone database initializer
#    server   : Main server daemon (links auth, sandbox, database)
#    client   : CLI client
#    clean    : Removes all binaries and temp files
#
#  Usage:
#    make              # build everything
#    ./init_db          # seed data/users.dat & data/leaderboard.dat
#    ./server           # start server on 127.0.0.1:8080
#    ./client           # connect and interact
# ──────────────────────────────────────────────────────────

CC       = gcc
CFLAGS   = -Wall -Wextra -Iinclude
LDFLAGS  = -pthread

SRC_DIR  = src
INC_DIR  = include

# ─── Targets ─────────────────────────────────────────────

.PHONY: all clean

all: init_db server client

# Standalone database initialiser.
# -DSTANDALONE_INIT enables the conditional main() in database.c
init_db: $(SRC_DIR)/database.c
	$(CC) $(CFLAGS) -DSTANDALONE_INIT -o $@ $^

# Server binary: links all modules + pthreads
server: $(SRC_DIR)/server.c $(SRC_DIR)/database.c $(SRC_DIR)/auth.c $(SRC_DIR)/sandbox.c
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS)

# Client binary: only needs the protocol structs (from database.h)
client: $(SRC_DIR)/client.c
	$(CC) $(CFLAGS) -o $@ $^

# ─── Cleanup ─────────────────────────────────────────────

clean:
	rm -f init_db server client a.out temp.cpp temp.c
	rm -rf data/
