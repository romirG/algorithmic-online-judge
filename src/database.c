/*
 * database.c - Database Initialization & File-Locked Updates
 * Algorithmic Online Judge
 *
 * OS Concepts Demonstrated:
 *   - fcntl() advisory WRITE locks (F_WRLCK) on leaderboard.dat
 *   - Binary file I/O with fread/fwrite for structured records
 *   - Conditional compilation with -DSTANDALONE_INIT for init_db target
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

/* ───────────────────────────────────────────────────────────
 * init_database()
 *
 * Creates the data/ directory (if absent), then seeds:
 *   - users.dat       with default Admin (id:1) and Contestant (id:2)
 *   - leaderboard.dat with zero-score records for both users
 * ─────────────────────────────────────────────────────────── */
void init_database(void)
{
    /* Create data/ directory (ignore EEXIST) */
    if (mkdir(DATA_DIR, 0755) < 0 && errno != EEXIST) {
        perror("mkdir data");
        exit(EXIT_FAILURE);
    }

    /* ── Seed users.dat ── */
    FILE *fp = fopen(USERS_FILE, "wb");
    if (!fp) {
        perror("fopen users.dat");
        exit(EXIT_FAILURE);
    }

    User admin      = { .id = 1, .password = "admin123", .role = ROLE_ADMIN };
    User contestant = { .id = 2, .password = "pass123",  .role = ROLE_CONTESTANT };

    fwrite(&admin,      sizeof(User), 1, fp);
    fwrite(&contestant, sizeof(User), 1, fp);
    fclose(fp);

    printf("[DB] users.dat seeded (Admin id:1, Contestant id:2)\n");

    /* ── Seed leaderboard.dat ── */
    fp = fopen(LEADERBOARD_FILE, "wb");
    if (!fp) {
        perror("fopen leaderboard.dat");
        exit(EXIT_FAILURE);
    }

    ScoreRecord s1 = { .user_id = 1, .solved_count = 0 };
    ScoreRecord s2 = { .user_id = 2, .solved_count = 0 };

    fwrite(&s1, sizeof(ScoreRecord), 1, fp);
    fwrite(&s2, sizeof(ScoreRecord), 1, fp);
    fclose(fp);

    printf("[DB] leaderboard.dat seeded (2 records)\n");
}

/* ───────────────────────────────────────────────────────────
 * update_leaderboard()
 *
 * Increments solved_count for the given user_id.
 *
 * CONSTRAINT: Uses fcntl() to apply an exclusive WRITE lock
 *             (F_WRLCK) on leaderboard.dat before modifying
 *             any record, preventing lost-update anomalies
 *             from concurrent threads.
 * ─────────────────────────────────────────────────────────── */
int update_leaderboard(int user_id)
{
    int fd = open(LEADERBOARD_FILE, O_RDWR);
    if (fd < 0) {
        perror("open leaderboard.dat");
        return 0;
    }

    /* Apply exclusive write lock on entire file */
    struct flock fl;
    memset(&fl, 0, sizeof(fl));
    fl.l_type   = F_WRLCK;      /* Exclusive write lock */
    fl.l_whence = SEEK_SET;
    fl.l_start  = 0;
    fl.l_len    = 0;             /* Lock entire file     */

    printf("[DB] Thread waiting for WRITE lock on leaderboard.dat ...\n");
    if (fcntl(fd, F_SETLKW, &fl) < 0) {   /* Blocking lock */
        perror("fcntl F_WRLCK");
        close(fd);
        return 0;
    }
    printf("[DB] WRITE lock acquired for user %d\n", user_id);

    /* Search for the user's ScoreRecord */
    ScoreRecord rec;
    int found = 0;

    while (read(fd, &rec, sizeof(ScoreRecord)) == sizeof(ScoreRecord)) {
        if (rec.user_id == user_id) {
            rec.solved_count++;

            /* Seek back to overwrite this record */
            lseek(fd, -(off_t)sizeof(ScoreRecord), SEEK_CUR);
            write(fd, &rec, sizeof(ScoreRecord));

            printf("[DB] User %d score updated to %d\n",
                   user_id, rec.solved_count);
            found = 1;
            break;
        }
    }

    if (!found) {
        /* Append new record if user not found */
        ScoreRecord new_rec = { .user_id = user_id, .solved_count = 1 };
        lseek(fd, 0, SEEK_END);
        write(fd, &new_rec, sizeof(ScoreRecord));
        printf("[DB] New leaderboard entry for user %d\n", user_id);
    }

    /* Release the write lock */
    fl.l_type = F_UNLCK;
    fcntl(fd, F_SETLK, &fl);
    printf("[DB] WRITE lock released\n");

    close(fd);
    return 1;
}

/* ───────────────────────────────────────────────────────────
 * get_leaderboard()
 *
 * Reads all ScoreRecords from leaderboard.dat and formats
 * them into a human-readable string.
 *
 * Returns: number of records read, or -1 on error.
 * ─────────────────────────────────────────────────────────── */
int get_leaderboard(char *buffer, int buf_size)
{
    FILE *fp = fopen(LEADERBOARD_FILE, "rb");
    if (!fp) {
        snprintf(buffer, buf_size, "Leaderboard unavailable.");
        return -1;
    }

    int offset = 0;
    int count  = 0;
    ScoreRecord rec;

    offset += snprintf(buffer + offset, buf_size - offset,
                       "=== LEADERBOARD ===\n");

    while (fread(&rec, sizeof(ScoreRecord), 1, fp) == 1) {
        offset += snprintf(buffer + offset, buf_size - offset,
                           "  User %d : %d solved\n",
                           rec.user_id, rec.solved_count);
        count++;
        if (offset >= buf_size - 1) break;
    }

    fclose(fp);
    return count;
}

/* ───────────────────────────────────────────────────────────
 * Conditional main() for standalone init_db binary.
 *
 * Compiled with:  gcc -DSTANDALONE_INIT database.c -o init_db
 * ─────────────────────────────────────────────────────────── */
#ifdef STANDALONE_INIT
int main(void)
{
    printf("=== Initializing Online Judge Database ===\n");
    init_database();
    printf("=== Database initialization complete ===\n");
    return 0;
}
#endif
