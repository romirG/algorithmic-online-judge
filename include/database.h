/*
 * database.h - Core Data Structures & Macros
 * Algorithmic Online Judge
 *
 * Defines all shared structures for Users, Scores,
 * Client-Server protocol messages, and file paths.
 */

#ifndef DATABASE_H
#define DATABASE_H

/* ─── Network Configuration ─── */
#define PORT            8080
#define SERVER_IP       "127.0.0.1"

/* ─── Data File Paths ─── */
#define DATA_DIR        "data"
#define USERS_FILE      "data/users.dat"
#define LEADERBOARD_FILE "data/leaderboard.dat"

/* ─── Action Codes (ClientRequest.action) ─── */
#define ACTION_LOGIN        1
#define ACTION_SUBMIT       2
#define ACTION_LEADERBOARD  3

/* ─── Role Codes (User.role) ─── */
#define ROLE_ADMIN       1   /* Admin / Problem Setter */
#define ROLE_CONTESTANT  2   /* Contestant             */

/* ─── Status Codes (ServerResponse.status) ─── */
#define STATUS_OK        1
#define STATUS_FAIL     -1

/* ─── Data Structures ─── */

typedef struct {
    int  id;
    char password[50];
    int  role;           /* ROLE_ADMIN or ROLE_CONTESTANT */
} User;

typedef struct {
    int user_id;
    int solved_count;
} ScoreRecord;

typedef struct {
    int  action;         /* ACTION_LOGIN, ACTION_SUBMIT, ACTION_LEADERBOARD */
    int  user_id;        /* Used during login & to track session */
    char password[50];   /* Used during login */
    char payload[4096];  /* Holds raw .cpp source code for submissions */
} ClientRequest;

typedef struct {
    int  status;         /* On login success: role value; else STATUS_OK/FAIL */
    char message[256];
} ServerResponse;

/* ─── Function Prototypes (database.c) ─── */

/*
 * init_database()  - Creates data/ directory, seeds users.dat
 *                    with default Admin & Contestant, and seeds
 *                    leaderboard.dat with initial zero-score records.
 */
void init_database(void);

/*
 * update_leaderboard() - Increments solved_count for the given user.
 *                        Uses fcntl() F_WRLCK for exclusive write lock.
 *   Returns: 1 on success, 0 on failure.
 */
int update_leaderboard(int user_id);

/*
 * get_leaderboard() - Reads all ScoreRecords and formats them
 *                     into a human-readable string in buffer.
 *   Returns: number of records read, or -1 on error.
 */
int get_leaderboard(char *buffer, int buf_size);

#endif /* DATABASE_H */
