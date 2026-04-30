/*
 * client.c  —  Interactive CLI client for the Algorithmic Online Judge.
 * Connects to the server over TCP, authenticates, then dispatches menu
 * actions as ClientRequest structs and prints the ServerResponse.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>

#include "database.h"

/* Discard leftover characters after scanf so the next prompt is clean */
static void flush_stdin(void)
{
    int c;
    while ((c = getchar()) != '\n' && c != EOF)
        ;
}

/* Read a local file into buf so it can be sent as the request payload */
static ssize_t read_local_file(const char *path, char *buf, size_t buf_size)
{
    FILE *fp = fopen(path, "r");
    if (!fp) { perror("  Cannot open file"); return -1; }
    size_t n = fread(buf, 1, buf_size - 1, fp);
    buf[n] = '\0';
    fclose(fp);
    return (ssize_t)n;
}

/* ── main ─────────────────────────────────────────────────────────────────── */
int main(void)
{
    printf("╔══════════════════════════════════════════╗\n");
    printf("║   Algorithmic Online Judge  -  Client    ║\n");
    printf("╚══════════════════════════════════════════╝\n\n");

    while (1) {
        /* ── Connect ── */
        int sock = socket(AF_INET, SOCK_STREAM, 0);
        if (sock < 0) { perror("socket"); exit(EXIT_FAILURE); }

        struct sockaddr_in serv_addr;
        memset(&serv_addr, 0, sizeof(serv_addr));
        serv_addr.sin_family = AF_INET;
        serv_addr.sin_port   = htons(PORT);
        inet_pton(AF_INET, SERVER_IP, &serv_addr.sin_addr);

        if (connect(sock, (struct sockaddr *)&serv_addr,
                    sizeof(serv_addr)) < 0) {
            perror("[Client] connect failed — is the server running?");
            close(sock);
            printf("Retrying in 3 seconds...\n\n");
            sleep(3);
            continue;
        }
        printf("[Client] Connected to %s:%d\n\n", SERVER_IP, PORT);

        /* Reconnect and re-authenticate after each registration or failed login */
        ClientRequest  req;
        ServerResponse res;

        memset(&req, 0, sizeof(req));

        printf("┌───────────────────────────────┐\n");
        printf("│  1. Login                     │\n");
        printf("│  2. Register                  │\n");
        printf("│  3. Enter as Spectator        │\n");
        printf("│  4. Exit                      │\n");
        printf("└───────────────────────────────┘\n");
        printf("  Choice: ");

        int init_choice;
        if (scanf("%d", &init_choice) != 1) {
            if (feof(stdin)) { close(sock); goto done; }
            printf("Invalid input.\n"); flush_stdin();
            close(sock); continue;
        }
        flush_stdin();

        if (init_choice == 4) {
            printf("Exiting client...\n");
            close(sock);
            goto done;
        }

        if (init_choice == 1) {
            req.action = ACTION_LOGIN;
            printf("──────────── LOGIN ────────────\n");
        } else if (init_choice == 2) {
            req.action = ACTION_REGISTER;
            printf("────────── REGISTER ───────────\n");
        } else if (init_choice == 3) {
            req.action = ACTION_SPECTATOR;
            printf("────────── SPECTATOR ──────────\n");
        } else {
            printf("Invalid choice.\n\n");
            close(sock); continue;
        }

        if (req.action != ACTION_SPECTATOR) {
            printf("  User ID  : ");
            if (scanf("%d", &req.user_id) != 1) {
                if (feof(stdin)) { close(sock); goto done; }
                printf("Invalid input.\n"); flush_stdin();
                close(sock); continue;
            }
            flush_stdin();

            printf("  Password : ");
            if (scanf("%49s", req.password) != 1) {
                if (feof(stdin)) { close(sock); goto done; }
                printf("Invalid input.\n"); flush_stdin();
                close(sock); continue;
            }
            flush_stdin();
            printf("───────────────────────────────\n");
        }

        send(sock, &req, sizeof(req), 0);
        recv(sock, &res, sizeof(res), 0);
        printf("\n  %s\n\n", res.message);

        /* Registration closes the connection; client loops and reconnects to log in */
        if (req.action == ACTION_REGISTER || res.status == STATUS_FAIL) {
            close(sock);
            continue;
        }

        int role = res.status;
        int running = 1;

        while (running) {
            memset(&req, 0, sizeof(req));
            memset(&res, 0, sizeof(res));

            /* ── ADMIN MENU ───────────────────────────────────────────────── */
            if (role == ROLE_ADMIN) {
                printf("┌─── Admin / Problem Setter ───┐\n");
                printf("│  1. Create / Update Problem   │\n");
                printf("│  2. Upload Test Cases         │\n");
                printf("│  3. View System Logs          │\n");
                printf("│  4. Emergency Halt / Resume   │\n");
                printf("│  5. View Leaderboard          │\n");
                printf("│  6. Logout                    │\n");
                printf("└───────────────────────────────┘\n");
                printf("  Choice: ");

                int ch;
                if (scanf("%d", &ch) != 1) {
                    if (feof(stdin)) { running = 0; break; }
                    printf("Invalid.\n"); flush_stdin(); continue;
                }
                flush_stdin();

                switch (ch) {

                /* 1: payload is packed as "title\ndescription" for the server to split */
                case 1: {
                    req.action = ACTION_CREATE_PROBLEM;
                    printf("  Problem ID: ");
                    scanf("%d", &req.problem_id); flush_stdin();

                    char title[100], desc[512];
                    printf("  Title: ");
                    fgets(title, sizeof(title), stdin);
                    title[strcspn(title, "\n")] = '\0';

                    printf("  Description: ");
                    fgets(desc, sizeof(desc), stdin);
                    desc[strcspn(desc, "\n")] = '\0';

                    /* Pack title and description separated by \n */
                    snprintf(req.payload, sizeof(req.payload),
                             "%s\n%s", title, desc);

                    send(sock, &req, sizeof(req), 0);
                    recv(sock, &res, sizeof(res), 0);
                    printf("\n  %s\n\n", res.message);
                    break;
                }

                /* 2: sends input.txt then expected.txt as separate requests */
                case 2: {
                    printf("  Problem ID: ");
                    int pid;
                    scanf("%d", &pid); flush_stdin();

                    /* First request: upload input.txt */
                    char path[256];
                    printf("  Path to input file (input.txt): ");
                    scanf("%255s", path); flush_stdin();

                    req.action = ACTION_UPLOAD_INPUT;
                    req.problem_id = pid;
                    if (read_local_file(path, req.payload,
                                        sizeof(req.payload)) >= 0) {
                        send(sock, &req, sizeof(req), 0);
                        recv(sock, &res, sizeof(res), 0);
                        printf("  %s\n", res.message);
                    }

                    /* Second request: upload expected.txt */
                    printf("  Path to expected output file (expected.txt): ");
                    scanf("%255s", path); flush_stdin();

                    memset(&req, 0, sizeof(req));
                    req.action = ACTION_UPLOAD_EXPECTED;
                    req.problem_id = pid;
                    if (read_local_file(path, req.payload,
                                        sizeof(req.payload)) >= 0) {
                        send(sock, &req, sizeof(req), 0);
                        recv(sock, &res, sizeof(res), 0);
                        printf("  %s\n\n", res.message);
                    }
                    break;
                }

                /* 3: server dumps its circular activity log */
                case 3:
                    req.action = ACTION_VIEW_LOGS;
                    send(sock, &req, sizeof(req), 0);
                    recv(sock, &res, sizeof(res), 0);
                    printf("\n%s\n", res.message);
                    break;

                /* 4: server sends SIGUSR1 to itself to toggle the halt flag */
                case 4:
                    req.action = ACTION_HALT_SYSTEM;
                    send(sock, &req, sizeof(req), 0);
                    recv(sock, &res, sizeof(res), 0);
                    printf("\n  ⚠  %s\n\n", res.message);
                    break;

                /* 5: leaderboard read under F_RDLCK — safe to view at any time */
                case 5:
                    req.action = ACTION_LEADERBOARD;
                    send(sock, &req, sizeof(req), 0);
                    recv(sock, &res, sizeof(res), 0);
                    printf("\n%s\n", res.message);
                    break;

                /* 6: break the inner loop; outer loop reconnects for next user */
                case 6:
                    running = 0;
                    break;

                default:
                    printf("  Invalid choice.\n\n");
                    break;
                }

            /* ── SPECTATOR MENU (read-only: F_RDLCK only, never writes) ─── */
            } else if (role == ROLE_SPECTATOR) {
                printf("┌─── Spectator Menu ───────────┐\n");
                printf("│  1. View Available Problems   │\n");
                printf("│  2. View Leaderboard          │\n");
                printf("│  3. Logout                    │\n");
                printf("└───────────────────────────────┘\n");
                printf("  Choice: ");

                int ch;
                if (scanf("%d", &ch) != 1) {
                    if (feof(stdin)) { running = 0; break; }
                    printf("Invalid.\n"); flush_stdin(); continue;
                }
                flush_stdin();

                switch (ch) {

                /* 1: view problems — F_RDLCK; spectators share the lock with other readers */
                case 1:
                    req.action = ACTION_VIEW_PROBLEMS;
                    send(sock, &req, sizeof(req), 0);
                    recv(sock, &res, sizeof(res), 0);
                    printf("\n%s\n", res.message);
                    break;

                /* 2: view leaderboard — F_RDLCK; concurrent spectators never block each other */
                case 2:
                    req.action = ACTION_LEADERBOARD;
                    send(sock, &req, sizeof(req), 0);
                    recv(sock, &res, sizeof(res), 0);
                    printf("\n%s\n", res.message);
                    break;

                /* 3: end the session */
                case 3:
                    running = 0;
                    break;

                default:
                    printf("  Invalid choice.\n\n");
                    break;
                }

            /* ── CONTESTANT MENU ──────────────────────────────────────────── */
            } else {
                printf("┌─── Contestant Menu ──────────┐\n");
                printf("│  1. View Available Problems   │\n");
                printf("│  2. Submit Solution           │\n");
                printf("│  3. View Leaderboard          │\n");
                printf("│  4. Logout                    │\n");
                printf("└───────────────────────────────┘\n");
                printf("  Choice: ");

                int ch;
                if (scanf("%d", &ch) != 1) {
                    if (feof(stdin)) { running = 0; break; }
                    printf("Invalid.\n"); flush_stdin(); continue;
                }
                flush_stdin();

                switch (ch) {

                /* 1: view problems under F_RDLCK */
                case 1:
                    req.action = ACTION_VIEW_PROBLEMS;
                    send(sock, &req, sizeof(req), 0);
                    recv(sock, &res, sizeof(res), 0);
                    printf("\n%s\n", res.message);
                    break;

                /* 2: read the local file, detect extension, send to sandbox */
                case 2: {
                    printf("  Problem ID: ");
                    scanf("%d", &req.problem_id); flush_stdin();

                    char filepath[256];
                    printf("  Path to source file (.cpp/.c): ");
                    if (scanf("%255s", filepath) != 1) {
                        if (feof(stdin)) { running = 0; break; }
                        printf("Invalid.\n"); flush_stdin(); break;
                    }
                    flush_stdin();

                    ssize_t n = read_local_file(filepath, req.payload,
                                                sizeof(req.payload));
                    if (n < 0) break;

                    /* File extension tells the server which compiler to use */
                    const char *dot = strrchr(filepath, '.');
                    if (dot) {
                        strncpy(req.file_ext, dot, sizeof(req.file_ext) - 1);
                    } else {
                        strcpy(req.file_ext, ".cpp");  /* default to C++ */
                    }

                    req.action = ACTION_SUBMIT;
                    printf("  Submitting %zd bytes to Problem %d (%s)...\n",
                           n, req.problem_id,
                           strcmp(req.file_ext, ".c") == 0 ? "C" : "C++");

                    send(sock, &req, sizeof(req), 0);
                    recv(sock, &res, sizeof(res), 0);

                    printf("\n  ╔════════════════════════════════════╗\n");
                    printf("  ║  %s\n", res.message);
                    printf("  ╚════════════════════════════════════╝\n\n");
                    break;
                }

                /* 3: view leaderboard under F_RDLCK */
                case 3:
                    req.action = ACTION_LEADERBOARD;
                    send(sock, &req, sizeof(req), 0);
                    recv(sock, &res, sizeof(res), 0);
                    printf("\n%s\n", res.message);
                    break;

                /* 4: end session */
                case 4:
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
        if (feof(stdin)) break;
    }

done:
    return 0;
}
