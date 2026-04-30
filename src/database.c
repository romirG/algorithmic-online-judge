/*
 * database.c  —  Binary flat-file database for users, problems, and the leaderboard.
 * Initialises data files on first run, and provides locked read/write access
 * to problems.dat, leaderboard.dat, and solved.dat for all server threads.
 *
 * OS concepts used:
 *   - fcntl() F_WRLCK : exclusive write lock on problems.dat and leaderboard.dat
 *   - fcntl() F_RDLCK : shared read lock (multiple readers allowed simultaneously)
 *   - pthread_mutex_t  : serializes leaderboard writes within the same process
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <errno.h>
#include <pthread.h>

#include "database.h"

/* ── Helper: create a directory path component by component ─────────────── */
static void mkdirs(const char *path)
{
    char tmp[256];
    snprintf(tmp, sizeof(tmp), "%s", path);
    for (char *p = tmp + 1; *p; p++) {
        if (*p == '/') {
            *p = '\0';
            mkdir(tmp, 0755);
            *p = '/';
        }
    }
    mkdir(tmp, 0755);
}

/* ── init_database ──────────────────────────────────────────────────────── */
/* Seeds all binary data files with default records on first run. */
void init_database(void)
{
    mkdirs(DATA_DIR);
    mkdirs(TESTCASES_DIR);

    /* users.dat: two default accounts — one admin, one contestant */
    FILE *fp = fopen(USERS_FILE, "wb");
    if (!fp) { perror("fopen users.dat"); exit(EXIT_FAILURE); }

    User admin      = { .id = 1, .password = "admin123", .role = ROLE_ADMIN };
    User contestant = { .id = 2, .password = "pass123",  .role = ROLE_CONTESTANT };

    fwrite(&admin,      sizeof(User), 1, fp);
    fwrite(&contestant, sizeof(User), 1, fp);
    fclose(fp);
    printf("[DB] users.dat seeded (Admin id:1, Contestant id:2)\n");

    /* leaderboard.dat: only contestants appear; admin does not compete */
    fp = fopen(LEADERBOARD_FILE, "wb");
    if (!fp) { perror("fopen leaderboard.dat"); exit(EXIT_FAILURE); }

    ScoreRecord s2 = { .user_id = 2, .solved_count = 0 };
    fwrite(&s2, sizeof(ScoreRecord), 1, fp);
    fclose(fp);
    printf("[DB] leaderboard.dat seeded\n");

    /* solved.dat: tracks which (user, problem) pairs are already accepted
     * to prevent awarding duplicate points for the same problem. */
    fp = fopen(SOLVED_FILE, "wb");
    if (!fp) { perror("fopen solved.dat"); exit(EXIT_FAILURE); }
    fclose(fp);
    printf("[DB] solved.dat seeded\n");

    /* problems.dat: two sample problems */
    fp = fopen(PROBLEMS_FILE, "wb");
    if (!fp) { perror("fopen problems.dat"); exit(EXIT_FAILURE); }

    Problem p1 = {
        .id = 1,
        .title = "Hello World",
        .description = "Write a program that prints exactly: Hello World",
        .active = 1
    };
    Problem p2 = {
        .id = 2,
        .title = "Two Sum",
        .description = "Given N integers and target K. Print YES if two distinct numbers sum to K, else NO.",
        .active = 1
    };
    fwrite(&p1, sizeof(Problem), 1, fp);
    fwrite(&p2, sizeof(Problem), 1, fp);
    fclose(fp);
    printf("[DB] problems.dat seeded (Problems 1 and 2)\n");

    /* Test case for Problem 1: empty input, expected output = "Hello World\n" */
    mkdirs("data/testcases/1");
    fp = fopen("data/testcases/1/input.txt", "w");
    if (fp) fclose(fp);
    fp = fopen("data/testcases/1/expected.txt", "w");
    if (fp) { fprintf(fp, "Hello World\n"); fclose(fp); }
    printf("[DB] Default test case seeded for Problem 1\n");

    /* Test case for Problem 2 */
    mkdirs("data/testcases/2");
    fp = fopen("data/testcases/2/input.txt", "w");
    if (fp) { fprintf(fp, "5\n1 4 5 7 9\n12\n"); fclose(fp); }
    fp = fopen("data/testcases/2/expected.txt", "w");
    if (fp) { fprintf(fp, "YES\n"); fclose(fp); }
    printf("[DB] Default test case seeded for Problem 2\n");
}

/* ── create_problem ─────────────────────────────────────────────────────── */
/* F_WRLCK prevents contestants from reading problems.dat mid-write. */
int create_problem(int id, const char *title, const char *description)
{
    int fd = open(PROBLEMS_FILE, O_RDWR | O_CREAT, 0644);
    if (fd < 0) { perror("open problems.dat"); return 0; }

    struct flock fl = { .l_type = F_WRLCK, .l_whence = SEEK_SET,
                        .l_start = 0, .l_len = 0 };
    printf("[DB] Acquiring WRITE lock on problems.dat ...\n");
    if (fcntl(fd, F_SETLKW, &fl) < 0) {
        perror("fcntl F_WRLCK problems.dat");
        close(fd);
        return 0;
    }
    printf("[DB] WRITE lock acquired on problems.dat\n");

    /* Scan for existing record; update in-place if found, else append */
    Problem rec;
    int found = 0;
    while (read(fd, &rec, sizeof(Problem)) == sizeof(Problem)) {
        if (rec.id == id) {
            strncpy(rec.title, title, sizeof(rec.title) - 1);
            strncpy(rec.description, description, sizeof(rec.description) - 1);
            rec.active = 1;
            lseek(fd, -(off_t)sizeof(Problem), SEEK_CUR);  /* seek back to overwrite */
            write(fd, &rec, sizeof(Problem));
            found = 1;
            printf("[DB] Problem %d updated\n", id);
            break;
        }
    }

    if (!found) {
        Problem newp;
        memset(&newp, 0, sizeof(newp));
        newp.id = id;
        newp.active = 1;
        strncpy(newp.title, title, sizeof(newp.title) - 1);
        strncpy(newp.description, description, sizeof(newp.description) - 1);
        lseek(fd, 0, SEEK_END);
        write(fd, &newp, sizeof(Problem));
        printf("[DB] Problem %d created\n", id);
    }

    /* Ensure a testcase directory exists for this problem */
    char dir[256];
    snprintf(dir, sizeof(dir), "%s/%d", TESTCASES_DIR, id);
    mkdirs(dir);

    fl.l_type = F_UNLCK;
    fcntl(fd, F_SETLK, &fl);
    printf("[DB] WRITE lock released on problems.dat\n");

    close(fd);
    return 1;
}

/* ── get_problems ───────────────────────────────────────────────────────── */
/* F_RDLCK allows many contestants to read concurrently but blocks admin writes. */
int get_problems(char *buffer, int buf_size)
{
    int fd = open(PROBLEMS_FILE, O_RDONLY);
    if (fd < 0) {
        snprintf(buffer, buf_size, "No problems available.");
        return -1;
    }

    struct flock fl = { .l_type = F_RDLCK, .l_whence = SEEK_SET,
                        .l_start = 0, .l_len = 0 };
    printf("[DB] Acquiring READ lock on problems.dat ...\n");
    if (fcntl(fd, F_SETLKW, &fl) < 0) {
        perror("fcntl F_RDLCK problems.dat");
        close(fd);
        snprintf(buffer, buf_size, "Lock error.");
        return -1;
    }
    printf("[DB] READ lock acquired on problems.dat\n");

    int offset = 0, count = 0;
    Problem rec;
    offset += snprintf(buffer + offset, buf_size - offset,
                       "=== AVAILABLE PROBLEMS ===\n");

    while (read(fd, &rec, sizeof(Problem)) == sizeof(Problem)) {
        if (rec.active) {
            offset += snprintf(buffer + offset, buf_size - offset,
                               "\n  [Problem %d] %s\n  %s\n",
                               rec.id, rec.title, rec.description);
            count++;
        }
        if (offset >= buf_size - 1) break;
    }

    if (count == 0)
        offset += snprintf(buffer + offset, buf_size - offset, "  (none)\n");

    fl.l_type = F_UNLCK;
    fcntl(fd, F_SETLK, &fl);
    printf("[DB] READ lock released on problems.dat\n");

    close(fd);
    return count;
}

/* ── save_testcase_file ─────────────────────────────────────────────────── */
/* Writes admin-uploaded test-case content to the per-problem directory. */
int save_testcase_file(int problem_id, const char *filename,
                       const char *content)
{
    char dir[256], path[512];
    snprintf(dir, sizeof(dir), "%s/%d", TESTCASES_DIR, problem_id);
    mkdirs(dir);

    snprintf(path, sizeof(path), "%s/%s", dir, filename);
    FILE *fp = fopen(path, "w");
    if (!fp) {
        perror("fopen testcase");
        return 0;
    }
    fprintf(fp, "%s", content);
    fclose(fp);
    printf("[DB] Saved %s for Problem %d (%zu bytes)\n",
           filename, problem_id, strlen(content));
    return 1;
}

/* ── Mutex guards intra-process concurrency on the leaderboard ──────────── */
static pthread_mutex_t leaderboard_mutex = PTHREAD_MUTEX_INITIALIZER;

/* ── init_user_leaderboard ──────────────────────────────────────────────── */
/* Called once after a successful registration to seed a 0-score row. */
int init_user_leaderboard(int user_id)
{
    pthread_mutex_lock(&leaderboard_mutex);

    int fd = open(LEADERBOARD_FILE, O_RDWR);
    if (fd < 0) {
        perror("open leaderboard.dat");
        pthread_mutex_unlock(&leaderboard_mutex);
        return 0;
    }

    /* F_WRLCK ensures no concurrent reader sees a half-written record */
    struct flock fl = { .l_type = F_WRLCK, .l_whence = SEEK_SET,
                        .l_start = 0, .l_len = 0 };
    printf("[DB] Waiting for WRITE lock on leaderboard.dat ...\n");
    if (fcntl(fd, F_SETLKW, &fl) < 0) {
        perror("fcntl F_WRLCK leaderboard");
        close(fd);
        pthread_mutex_unlock(&leaderboard_mutex);
        return 0;
    }

    ScoreRecord new_rec = { .user_id = user_id, .solved_count = 0 };
    lseek(fd, 0, SEEK_END);
    write(fd, &new_rec, sizeof(ScoreRecord));

    fl.l_type = F_UNLCK;
    fcntl(fd, F_SETLK, &fl);
    close(fd);

    pthread_mutex_unlock(&leaderboard_mutex);
    return 1;
}

/* ── update_leaderboard ─────────────────────────────────────────────────── */
/* Atomically checks solved.dat and increments the score only for a new solve. */
int update_leaderboard(int user_id, int problem_id)
{
    /* Step 1: Check solved.dat under F_WRLCK (atomic check-and-set) */
    int fd_sol = open(SOLVED_FILE, O_RDWR | O_CREAT, 0644);
    if (fd_sol < 0) { perror("open solved.dat"); return 0; }

    struct flock fl_sol = { .l_type = F_WRLCK, .l_whence = SEEK_SET,
                            .l_start = 0, .l_len = 0 };
    if (fcntl(fd_sol, F_SETLKW, &fl_sol) < 0) {
        perror("fcntl F_WRLCK solved");
        close(fd_sol);
        return 0;
    }

    SolvedRecord srec;
    int already_solved = 0;
    while (read(fd_sol, &srec, sizeof(SolvedRecord)) == sizeof(SolvedRecord)) {
        if (srec.user_id == user_id && srec.problem_id == problem_id) {
            already_solved = 1;
            break;
        }
    }

    if (already_solved) {
        printf("[DB] User %d already solved problem %d. No points awarded.\n",
               user_id, problem_id);
        fl_sol.l_type = F_UNLCK;
        fcntl(fd_sol, F_SETLK, &fl_sol);
        close(fd_sol);
        return 1;
    }

    /* Mark this (user, problem) pair as solved */
    srec.user_id   = user_id;
    srec.problem_id = problem_id;
    lseek(fd_sol, 0, SEEK_END);
    write(fd_sol, &srec, sizeof(SolvedRecord));

    fl_sol.l_type = F_UNLCK;
    fcntl(fd_sol, F_SETLK, &fl_sol);
    close(fd_sol);

    /* Step 2: Increment the score in leaderboard.dat under F_WRLCK */
    int fd = open(LEADERBOARD_FILE, O_RDWR);
    if (fd < 0) { perror("open leaderboard.dat"); return 0; }

    struct flock fl = { .l_type = F_WRLCK, .l_whence = SEEK_SET,
                        .l_start = 0, .l_len = 0 };
    printf("[DB] Waiting for WRITE lock on leaderboard.dat ...\n");
    if (fcntl(fd, F_SETLKW, &fl) < 0) {
        perror("fcntl F_WRLCK leaderboard");
        close(fd);
        return 0;
    }
    printf("[DB] WRITE lock acquired (leaderboard) for user %d\n", user_id);

    ScoreRecord rec;
    int found = 0;
    while (read(fd, &rec, sizeof(ScoreRecord)) == sizeof(ScoreRecord)) {
        if (rec.user_id == user_id) {
            rec.solved_count++;
            lseek(fd, -(off_t)sizeof(ScoreRecord), SEEK_CUR);  /* seek back to overwrite */
            write(fd, &rec, sizeof(ScoreRecord));
            printf("[DB] User %d score → %d\n", user_id, rec.solved_count);
            found = 1;
            break;
        }
    }

    if (!found) {
        /* New user not yet on the board — append a fresh entry */
        ScoreRecord new_rec = { .user_id = user_id, .solved_count = 1 };
        lseek(fd, 0, SEEK_END);
        write(fd, &new_rec, sizeof(ScoreRecord));
        printf("[DB] New leaderboard entry for user %d\n", user_id);
    }

    fl.l_type = F_UNLCK;
    fcntl(fd, F_SETLK, &fl);
    printf("[DB] WRITE lock released (leaderboard)\n");

    close(fd);
    return 1;
}

/* ── get_leaderboard ────────────────────────────────────────────────────── */
/* F_RDLCK allows all three roles to read simultaneously without blocking each other. */
int get_leaderboard(char *buffer, int buf_size)
{
    int fd = open(LEADERBOARD_FILE, O_RDONLY);
    if (fd < 0) {
        snprintf(buffer, buf_size, "Leaderboard unavailable.");
        return -1;
    }

    struct flock fl = { .l_type = F_RDLCK, .l_whence = SEEK_SET,
                        .l_start = 0, .l_len = 0 };
    printf("[DB] Acquiring READ lock on leaderboard.dat ...\n");
    if (fcntl(fd, F_SETLKW, &fl) < 0) {
        perror("fcntl F_RDLCK leaderboard");
        close(fd);
        snprintf(buffer, buf_size, "Lock error.");
        return -1;
    }
    printf("[DB] READ lock acquired (leaderboard)\n");

    /* Load all records into memory so we can sort before formatting */
    #define MAX_USERS 100
    ScoreRecord records[MAX_USERS];
    int count = 0;
    ScoreRecord rec;
    while (count < MAX_USERS &&
           read(fd, &rec, sizeof(ScoreRecord)) == sizeof(ScoreRecord)) {
        records[count++] = rec;
    }

    /* Bubble sort descending by solved_count to produce ranked output */
    for (int i = 0; i < count - 1; i++) {
        for (int j = 0; j < count - 1 - i; j++) {
            if (records[j].solved_count < records[j + 1].solved_count) {
                ScoreRecord tmp = records[j];
                records[j]     = records[j + 1];
                records[j + 1] = tmp;
            }
        }
    }

    /* Format as an ASCII table */
    int offset = 0;
    offset += snprintf(buffer + offset, buf_size - offset,
        "╔══════════════════════════════════╗\n"
        "║         LEADERBOARD              ║\n"
        "╠══════╦══════════╦════════════════╣\n"
        "║ Rank ║ User ID  ║    Solved      ║\n"
        "╠══════╬══════════╬════════════════╣\n");

    for (int i = 0; i < count && offset < buf_size - 1; i++) {
        offset += snprintf(buffer + offset, buf_size - offset,
                           "║  #%-2d ║  User %-2d ║    %-3d         ║\n",
                           i + 1, records[i].user_id, records[i].solved_count);
    }

    offset += snprintf(buffer + offset, buf_size - offset,
        "╚══════╩══════════╩════════════════╝\n");
    (void)offset;

    fl.l_type = F_UNLCK;
    fcntl(fd, F_SETLK, &fl);
    printf("[DB] READ lock released (leaderboard)\n");

    close(fd);
    return count;
}

/* ── Conditional main: compiled into a standalone init_db binary ─────────── */
#ifdef STANDALONE_INIT
int main(void)
{
    printf("=== Initializing Online Judge Database ===\n");
    init_database();
    printf("=== Database initialization complete ===\n");
    return 0;
}
#endif
