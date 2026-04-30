# Algorithmic Online Judge — C/POSIX

A modular, command-line client-server application that functions as an
Algorithmic Online Judge, built entirely in C using POSIX system calls.

## OS Concepts Demonstrated

| Concept               | API / Mechanism                              | Location          |
|-----------------------|----------------------------------------------|--------------------|
| **Multithreading**    | `pthread_create()`, `pthread_detach()`       | `src/server.c`     |
| **Mutex**             | `pthread_mutex_lock/unlock`                  | `src/server.c`     |
| **File Locking (R)**  | `fcntl()` `F_RDLCK` on `users.dat`          | `src/auth.c`       |
| **File Locking (W)**  | `fcntl()` `F_WRLCK` on `leaderboard.dat`    | `src/database.c`   |
| **TCP Sockets**       | `socket`, `bind`, `listen`, `accept`, `connect` | `server.c`/`client.c` |
| **IPC — Pipes**       | `pipe()`, `read()`, `write()`                | `src/sandbox.c`    |
| **Process Mgmt**      | `fork()`, `execlp()`, `waitpid()`            | `src/sandbox.c`    |
| **Resource Limits**   | `setrlimit(RLIMIT_CPU, 2s)`                 | `src/sandbox.c`    |
| **FD Redirection**    | `dup2(pipe_fd, STDOUT_FILENO)`               | `src/sandbox.c`    |

## Directory Structure

```
online_judge/
├── src/
│   ├── server.c         # Main daemon, socket listening, thread spawning
│   ├── client.c         # CLI terminal UI, socket connection
│   ├── auth.c           # Role-based login, fcntl read locks
│   ├── sandbox.c        # fork, exec, pipe, setrlimit logic
│   └── database.c       # File initialization, fcntl write locks
├── include/
│   ├── database.h       # Structs, macros, shared constants
│   ├── auth.h           # Authentication interface
│   └── sandbox.h        # Sandbox interface
├── data/                # Created at runtime
│   ├── users.dat        # Binary User records
│   └── leaderboard.dat  # Binary ScoreRecord records
├── demo_assets/         # Sample solutions and test cases
│   ├── test_accepted.cpp # Sample: produces "Hello World\n" → ACCEPTED
│   ├── test_wrong.cpp    # Sample: wrong output → WRONG ANSWER
│   └── test_tle.cpp      # Sample: infinite loop → TLE (killed by SIGXCPU)
├── Makefile
└── README.md
```

## Build & Run (Linux)

```bash
# 1. Build all three binaries
make

# 2. Initialise the database (seeds users & leaderboard)
./init_db

# 3. Start the server (Terminal 1)
./server

# 4. Connect a client (Terminal 2)
./client
```

## Default Credentials

| User ID | Password   | Role        |
|---------|------------|-------------|
| 1       | `admin123` | Admin       |
| 2       | `pass123`  | Contestant  |

## Usage Example

```
──────────── LOGIN ────────────
  User ID  : 2
  Password : pass123
───────────────────────────────

  Login successful. Welcome, User 2 (role=Contestant).

┌─── Contestant Menu ──────────┐
│  1. View Available Problems   │
│  2. Submit Solution           │
│  3. View Leaderboard          │
│  4. Logout                    │
└───────────────────────────────┘
  Choice: 2
  Problem ID: 1
  Path to source file (.cpp/.c): demo_assets/test_accepted.cpp
  Submitting 128 bytes to Problem 1 (C++)...

  ╔═══════════════════════════╗
  ║  Verdict: ACCEPTED ✓
  ╚═══════════════════════════╝
```

## Cleanup

```bash
make clean    # Removes binaries and temp files (preserves data/)
```
