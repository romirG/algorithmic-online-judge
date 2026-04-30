/*
 * database.h  —  Shared data structures, constants, and function prototypes.
 * Included by all modules so the entire system speaks the same "language".
 */

#ifndef DATABASE_H
#define DATABASE_H

/* ─── Network ─── */
#define PORT        8080
#define SERVER_IP   "127.0.0.1"

/* ─── Binary data-file paths ─── */
#define DATA_DIR          "data"
#define USERS_FILE        "data/users.dat"
#define LEADERBOARD_FILE  "data/leaderboard.dat"
#define PROBLEMS_FILE     "data/problems.dat"
#define TESTCASES_DIR     "data/testcases"
#define SOLVED_FILE       "data/solved.dat"

/* ─── Action codes sent inside ClientRequest.action ─── */
#define ACTION_LOGIN            1
#define ACTION_SUBMIT           2
#define ACTION_LEADERBOARD      3
#define ACTION_CREATE_PROBLEM   4
#define ACTION_VIEW_PROBLEMS    5
#define ACTION_UPLOAD_INPUT     6   /* upload input.txt for a problem    */
#define ACTION_UPLOAD_EXPECTED  7   /* upload expected.txt for a problem */
#define ACTION_VIEW_LOGS        8
#define ACTION_HALT_SYSTEM      9   /* triggers SIGUSR1 on the server    */
#define ACTION_REGISTER         10  /* create a new contestant account   */
#define ACTION_SPECTATOR        11  /* enter read-only guest session     */

/* ─── Role codes stored in User.role ─── */
#define ROLE_ADMIN       1
#define ROLE_CONTESTANT  2
#define ROLE_SPECTATOR   3  /* read-only guest; no login required */

/* ─── Status codes returned in ServerResponse.status ─── */
#define STATUS_OK        1
#define STATUS_FAIL     -1
#define VERDICT_AC       1   /* Accepted              */
#define VERDICT_WA       0   /* Wrong Answer           */
#define VERDICT_CE      -1   /* Compilation Error      */
#define VERDICT_TLE     -2   /* Time Limit Exceeded    */

/* ─── On-disk record types (fixed-size for O(1) seeks) ─── */

/* One row in users.dat */
typedef struct {
    int  id;
    char password[50];
    int  role;          /* ROLE_ADMIN / ROLE_CONTESTANT */
} User;

/* One row in leaderboard.dat */
typedef struct {
    int user_id;
    int solved_count;
} ScoreRecord;

/* One row in solved.dat — prevents double-counting the same problem */
typedef struct {
    int user_id;
    int problem_id;
} SolvedRecord;

/* One row in problems.dat */
typedef struct {
    int  id;
    char title[100];
    char description[512];
    int  active;    /* 1 = visible to contestants, 0 = hidden */
} Problem;

/* ─── IPC structs passed over the TCP socket ─── */

/* Client → Server */
typedef struct {
    int  action;
    int  user_id;
    int  problem_id;
    char password[50];
    char file_ext[10];   /* ".c" or ".cpp" — tells sandbox which compiler to use */
    char payload[4096];  /* code, problem text, or test-case content */
} ClientRequest;

/* Server → Client */
typedef struct {
    int  status;
    char message[4096];  /* large enough to hold problem lists and log dumps */
} ServerResponse;

/* ─── Function prototypes ─── */

void init_database(void);
int  update_leaderboard(int user_id, int problem_id);
int  init_user_leaderboard(int user_id);
int  get_leaderboard(char *buffer, int buf_size);

/* Problems — fcntl F_WRLCK on write, F_RDLCK on read */
int  create_problem(int id, const char *title, const char *description);
int  get_problems(char *buffer, int buf_size);

/* Test-case file I/O (no lock needed; files are per-problem and written once) */
int  save_testcase_file(int problem_id, const char *filename,
                        const char *content);

#endif /* DATABASE_H */
