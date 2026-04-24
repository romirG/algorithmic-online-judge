/*
 * database.c - Database Init, Problem CRUD, File-Locked Updates (v2)
 *
 * OS Concepts:
 *   - fcntl() F_WRLCK on problems.dat   (create_problem)
 *   - fcntl() F_RDLCK on problems.dat   (get_problems)
 *   - fcntl() F_WRLCK on leaderboard.dat (update_leaderboard)
 *   - fcntl() F_RDLCK on leaderboard.dat (get_leaderboard)
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <errno.h>

#include "database.h"

/* ── Helper: recursively create directories ── */
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

/* ───────────────────────────────────────────────────────────
 * init_database()
 * ─────────────────────────────────────────────────────────── */
void init_database(void)
{
    /* Create directories */
    mkdirs(DATA_DIR);
    mkdirs(TESTCASES_DIR);

    /* ── Seed users.dat ── */
    FILE *fp = fopen(USERS_FILE, "wb");
    if (!fp) { perror("fopen users.dat"); exit(EXIT_FAILURE); }

    User admin      = { .id = 1, .password = "admin123", .role = ROLE_ADMIN };
    User contestant = { .id = 2, .password = "pass123",  .role = ROLE_CONTESTANT };

    fwrite(&admin,      sizeof(User), 1, fp);
    fwrite(&contestant, sizeof(User), 1, fp);
    fclose(fp);
    printf("[DB] users.dat seeded (Admin id:1, Contestant id:2)\n");

    /* ── Seed leaderboard.dat ── */
    fp = fopen(LEADERBOARD_FILE, "wb");
    if (!fp) { perror("fopen leaderboard.dat"); exit(EXIT_FAILURE); }

    ScoreRecord s1 = { .user_id = 1, .solved_count = 0 };
    ScoreRecord s2 = { .user_id = 2, .solved_count = 0 };
    fwrite(&s1, sizeof(ScoreRecord), 1, fp);
    fwrite(&s2, sizeof(ScoreRecord), 1, fp);
    fclose(fp);
    printf("[DB] leaderboard.dat seeded\n");

    /* ── Seed problems.dat with a default problem ── */
    fp = fopen(PROBLEMS_FILE, "wb");
    if (!fp) { perror("fopen problems.dat"); exit(EXIT_FAILURE); }

    Problem p1 = {
        .id = 1,
        .title = "Hello World",
        .description = "Write a program that prints exactly: Hello World",
        .active = 1
    };
    fwrite(&p1, sizeof(Problem), 1, fp);
    fclose(fp);
    printf("[DB] problems.dat seeded (Problem 1: Hello World)\n");

    /* ── Seed default test case for Problem 1 ── */
    mkdirs("data/testcases/1");

    fp = fopen("data/testcases/1/input.txt", "w");
    if (fp) { fclose(fp); }  /* Empty input file */

    fp = fopen("data/testcases/1/expected.txt", "w");
    if (fp) { fprintf(fp, "Hello World\n"); fclose(fp); }

    printf("[DB] Default test case seeded for Problem 1\n");
}

/* ───────────────────────────────────────────────────────────
 * create_problem()
 *
 * CONSTRAINT: fcntl() F_WRLCK on problems.dat to ensure no
 *             contestant reads while admin is writing.
 * ─────────────────────────────────────────────────────────── */
int create_problem(int id, const char *title, const char *description)
{
    int fd = open(PROBLEMS_FILE, O_RDWR | O_CREAT, 0644);
    if (fd < 0) { perror("open problems.dat"); return 0; }

    /* Exclusive write lock */
    struct flock fl = { .l_type = F_WRLCK, .l_whence = SEEK_SET,
                        .l_start = 0, .l_len = 0 };
    printf("[DB] Acquiring WRITE lock on problems.dat ...\n");
    if (fcntl(fd, F_SETLKW, &fl) < 0) {
        perror("fcntl F_WRLCK problems.dat");
        close(fd);
        return 0;
    }
    printf("[DB] WRITE lock acquired on problems.dat\n");

    /* Check if problem ID already exists → update it */
    Problem rec;
    int found = 0;
    while (read(fd, &rec, sizeof(Problem)) == sizeof(Problem)) {
        if (rec.id == id) {
            strncpy(rec.title, title, sizeof(rec.title) - 1);
            strncpy(rec.description, description, sizeof(rec.description) - 1);
            rec.active = 1;
            lseek(fd, -(off_t)sizeof(Problem), SEEK_CUR);
            write(fd, &rec, sizeof(Problem));
            found = 1;
            printf("[DB] Problem %d updated\n", id);
            break;
        }
    }

    if (!found) {
        /* Append new problem */
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

    /* Create testcase directory for this problem */
    char dir[256];
    snprintf(dir, sizeof(dir), "%s/%d", TESTCASES_DIR, id);
    mkdirs(dir);

    /* Release lock */
    fl.l_type = F_UNLCK;
    fcntl(fd, F_SETLK, &fl);
    printf("[DB] WRITE lock released on problems.dat\n");

    close(fd);
    return 1;
}

/* ───────────────────────────────────────────────────────────
 * get_problems()
 *
 * CONSTRAINT: fcntl() F_RDLCK on problems.dat — advisory
 *             read lock prevents reads during admin writes.
 * ─────────────────────────────────────────────────────────── */
int get_problems(char *buffer, int buf_size)
{
    int fd = open(PROBLEMS_FILE, O_RDONLY);
    if (fd < 0) {
        snprintf(buffer, buf_size, "No problems available.");
        return -1;
    }

    /* Advisory read lock */
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

    if (count == 0) {
        offset += snprintf(buffer + offset, buf_size - offset,
                           "  (none)\n");
    }

    fl.l_type = F_UNLCK;
    fcntl(fd, F_SETLK, &fl);
    printf("[DB] READ lock released on problems.dat\n");

    close(fd);
    return count;
}

/* ───────────────────────────────────────────────────────────
 * save_testcase_file()
 *
 * Writes content to data/testcases/<problem_id>/<filename>.
 * Used for uploading input.txt and expected.txt.
 * ─────────────────────────────────────────────────────────── */
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

/* ───────────────────────────────────────────────────────────
 * update_leaderboard()
 *
 * CONSTRAINT: fcntl() F_WRLCK on leaderboard.dat
 * ─────────────────────────────────────────────────────────── */
int update_leaderboard(int user_id)
{
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
            lseek(fd, -(off_t)sizeof(ScoreRecord), SEEK_CUR);
            write(fd, &rec, sizeof(ScoreRecord));
            printf("[DB] User %d score → %d\n", user_id, rec.solved_count);
            found = 1;
            break;
        }
    }

    if (!found) {
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

/* ───────────────────────────────────────────────────────────
 * get_leaderboard()
 *
 * CONSTRAINT: fcntl() F_RDLCK on leaderboard.dat
 * ─────────────────────────────────────────────────────────── */
int get_leaderboard(char *buffer, int buf_size)
{
    int fd = open(LEADERBOARD_FILE, O_RDONLY);
    if (fd < 0) {
        snprintf(buffer, buf_size, "Leaderboard unavailable.");
        return -1;
    }

    /* Advisory read lock */
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


    /* Read all records into array */
    #define MAX_USERS 100
    ScoreRecord records[MAX_USERS];
    int count = 0;
    ScoreRecord rec;

    while (count < MAX_USERS &&
           read(fd, &rec, sizeof(ScoreRecord)) == sizeof(ScoreRecord)) {
        records[count++] = rec;
    }

    /* Sort by solved_count descending (simple bubble sort) */
    for (int i = 0; i < count - 1; i++) {
        for (int j = 0; j < count - 1 - i; j++) {
            if (records[j].solved_count < records[j + 1].solved_count) {
                ScoreRecord tmp = records[j];
                records[j] = records[j + 1];
                records[j + 1] = tmp;
            }
        }
    }

    /* Format ranked leaderboard */
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
                           i + 1, records[i].user_id,
                           records[i].solved_count);
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

/* ── Conditional main for standalone init_db binary ── */
#ifdef STANDALONE_INIT
int main(void)
{
    printf("=== Initializing Online Judge Database ===\n");
    init_database();
    printf("=== Database initialization complete ===\n");
    return 0;
}
#endif
