/*
 * server.c  —  Multithreaded TCP server daemon for the Algorithmic Online Judge.
 * Listens on a port, accepts one detached thread per client, authenticates sessions,
 * and routes each ClientRequest to the appropriate handler (sandbox, database, auth).
 *
 * OS concepts used:
 *   - socket()/bind()/listen()/accept() : TCP server lifecycle
 *   - pthread_create()/pthread_detach() : one detached thread per client connection
 *   - pthread_mutex_t (evaluation_mutex): serializes sandbox calls (one eval at a time)
 *   - pthread_mutex_t (log_mutex)       : thread-safe writes to the in-memory log buffer
 *   - SIGUSR1 signal handler            : IPC mechanism to toggle the emergency halt flag
 *   - SIGINT  signal handler            : graceful shutdown on Ctrl-C
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <unistd.h>
#include <signal.h>
#include <stdarg.h>
#include <time.h>
#include <pthread.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>

#include "database.h"
#include "auth.h"
#include "sandbox.h"

/* ── Mutex: only one submission is evaluated at a time ──────────────────── */
pthread_mutex_t evaluation_mutex = PTHREAD_MUTEX_INITIALIZER;

/* ── Circular in-memory log buffer (protected by log_mutex) ─────────────── */
#define MAX_LOG_ENTRIES 200
#define LOG_ENTRY_LEN   256

static char   activity_log[MAX_LOG_ENTRIES][LOG_ENTRY_LEN];
static int    log_count = 0;
static pthread_mutex_t log_mutex = PTHREAD_MUTEX_INITIALIZER;

/* ── IPC: volatile so the compiler never optimises away signal-handler writes */
volatile sig_atomic_t system_halted = 0;

/* ── Graceful shutdown state ─────────────────────────────────────────────── */
volatile sig_atomic_t server_running = 1;
int global_server_fd = -1;
int active_threads   = 0;
pthread_mutex_t thread_mutex = PTHREAD_MUTEX_INITIALIZER;
pthread_cond_t  thread_cv    = PTHREAD_COND_INITIALIZER;

/* ── SIGINT handler: sets the flag that breaks the accept() loop ─────────── */
void sigint_handler(int sig)
{
    (void)sig;
    server_running = 0;
    if (global_server_fd >= 0)
        close(global_server_fd);  /* unblocks accept() */
}

/* ── SIGUSR1 handler: toggles the halt flag; safe because it only writes sig_atomic_t */
void sigusr1_handler(int sig)
{
    (void)sig;
    system_halted = !system_halted;
}

/* ── server_log: timestamped logging to stdout and circular buffer ────────── */
static void server_log(const char *fmt, ...)
{
    char entry[LOG_ENTRY_LEN];
    time_t now = time(NULL);
    struct tm *t = localtime(&now);
    int off = (int)strftime(entry, sizeof(entry), "[%H:%M:%S] ", t);

    va_list args;
    va_start(args, fmt);
    vsnprintf(entry + off, sizeof(entry) - off, fmt, args);
    va_end(args);

    printf("%s\n", entry);

    /* log_mutex makes the circular buffer safe across threads */
    pthread_mutex_lock(&log_mutex);
    strncpy(activity_log[log_count % MAX_LOG_ENTRIES], entry, LOG_ENTRY_LEN - 1);
    log_count++;
    pthread_mutex_unlock(&log_mutex);
}

/* ── get_system_logs: serialises the buffer into a string for the admin ──── */
static void get_system_logs(char *buffer, int buf_size)
{
    pthread_mutex_lock(&log_mutex);
    int offset = 0;
    offset += snprintf(buffer + offset, buf_size - offset,
                       "=== SYSTEM ACTIVITY LOG ===\n");

    int start = (log_count > MAX_LOG_ENTRIES) ? log_count - MAX_LOG_ENTRIES : 0;
    for (int i = start; i < log_count && offset < buf_size - 1; i++) {
        offset += snprintf(buffer + offset, buf_size - offset,
                           "  %s\n", activity_log[i % MAX_LOG_ENTRIES]);
    }

    if (log_count == 0)
        offset += snprintf(buffer + offset, buf_size - offset, "  (no activity yet)\n");

    (void)offset;
    pthread_mutex_unlock(&log_mutex);
}

/* ── client_handler_internal: handles one full client session ───────────── */
void *client_handler_internal(void *arg)
{
    int client_fd = *(int *)arg;
    free(arg);

    ClientRequest  req;
    ServerResponse res;

    /* Phase 1: first message must be LOGIN, REGISTER, or SPECTATOR */
    memset(&req, 0, sizeof(req));
    memset(&res, 0, sizeof(res));

    ssize_t bytes = recv(client_fd, &req, sizeof(req), 0);
    if (bytes <= 0 || (req.action != ACTION_LOGIN &&
                       req.action != ACTION_REGISTER &&
                       req.action != ACTION_SPECTATOR)) {
        res.status = STATUS_FAIL;
        snprintf(res.message, sizeof(res.message),
                 "Error: first request must be LOGIN, REGISTER, or SPECTATOR.");
        send(client_fd, &res, sizeof(res), 0);
        close(client_fd);
        return NULL;
    }

    if (req.action == ACTION_REGISTER) {
        /* New contestants are always created with ROLE_CONTESTANT */
        int role = ROLE_CONTESTANT;
        if (register_user(req.user_id, req.password, role)) {
            init_user_leaderboard(req.user_id);
            res.status = STATUS_OK;
            snprintf(res.message, sizeof(res.message),
                     "Registration successful! Please login.");
            server_log("User %d registered (role=Contestant)", req.user_id);
        } else {
            res.status = STATUS_FAIL;
            snprintf(res.message, sizeof(res.message),
                     "Registration failed: User ID %d already exists.", req.user_id);
            server_log("Failed registration attempt for user %d (duplicate)", req.user_id);
        }
        send(client_fd, &res, sizeof(res), 0);
        close(client_fd);
        return NULL;  /* client must reconnect to log in */
    }

    int role    = 0;
    int user_id = req.user_id;

    if (req.action == ACTION_SPECTATOR) {
        /* Spectators get no credentials; assign a random session ID for logging */
        role    = ROLE_SPECTATOR;
        user_id = 10000 + (rand() % 90000);
        res.status = role;
        snprintf(res.message, sizeof(res.message),
                 "Entered as Spectator (Guest %d).", user_id);
        server_log("Guest %d entered as Spectator", user_id);
    } else {
        /* ACTION_LOGIN: verify credentials with an F_RDLCK on users.dat */
        if (authenticate_user(req.user_id, req.password, &role)) {
            res.status = role;
            snprintf(res.message, sizeof(res.message),
                     "Login successful. Welcome, User %d (%s).",
                     req.user_id,
                     role == ROLE_ADMIN ? "Admin" : "Contestant");
            server_log("User %d logged in (role=%s)",
                       req.user_id,
                       role == ROLE_ADMIN ? "Admin" : "Contestant");
        } else {
            res.status = STATUS_FAIL;
            snprintf(res.message, sizeof(res.message), "Authentication failed.");
            server_log("Failed login attempt for user %d", req.user_id);
        }
    }

    send(client_fd, &res, sizeof(res), 0);
    if (res.status == STATUS_FAIL) { close(client_fd); return NULL; }

    /* Phase 2: serve requests in a loop until the client disconnects */
    while (1) {
        memset(&req, 0, sizeof(req));
        memset(&res, 0, sizeof(res));

        bytes = recv(client_fd, &req, sizeof(req), 0);
        if (bytes <= 0) {
            server_log("User %d disconnected", user_id);
            break;
        }

        switch (req.action) {

        /* ── CONTESTANT: Submit Solution ───────────────────────────────── */
        case ACTION_SUBMIT: {
            if (role != ROLE_CONTESTANT) {
                res.status = STATUS_FAIL;
                snprintf(res.message, sizeof(res.message),
                         "Permission denied. Only Contestants may submit.");
                break;
            }

            /* Refuse submissions when the admin has triggered a halt */
            if (system_halted) {
                res.status = STATUS_FAIL;
                snprintf(res.message, sizeof(res.message),
                         "SYSTEM HALTED by Admin. Submissions suspended.");
                server_log("User %d submission REJECTED (system halted)", user_id);
                break;
            }

            server_log("User %d submitted code for Problem %d (%zu bytes)",
                       user_id, req.problem_id, strlen(req.payload));

            /* evaluation_mutex ensures temp files are not mixed up between threads */
            pthread_mutex_lock(&evaluation_mutex);
            server_log("evaluation_mutex LOCKED by user %d", user_id);

            int verdict = evaluate_submission(req.payload, req.problem_id,
                                              req.file_ext);

            server_log("evaluation_mutex UNLOCKED by user %d", user_id);
            pthread_mutex_unlock(&evaluation_mutex);

            switch (verdict) {
            case VERDICT_AC:
                update_leaderboard(user_id, req.problem_id);
                res.status = STATUS_OK;
                snprintf(res.message, sizeof(res.message), "Verdict: ACCEPTED (AC) ✓");
                server_log("User %d → Problem %d → AC", user_id, req.problem_id);
                break;
            case VERDICT_WA:
                res.status = STATUS_FAIL;
                snprintf(res.message, sizeof(res.message), "Verdict: WRONG ANSWER (WA) ✗");
                server_log("User %d → Problem %d → WA", user_id, req.problem_id);
                break;
            case VERDICT_CE:
                res.status = STATUS_FAIL;
                snprintf(res.message, sizeof(res.message), "Verdict: COMPILATION ERROR (CE)");
                server_log("User %d → Problem %d → CE", user_id, req.problem_id);
                break;
            case VERDICT_TLE:
                res.status = STATUS_FAIL;
                snprintf(res.message, sizeof(res.message), "Verdict: TIME LIMIT EXCEEDED (TLE)");
                server_log("User %d → Problem %d → TLE", user_id, req.problem_id);
                break;
            }
            break;
        }

        /* ── ALL ROLES: View Leaderboard (F_RDLCK — readers never block each other) */
        case ACTION_LEADERBOARD:
            res.status = STATUS_OK;
            get_leaderboard(res.message, sizeof(res.message));
            break;

        /* ── CONTESTANT + SPECTATOR: View Available Problems (F_RDLCK) ─── */
        case ACTION_VIEW_PROBLEMS:
            res.status = STATUS_OK;
            get_problems(res.message, sizeof(res.message));
            break;

        /* ── ADMIN: Create / Update Problem (F_WRLCK) ──────────────────── */
        case ACTION_CREATE_PROBLEM: {
            if (role != ROLE_ADMIN) {
                res.status = STATUS_FAIL;
                snprintf(res.message, sizeof(res.message), "Permission denied.");
                break;
            }

            /* Payload format: "title\ndescription" */
            char title[100] = "", desc[512] = "";
            char *nl = strchr(req.payload, '\n');
            if (nl) {
                size_t tlen = (size_t)(nl - req.payload);
                if (tlen >= sizeof(title)) tlen = sizeof(title) - 1;
                strncpy(title, req.payload, tlen);
                title[tlen] = '\0';
                strncpy(desc, nl + 1, sizeof(desc) - 1);
            } else {
                strncpy(title, req.payload, sizeof(title) - 1);
            }

            if (create_problem(req.problem_id, title, desc)) {
                res.status = STATUS_OK;
                snprintf(res.message, sizeof(res.message),
                         "Problem %d created/updated: %s", req.problem_id, title);
                server_log("Admin created Problem %d: %s", req.problem_id, title);
            } else {
                res.status = STATUS_FAIL;
                snprintf(res.message, sizeof(res.message), "Failed to create problem.");
            }
            break;
        }

        /* ── ADMIN: Upload input.txt for a problem ──────────────────────── */
        case ACTION_UPLOAD_INPUT: {
            if (role != ROLE_ADMIN) {
                res.status = STATUS_FAIL;
                snprintf(res.message, sizeof(res.message), "Permission denied.");
                break;
            }
            if (save_testcase_file(req.problem_id, "input.txt", req.payload)) {
                res.status = STATUS_OK;
                snprintf(res.message, sizeof(res.message),
                         "input.txt uploaded for Problem %d.", req.problem_id);
                server_log("Admin uploaded input.txt for Problem %d", req.problem_id);
            } else {
                res.status = STATUS_FAIL;
                snprintf(res.message, sizeof(res.message), "Failed to save input.txt.");
            }
            break;
        }

        /* ── ADMIN: Upload expected.txt for a problem ───────────────────── */
        case ACTION_UPLOAD_EXPECTED: {
            if (role != ROLE_ADMIN) {
                res.status = STATUS_FAIL;
                snprintf(res.message, sizeof(res.message), "Permission denied.");
                break;
            }
            if (save_testcase_file(req.problem_id, "expected.txt", req.payload)) {
                res.status = STATUS_OK;
                snprintf(res.message, sizeof(res.message),
                         "expected.txt uploaded for Problem %d.", req.problem_id);
                server_log("Admin uploaded expected.txt for Problem %d", req.problem_id);
            } else {
                res.status = STATUS_FAIL;
                snprintf(res.message, sizeof(res.message), "Failed to save expected.txt.");
            }
            break;
        }

        /* ── ADMIN: Dump activity log ────────────────────────────────────── */
        case ACTION_VIEW_LOGS:
            if (role != ROLE_ADMIN) {
                res.status = STATUS_FAIL;
                snprintf(res.message, sizeof(res.message), "Permission denied.");
                break;
            }
            res.status = STATUS_OK;
            get_system_logs(res.message, sizeof(res.message));
            break;

        /* ── ADMIN: Toggle emergency halt via SIGUSR1 (IPC) ─────────────── */
        case ACTION_HALT_SYSTEM:
            if (role != ROLE_ADMIN) {
                res.status = STATUS_FAIL;
                snprintf(res.message, sizeof(res.message), "Permission denied.");
                break;
            }

            /* kill(getpid(), SIGUSR1) is an intra-process IPC signal;
             * the handler flips system_halted atomically. */
            kill(getpid(), SIGUSR1);
            res.status = STATUS_OK;

            if (system_halted) {
                snprintf(res.message, sizeof(res.message),
                    "SYSTEM HALTED. All contestant submissions suspended.\n"
                    "Send this command again to resume.\n"
                    "(Server PID: %d — can also: kill -SIGUSR1 %d)",
                    getpid(), getpid());
                server_log("*** SYSTEM HALTED by Admin ***");
            } else {
                snprintf(res.message, sizeof(res.message),
                    "System RESUMED. Contestant submissions re-enabled.");
                server_log("*** SYSTEM RESUMED by Admin ***");
            }
            break;

        default:
            res.status = STATUS_FAIL;
            snprintf(res.message, sizeof(res.message), "Unknown action: %d", req.action);
            break;
        }

        send(client_fd, &res, sizeof(res), 0);
    }

    close(client_fd);
    return NULL;
}

/* ── client_handler: thin wrapper that logs IP and decrements thread counter */
void *client_handler(void *arg)
{
    int client_fd = *(int *)arg;

    /* Capture peer address before handing off, for logging purposes */
    struct sockaddr_in peer_addr;
    socklen_t peer_len = sizeof(peer_addr);
    char ip[64] = "Unknown";
    int  port   = 0;
    if (getpeername(client_fd, (struct sockaddr *)&peer_addr, &peer_len) == 0) {
        strncpy(ip, inet_ntoa(peer_addr.sin_addr), sizeof(ip) - 1);
        port = ntohs(peer_addr.sin_port);
    }

    void *ret = client_handler_internal(arg);

    server_log("Connection closed from %s:%d", ip, port);

    /* Signal the shutdown path that this thread has finished */
    pthread_mutex_lock(&thread_mutex);
    active_threads--;
    pthread_cond_signal(&thread_cv);
    pthread_mutex_unlock(&thread_mutex);

    return ret;
}

/* ── main ─────────────────────────────────────────────────────────────────── */
int main(void)
{
    printf("╔══════════════════════════════════════════╗\n");
    printf("║   Algorithmic Online Judge  -  Server    ║\n");
    printf("╚══════════════════════════════════════════╝\n\n");

    /* Register SIGUSR1 for the admin-triggered halt IPC mechanism */
    struct sigaction sa;
    memset(&sa, 0, sizeof(sa));
    sa.sa_handler = sigusr1_handler;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = 0;
    if (sigaction(SIGUSR1, &sa, NULL) < 0) {
        perror("[Server] sigaction SIGUSR1");
        exit(EXIT_FAILURE);
    }
    printf("[Server] SIGUSR1 handler installed (PID: %d)\n", getpid());

    /* Register SIGINT so Ctrl-C shuts down cleanly */
    struct sigaction sa_int;
    memset(&sa_int, 0, sizeof(sa_int));
    sa_int.sa_handler = sigint_handler;
    sigemptyset(&sa_int.sa_mask);
    sa_int.sa_flags = 0;
    if (sigaction(SIGINT, &sa_int, NULL) < 0) {
        perror("[Server] sigaction SIGINT");
        exit(EXIT_FAILURE);
    }
    printf("[Server] SIGINT handler installed\n");

    init_database();
    printf("\n");

    /* Create and configure the listening TCP socket */
    int server_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (server_fd < 0) {
        perror("[Server] socket()");
        exit(EXIT_FAILURE);
    }
    global_server_fd = server_fd;

    /* SO_REUSEADDR avoids "address already in use" on quick restarts */
    int opt = 1;
    setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family      = AF_INET;
    addr.sin_addr.s_addr = inet_addr(SERVER_IP);
    addr.sin_port        = htons(PORT);

    if (bind(server_fd, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        perror("[Server] bind()");
        close(server_fd);
        exit(EXIT_FAILURE);
    }

    if (listen(server_fd, 10) < 0) {
        perror("[Server] listen()");
        close(server_fd);
        exit(EXIT_FAILURE);
    }

    server_log("Server listening on %s:%d", SERVER_IP, PORT);
    printf("[Server] Halt system from terminal: kill -SIGUSR1 %d\n\n", getpid());

    /* Accept loop: each accepted connection gets its own detached thread */
    while (server_running) {
        struct sockaddr_in client_addr;
        socklen_t client_len = sizeof(client_addr);

        int *client_fd = malloc(sizeof(int));
        if (!client_fd) { perror("[Server] malloc"); continue; }

        *client_fd = accept(server_fd, (struct sockaddr *)&client_addr, &client_len);
        if (*client_fd < 0) {
            if (errno == EINTR || !server_running) { free(client_fd); break; }
            perror("[Server] accept()");
            free(client_fd);
            continue;
        }

        server_log("Connection from %s:%d",
                   inet_ntoa(client_addr.sin_addr),
                   ntohs(client_addr.sin_port));

        pthread_mutex_lock(&thread_mutex);
        active_threads++;
        pthread_mutex_unlock(&thread_mutex);

        pthread_t tid;
        if (pthread_create(&tid, NULL, client_handler, client_fd) != 0) {
            perror("[Server] pthread_create");
            close(*client_fd);
            free(client_fd);
            pthread_mutex_lock(&thread_mutex);
            active_threads--;
            pthread_cond_signal(&thread_cv);
            pthread_mutex_unlock(&thread_mutex);
            continue;
        }
        pthread_detach(tid);  /* thread cleans up its own resources on exit */
    }

    /* Graceful shutdown: wait for all threads to finish before exiting */
    if (global_server_fd >= 0)
        close(global_server_fd);
    printf("\n[Server] Shutting down, waiting for %d active threads...\n",
           active_threads);
    pthread_mutex_lock(&thread_mutex);
    while (active_threads > 0)
        pthread_cond_wait(&thread_cv, &thread_mutex);
    pthread_mutex_unlock(&thread_mutex);
    printf("[Server] All threads finished. Goodbye!\n");

    return 0;
}
