/*
 * database.h - Core Data Structures & Macros
 * Algorithmic Online Judge (v2 — Extended Feature Set)
 */

#ifndef DATABASE_H
#define DATABASE_H

/* ─── Network Configuration ─── */
#define PORT            8080
#define SERVER_IP       "127.0.0.1"

/* ─── Data File Paths ─── */
#define DATA_DIR          "data"
#define USERS_FILE        "data/users.dat"
#define LEADERBOARD_FILE  "data/leaderboard.dat"
#define PROBLEMS_FILE     "data/problems.dat"
#define TESTCASES_DIR     "data/testcases"

/* ─── Action Codes (ClientRequest.action) ─── */
#define ACTION_LOGIN            1
#define ACTION_SUBMIT           2
#define ACTION_LEADERBOARD      3
#define ACTION_CREATE_PROBLEM   4
#define ACTION_VIEW_PROBLEMS    5
#define ACTION_UPLOAD_INPUT     6   /* Upload input.txt for a problem  */
#define ACTION_UPLOAD_EXPECTED  7   /* Upload expected.txt             */
#define ACTION_VIEW_LOGS        8
#define ACTION_HALT_SYSTEM      9   /* IPC Signal trigger (SIGUSR1)    */

/* ─── Role Codes ─── */
#define ROLE_ADMIN       1
#define ROLE_CONTESTANT  2

/* ─── Status / Verdict Codes ─── */
#define STATUS_OK        1
#define STATUS_FAIL     -1
#define VERDICT_AC       1   /* Accepted              */
#define VERDICT_WA       0   /* Wrong Answer           */
#define VERDICT_CE      -1   /* Compilation Error       */
#define VERDICT_TLE     -2   /* Time Limit Exceeded     */

/* ─── Data Structures ─── */

typedef struct {
    int  id;
    char password[50];
    int  role;
} User;

typedef struct {
    int user_id;
    int solved_count;
} ScoreRecord;

typedef struct {
    int  id;
    char title[100];
    char description[512];
    int  active;             /* 1 = active, 0 = disabled */
} Problem;

typedef struct {
    int  action;
    int  user_id;
    int  problem_id;         /* Which problem to submit to / manage */
    char password[50];
    char payload[4096];      /* Code, problem desc, test-case content */
} ClientRequest;

typedef struct {
    int  status;
    char message[4096];      /* Large enough for problem lists & logs */
} ServerResponse;

/* ─── Function Prototypes ─── */

void init_database(void);
int  update_leaderboard(int user_id);
int  get_leaderboard(char *buffer, int buf_size);

/* Problem management (fcntl write/read locks on problems.dat) */
int  create_problem(int id, const char *title, const char *description);
int  get_problems(char *buffer, int buf_size);

/* Test-case file management */
int  save_testcase_file(int problem_id, const char *filename,
                        const char *content);

#endif /* DATABASE_H */
