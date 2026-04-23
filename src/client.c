/*
 * client.c - CLI Terminal Client
 * Algorithmic Online Judge
 *
 * OS Concepts Demonstrated:
 *   - TCP Sockets : socket(), connect(), send(), recv()
 *   - File I/O    : fopen/fread to load .cpp files from disk
 *
 * Flow:
 *   1. Connect to server at 127.0.0.1:8080
 *   2. Prompt user for login (id + password)
 *   3. On success, display role-based menu in a while(1) loop:
 *      - Admin:      View Leaderboard, Logout
 *      - Contestant: Submit Solution, View Leaderboard, Logout
 *   4. On logout, close socket and restart login prompt
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>

#include "database.h"

/* ─── Helper: flush stdin after scanf ─── */
static void flush_stdin(void)
{
    int c;
    while ((c = getchar()) != '\n' && c != EOF)
        ;
}

/* ───────────────────────────────────────────────────────────
 * main()
 *
 * Outer loop: reconnects and re-authenticates on each logout.
 * Inner loop: menu-driven actions until user chooses logout.
 * ─────────────────────────────────────────────────────────── */
int main(void)
{
    printf("╔══════════════════════════════════════════╗\n");
    printf("║   Algorithmic Online Judge  -  Client    ║\n");
    printf("╚══════════════════════════════════════════╝\n\n");

    while (1) {
        /* ── Step 1: Create socket and connect ── */
        int sock = socket(AF_INET, SOCK_STREAM, 0);
        if (sock < 0) {
            perror("[Client] socket() failed");
            exit(EXIT_FAILURE);
        }

        struct sockaddr_in serv_addr;
        memset(&serv_addr, 0, sizeof(serv_addr));
        serv_addr.sin_family = AF_INET;
        serv_addr.sin_port   = htons(PORT);
        inet_pton(AF_INET, SERVER_IP, &serv_addr.sin_addr);

        if (connect(sock, (struct sockaddr *)&serv_addr,
                    sizeof(serv_addr)) < 0) {
            perror("[Client] connect() failed — is the server running?");
            close(sock);
            printf("Retrying in 3 seconds...\n\n");
            sleep(3);
            continue;
        }
        printf("[Client] Connected to %s:%d\n\n", SERVER_IP, PORT);

        /* ── Step 2: Login ── */
        ClientRequest  req;
        ServerResponse res;

        memset(&req, 0, sizeof(req));
        req.action = ACTION_LOGIN;

        printf("──────────── LOGIN ────────────\n");
        printf("  User ID  : ");
        if (scanf("%d", &req.user_id) != 1) {
            if (feof(stdin)) { close(sock); goto done; }
            printf("Invalid input.\n");
            flush_stdin();
            close(sock);
            continue;
        }
        flush_stdin();

        printf("  Password : ");
        if (scanf("%49s", req.password) != 1) {
            if (feof(stdin)) { close(sock); goto done; }
            printf("Invalid input.\n");
            flush_stdin();
            close(sock);
            continue;
        }
        flush_stdin();
        printf("───────────────────────────────\n");

        send(sock, &req, sizeof(req), 0);
        recv(sock, &res, sizeof(res), 0);

        printf("\n  %s\n\n", res.message);

        if (res.status == STATUS_FAIL) {
            close(sock);
            continue;   /* Back to login */
        }

        int role = res.status;

        /* ── Step 3: Role-based Menu Loop ── */
        int running = 1;
        while (running) {
            memset(&req, 0, sizeof(req));
            memset(&res, 0, sizeof(res));

            if (role == ROLE_ADMIN) {
                /* ─── Admin Menu ─── */
                printf("┌─── Admin Menu ───────────────┐\n");
                printf("│  1. View Leaderboard         │\n");
                printf("│  2. Logout                   │\n");
                printf("└──────────────────────────────┘\n");
                printf("  Choice: ");

                int choice;
                if (scanf("%d", &choice) != 1) {
                    if (feof(stdin)) { running = 0; break; }
                    printf("Invalid input.\n");
                    flush_stdin();
                    continue;
                }
                flush_stdin();

                switch (choice) {
                case 1:
                    req.action = ACTION_LEADERBOARD;
                    send(sock, &req, sizeof(req), 0);
                    recv(sock, &res, sizeof(res), 0);
                    printf("\n%s\n", res.message);
                    break;
                case 2:
                    running = 0;
                    break;
                default:
                    printf("  Invalid choice.\n\n");
                    break;
                }

            } else {
                /* ─── Contestant Menu ─── */
                printf("┌─── Contestant Menu ──────────┐\n");
                printf("│  1. Submit Solution (.cpp)   │\n");
                printf("│  2. View Leaderboard         │\n");
                printf("│  3. Logout                   │\n");
                printf("└──────────────────────────────┘\n");
                printf("  Choice: ");

                int choice;
                if (scanf("%d", &choice) != 1) {
                    if (feof(stdin)) { running = 0; break; }
                    printf("Invalid input.\n");
                    flush_stdin();
                    continue;
                }
                flush_stdin();

                switch (choice) {
                case 1: {
                    /* Read a .cpp file from disk and send as payload */
                    char filepath[256];
                    printf("  Enter .cpp file path: ");
                    if (scanf("%255s", filepath) != 1) {
                        if (feof(stdin)) { running = 0; break; }
                        printf("Invalid input.\n");
                        flush_stdin();
                        break;
                    }
                    flush_stdin();

                    FILE *fp = fopen(filepath, "r");
                    if (!fp) {
                        perror("  Cannot open file");
                        break;
                    }

                    req.action  = ACTION_SUBMIT;
                    req.user_id = 0;  /* not needed post-login */
                    size_t n = fread(req.payload, 1,
                                     sizeof(req.payload) - 1, fp);
                    req.payload[n] = '\0';
                    fclose(fp);

                    printf("  Submitting %zu bytes...\n", n);

                    send(sock, &req, sizeof(req), 0);
                    recv(sock, &res, sizeof(res), 0);

                    printf("\n  ╔═══════════════════════════╗\n");
                    printf("  ║  %s\n", res.message);
                    printf("  ╚═══════════════════════════╝\n\n");
                    break;
                }
                case 2:
                    req.action = ACTION_LEADERBOARD;
                    send(sock, &req, sizeof(req), 0);
                    recv(sock, &res, sizeof(res), 0);
                    printf("\n%s\n", res.message);
                    break;
                case 3:
                    running = 0;
                    break;
                default:
                    printf("  Invalid choice.\n\n");
                    break;
                }
            }
        }

        printf("[Client] Logged out.\n\n");
        close(sock);

        /* Exit if stdin is exhausted (piped/automated input) */
        if (feof(stdin)) break;
    }

done:

    return 0;
}
