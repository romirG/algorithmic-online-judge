/*
 * server.c - Main Server Daemon with Threaded Client Handling
 * Algorithmic Online Judge
 *
 * OS Concepts Demonstrated:
 *   - TCP Sockets    : socket(), bind(), listen(), accept()
 *   - Multithreading : pthread_create(), pthread_detach()
 *   - Mutex          : pthread_mutex_t protects temp.cpp writes
 *   - Integration    : Links auth, sandbox, and database modules
 *
 * Flow:
 *   1. Initialize database (seed users & leaderboard)
 *   2. Create TCP socket bound to 127.0.0.1:8080
 *   3. listen() for incoming connections
 *   4. accept() → pthread_create() a detached client_handler thread
 *   5. Each thread: authenticates, then serves a request loop
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <pthread.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>

#include "database.h"
#include "auth.h"
#include "sandbox.h"

/* ─── Global Mutex ───
 *
 * CONSTRAINT: evaluation_mutex MUST be locked before any thread
 * writes the .cpp payload to temp.cpp on disk, preventing race
 * conditions between concurrent submissions.
 */
pthread_mutex_t evaluation_mutex = PTHREAD_MUTEX_INITIALIZER;

/* ───────────────────────────────────────────────────────────
 * client_handler()
 *
 * Entry point for each per-client worker thread.
 * Handles login, then enters a request loop dispatching
 * submissions and leaderboard queries.
 * ─────────────────────────────────────────────────────────── */
void *client_handler(void *arg)
{
    int client_fd = *(int *)arg;
    free(arg);   /* Heap-allocated by accept loop */

    ClientRequest  req;
    ServerResponse res;

    /* ── Phase 1: Mandatory Login ── */
    memset(&req, 0, sizeof(req));
    memset(&res, 0, sizeof(res));

    ssize_t bytes = recv(client_fd, &req, sizeof(req), 0);
    if (bytes <= 0 || req.action != ACTION_LOGIN) {
        res.status = STATUS_FAIL;
        snprintf(res.message, sizeof(res.message),
                 "Error: First request must be LOGIN.");
        send(client_fd, &res, sizeof(res), 0);
        close(client_fd);
        printf("[Server] Client disconnected (no login)\n");
        return NULL;
    }

    int role = 0;
    if (authenticate_user(req.user_id, req.password, &role)) {
        res.status = role;   /* Send role back as status */
        snprintf(res.message, sizeof(res.message),
                 "Login successful. Welcome, User %d (role=%s).",
                 req.user_id,
                 role == ROLE_ADMIN ? "Admin" : "Contestant");
    } else {
        res.status = STATUS_FAIL;
        snprintf(res.message, sizeof(res.message),
                 "Authentication failed. Invalid ID or password.");
    }

    send(client_fd, &res, sizeof(res), 0);

    if (res.status == STATUS_FAIL) {
        close(client_fd);
        return NULL;
    }

    int user_id = req.user_id;
    printf("[Server] User %d logged in (role=%s)\n",
           user_id, role == ROLE_ADMIN ? "Admin" : "Contestant");

    /* ── Phase 2: Request Loop ── */
    while (1) {
        memset(&req, 0, sizeof(req));
        memset(&res, 0, sizeof(res));

        bytes = recv(client_fd, &req, sizeof(req), 0);
        if (bytes <= 0) {
            printf("[Server] User %d disconnected\n", user_id);
            break;
        }

        switch (req.action) {

        /* ── Submit Code ── */
        case ACTION_SUBMIT:
            if (role != ROLE_CONTESTANT) {
                res.status = STATUS_FAIL;
                snprintf(res.message, sizeof(res.message),
                         "Permission denied. Only Contestants may submit.");
                break;
            }

            printf("[Server] User %d submitted code (%zu bytes)\n",
                   user_id, strlen(req.payload));

            /*
             * MUTEX LOCK: Protect temp.cpp from concurrent writes.
             * The mutex is held for the entire compile+run cycle
             * since both steps share the same temp files.
             */
            pthread_mutex_lock(&evaluation_mutex);
            printf("[Server] evaluation_mutex LOCKED by user %d\n", user_id);

            int verdict = evaluate_submission(req.payload);

            printf("[Server] evaluation_mutex UNLOCKED by user %d\n", user_id);
            pthread_mutex_unlock(&evaluation_mutex);

            if (verdict == 1) {
                /* Accepted → update leaderboard */
                update_leaderboard(user_id);
                res.status = STATUS_OK;
                snprintf(res.message, sizeof(res.message),
                         "Verdict: ACCEPTED ✓");
            } else {
                res.status = STATUS_FAIL;
                snprintf(res.message, sizeof(res.message),
                         "Verdict: WRONG ANSWER ✗");
            }
            break;

        /* ── View Leaderboard ── */
        case ACTION_LEADERBOARD:
            res.status = STATUS_OK;
            get_leaderboard(res.message, sizeof(res.message));
            break;

        /* ── Unknown Action ── */
        default:
            res.status = STATUS_FAIL;
            snprintf(res.message, sizeof(res.message),
                     "Unknown action code: %d", req.action);
            break;
        }

        send(client_fd, &res, sizeof(res), 0);
    }

    close(client_fd);
    return NULL;
}

/* ───────────────────────────────────────────────────────────
 * main()
 *
 * 1. Initializes database
 * 2. Creates and binds a TCP socket
 * 3. Loops on accept(), spawning detached threads
 * ─────────────────────────────────────────────────────────── */
int main(void)
{
    printf("╔══════════════════════════════════════════╗\n");
    printf("║   Algorithmic Online Judge  -  Server    ║\n");
    printf("╚══════════════════════════════════════════╝\n\n");

    /* Step 1: Initialize database files */
    init_database();
    printf("\n");

    /* Step 2: Create TCP socket */
    int server_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (server_fd < 0) {
        perror("[Server] socket() failed");
        exit(EXIT_FAILURE);
    }

    /* Allow port reuse to avoid "Address already in use" */
    int opt = 1;
    if (setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR,
                   &opt, sizeof(opt)) < 0) {
        perror("[Server] setsockopt SO_REUSEADDR");
        close(server_fd);
        exit(EXIT_FAILURE);
    }

    /* Bind to 127.0.0.1:PORT */
    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family      = AF_INET;
    addr.sin_addr.s_addr = inet_addr(SERVER_IP);
    addr.sin_port        = htons(PORT);

    if (bind(server_fd, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        perror("[Server] bind() failed");
        close(server_fd);
        exit(EXIT_FAILURE);
    }

    /* Listen with a backlog of 10 */
    if (listen(server_fd, 10) < 0) {
        perror("[Server] listen() failed");
        close(server_fd);
        exit(EXIT_FAILURE);
    }

    printf("[Server] Listening on %s:%d  (Ctrl+C to stop)\n\n",
           SERVER_IP, PORT);

    /* Step 3: Accept loop — spawn detached threads */
    while (1) {
        struct sockaddr_in client_addr;
        socklen_t client_len = sizeof(client_addr);

        int *client_fd = malloc(sizeof(int));
        if (!client_fd) {
            perror("[Server] malloc");
            continue;
        }

        *client_fd = accept(server_fd,
                            (struct sockaddr *)&client_addr,
                            &client_len);
        if (*client_fd < 0) {
            perror("[Server] accept() failed");
            free(client_fd);
            continue;
        }

        printf("[Server] New connection from %s:%d\n",
               inet_ntoa(client_addr.sin_addr),
               ntohs(client_addr.sin_port));

        /* Spawn a detached worker thread */
        pthread_t tid;
        if (pthread_create(&tid, NULL, client_handler, client_fd) != 0) {
            perror("[Server] pthread_create");
            close(*client_fd);
            free(client_fd);
            continue;
        }
        pthread_detach(tid);
    }

    close(server_fd);
    return 0;
}
