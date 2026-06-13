# 🧠 Algorithmic Online Judge

> A multithreaded, client-server competitive programming judge built in POSIX C — featuring a sandboxed execution engine, role-based authentication, and real-time leaderboard.

![Language](https://img.shields.io/badge/language-C-blue?style=flat-square)
![Platform](https://img.shields.io/badge/platform-Linux%20%7C%20WSL-green?style=flat-square)
![License](https://img.shields.io/badge/license-MIT-lightgrey?style=flat-square)

Supports multiple concurrent clients, safe binary database access via `fcntl` advisory locks, and a `fork`+`setrlimit` sandbox that enforces CPU time limits and prevents runaway submissions from hanging the server.

## ✨ Features

- 🔐 **Role-based auth** — Admin, Contestant, and Spectator roles
- ⚡ **Concurrent connections** — one detached `pthread` per client
- 🔒 **Race-free database** — `F_RDLCK` / `F_WRLCK` file locks on all reads/writes
- 🏖️ **Sandboxed execution** — `fork` + `pipe` + `dup2` + `setrlimit(RLIMIT_CPU)`
- 🚦 **Verdict engine** — Accepted, Wrong Answer, Time Limit Exceeded
- 🛑 **Emergency halt** — Admin can broadcast `SIGUSR1` to pause all submissions
- 📊 **Live leaderboard** — sorted scores persisted to binary file

## 📋 Table of Contents
1. [OS Concepts Implemented](#1-os-concepts-implemented)
2. [Architecture & Directory Structure](#2-architecture--directory-structure)
3. [Build & Run](#3-build--run)
4. [System Output & Screenshots](#4-system-output--screenshots)
5. [Challenges Faced and Solutions](#5-challenges-faced-and-solutions)

## 1. OS Concepts Implemented
This project heavily relies on core Operating System concepts to guarantee safety, concurrency, and security.

- **Multithreading (`pthread`)**
  - **Location:** `src/server.c`
  - **Implementation:** The server uses `pthread_create()` to spawn a detached thread for every incoming client connection. This allows multiple clients to interact with the system concurrently without blocking the main socket acceptance loop.
  ```c
  pthread_t tid;
  if (pthread_create(&tid, NULL, client_handler, client_fd) != 0) { ... }
  pthread_detach(tid);
  ```

- **Mutexes**
  - **Location:** `src/server.c`
  - **Implementation:** A `pthread_mutex_t` (`evaluation_mutex`) prevents race conditions when concurrent contestants submit code, ensuring the compilation sandbox temporary files and executions do not destructively overlap.
  ```c
  pthread_mutex_lock(&evaluation_mutex);
  sandbox_result_t result = evaluate_submission(...);
  pthread_mutex_unlock(&evaluation_mutex);
  ```

- **Advisory File Locking (`fcntl`)**
  - **Location:** `src/auth.c` and `src/database.c`
  - **Implementation:** 
    - **Shared Read Locks (`F_RDLCK`):** Applied when clients view the leaderboard or problem lists, allowing high-concurrency non-blocking reads.
    - **Exclusive Write Locks (`F_WRLCK`):** Applied when registering new users or updating the leaderboard, ensuring serialised, corruption-free binary file writes.
  ```c
  // Example of Write Lock in src/database.c
  struct flock fl = { .l_type = F_WRLCK, .l_whence = SEEK_SET, .l_start = 0, .l_len = 0 };
  fcntl(fd, F_SETLKW, &fl);
  // ... perform write ...
  fl.l_type = F_UNLCK;
  fcntl(fd, F_SETLK, &fl);
  ```

- **Process Management & IPC (Pipes)**
  - **Location:** `src/sandbox.c`
  - **Implementation:** When a user submits code, the server uses `fork()` to create an isolated sandbox process. `execlp()` transforms the child process into the compiler. A second child is forked to execute the binary. `pipe()` and `dup2()` feed the hidden input testcase to the child's `stdin` and capture its `stdout` back to the parent for evaluation.
  ```c
  int out_pipe[2];
  pipe(out_pipe);
  pid_t pid = fork();
  if (pid == 0) { // Child
      dup2(out_pipe[1], STDOUT_FILENO); // Redirect stdout to pipe
      execlp("./a.out", "./a.out", NULL);
  }
  ```

- **Resource Limits**
  - **Location:** `src/sandbox.c`
  - **Implementation:** `setrlimit()` enforces a strict CPU time limit (e.g., 2 seconds) on the executing child process. If the user's code falls into an infinite loop, the OS sends `SIGXCPU` to kill the process, which the parent's `waitpid()` call detects as a "Time Limit Exceeded".
  ```c
  struct rlimit rl = { .rlim_cur = 2, .rlim_max = 2 };
  setrlimit(RLIMIT_CPU, &rl);
  ```

- **Signal Handling**
  - **Location:** `src/server.c`
  - **Implementation:** `sigaction()` catches `SIGINT` for graceful shutdown. An intra-process IPC mechanism uses `SIGUSR1` to trigger an "Emergency System Halt", pausing submissions dynamically.
  ```c
  // Admin triggers halt via IPC signal
  kill(getpid(), SIGUSR1);
  ```

## 2. Architecture & Directory Structure

```
online_judge/
├── src/
│   ├── server.c         # Main daemon, socket listening, thread spawning
│   ├── client.c         # CLI terminal UI, socket connection
│   ├── auth.c           # Role-based login, fcntl locks for auth
│   ├── sandbox.c        # fork, exec, pipe, setrlimit execution engine
│   └── database.c       # File initialization, fcntl locks for CRUD
├── include/
│   └── database.h, auth.h, sandbox.h
├── data/                # Persistent binary storage (created at runtime)
│   ├── users.dat        # Binary User records
│   ├── problems.dat     # Binary Problem definitions
│   └── leaderboard.dat  # Binary ScoreRecord records
├── demo_assets/         # Sample solutions and test cases
│   ├── test_accepted.cpp # Sample: produces expected output → ACCEPTED
│   ├── test_wrong.cpp    # Sample: wrong output → WRONG ANSWER
│   └── test_tle.cpp      # Sample: infinite loop → TLE
├── Makefile
└── README.md
```

### Default Credentials
| User ID | Password   | Role        |
|---------|------------|-------------|
| 1       | `admin123` | Admin       |
| 2       | `pass123`  | Contestant  |

*(Note: "Spectators" can connect without credentials as randomized guests).*

## 3. Build & Run
To run the system natively on a Linux or WSL environment:

```bash
# 1. Build all binaries
make

# 2. Initialise the database (seeds users & leaderboard)
./init_db

# 3. Start the server (Terminal 1)
./server

# 4. Connect a client (Terminal 2)
./client

# Cleanup (removes binaries & temps, preserves database)
make clean
```

## 4. System Output & Screenshots

### Initialization
- **Server Initialization:** The server creates the listening socket and seeds the initial binary databases.
  ![Server init](screenshots/Server%20initialization.png)
- **Client Initialization:** The CLI interface connects to the server and presents the login menu.
  ![Client init](screenshots/client%20initialization.png)

### Admin Workflows
- **Admin Login Log:**
  ![Admin Login Log](screenshots/server%20log%20on%20admin%20login.png)
- **Admin Menu:** 
  ![Admin Menu](screenshots/admin%20menu.png)
- **Problem Creation:** The admin submits a new problem title and description.
  ![Problem Creation](screenshots/problem%20creation%20as%20admin.png)
  ![Problem Creation Log](screenshots/server%20log%20on%20creating%20a%20problem.png)
- **Emergency Halt:** The admin uses IPC (`SIGUSR1`) to halt the system.
  ![System Halted](screenshots/system%20halted%20(as%20admin).png)
  ![System Halted Log](screenshots/server%20log%20system%20halted.png)

### Contestant Workflows
- **Contestant Login Log:**
  ![Contestant Login Log](screenshots/server%20log%20on%20contestant%20login.png)
- **Contestant Menu:**
  ![Contestant Menu](screenshots/contestant%20menu.png)
- **Viewing Problems:** Retrieving the problem list under a shared read lock.
  ![View Problems](screenshots/contestant%20view%20available%20problems.png)
- **Submitting Code:** The contestant submits code, which is sent to the sandbox.
  ![Submit Code](screenshots/Submitting%20solution%20as%20contestant.png)
  ![Submit Code Log](screenshots/server%20log%20for%20problem%20submission.png)
- **Halted Rejection:** Contestant attempting to submit while the system is halted.
  ![Halted Rejection](screenshots/trying%20to%20submit%20when%20system%20halted%20by%20admin.png)

### General Workflows
- **Leaderboard:** Displaying the sorted scores under a shared read lock.
  ![Leaderboard](screenshots/leaderboard%20view.png)
  ![Leaderboard Log](screenshots/server%20log%20for%20viewing%20leaderboard.png)
- **Logout:** 
  ![Logout Log](screenshots/server%20log%20on%20user%20logout.png)

## 5. Challenges Faced and Solutions

1. **Challenge: Safely Executing Untrusted User Code**
   - **Problem:** Contestants could submit code containing infinite loops, which would permanently tie up server threads and max out the host CPU.
   - **Solution:** Used `fork()` to completely isolate execution. In the child process, immediately after wiring up the pipes via `dup2()`, we apply `setrlimit(RLIMIT_CPU)` to restrict the CPU time. The OS automatically sends a `SIGXCPU` signal if the limit is exceeded. The parent server thread simply uses `waitpid()` and checks `WIFSIGNALED` to award a safe, deterministic "Time Limit Exceeded" (TLE) verdict without hanging.

2. **Challenge: Preventing Database Corruption During High Traffic**
   - **Problem:** If multiple contestants solve a problem at the exact same millisecond, they could simultaneously read and increment their score in `leaderboard.dat`, causing race conditions and lost updates.
   - **Solution:** Implemented POSIX advisory file locking using `fcntl()`. Before reading/writing the leaderboard, the thread explicitly acquires an exclusive `F_WRLCK`. This forces concurrent threads to queue safely at the OS level until the lock is released. Conversely, `F_RDLCK` is used for non-destructive reads (like viewing problems), allowing massive concurrency.

3. **Challenge: Admin Emergency Halt Synchronization**
   - **Problem:** The system halt feature uses the `kill(getpid(), SIGUSR1)` signal to dynamically pause submissions. Initially, a race condition occurred where the client thread would check the global state *before* the asynchronous signal handler had time to flip the flag, resulting in backwards/incorrect logging ("System Resumed" when it was actually "Halted").
   - **Solution:** Altered the thread logic to cache the state of the flag *before* emitting the signal. Because we know the signal handler acts as a strict toggle, caching the pre-signal state allowed the worker thread to perfectly predict and log the correct outcome without requiring sleep delays or complex thread synchronization primitives.
