/*
 * sandbox.c - Execution Sandbox with Two-Way IPC (v2)
 *
 * OS Concepts:
 *   - pipe() x2      : stdin pipe (input→child) + stdout pipe (child→parent)
 *   - fork() x2      : compilation child + execution child
 *   - execlp()       : g++ for compile, ./a.out for run
 *   - setrlimit()    : RLIMIT_CPU = 2s
 *   - dup2()         : redirect child stdin/stdout
 *   - waitpid()      : reap children, detect SIGXCPU
 *
 * Now reads test input/expected from data/testcases/<problem_id>/
 * Returns distinct verdict codes: AC, WA, CE, TLE
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <sys/time.h>
#include <sys/resource.h>
#include <pthread.h>

#include "sandbox.h"
#include "database.h"   /* for TESTCASES_DIR, verdict macros */

#define TEMP_BINARY "a.out"

/* ── Helper: read entire file into buffer, return bytes read ── */
static ssize_t read_file(const char *path, char *buf, size_t buf_size)
{
    FILE *fp = fopen(path, "r");
    if (!fp) return -1;
    size_t n = fread(buf, 1, buf_size - 1, fp);
    buf[n] = '\0';
    fclose(fp);
    return (ssize_t)n;
}

/* ───────────────────────────────────────────────────────────
 * evaluate_submission()
 *
 * file_ext: ".c" → gcc compiler, ".cpp" → g++ compiler
 *
 * 1. Write source_code → temp.c or temp.cpp
 * 2. fork+execlp gcc/g++ → compile
 * 3. Read input.txt + expected.txt for the problem
 * 4. pipe x2 + fork: feed input via stdin pipe,
 *    capture stdout via stdout pipe
 * 5. setrlimit RLIMIT_CPU 2s in child
 * 6. Compare captured output with expected
 *
 * Returns: VERDICT_AC, VERDICT_WA, VERDICT_CE, VERDICT_TLE
 * ─────────────────────────────────────────────────────────── */
int evaluate_submission(const char *source_code, int problem_id,
                        const char *file_ext)
{
    /* Determine compiler and temp filename from extension */
    int is_c = (file_ext && strcmp(file_ext, ".c") == 0);
    const char *compiler = is_c ? "gcc" : "g++";

    /* Generate unique temp files per thread to handle concurrent multi-user submissions */
    char temp_source[64];
    char temp_binary[64];
    long tid = (long)pthread_self();
    snprintf(temp_source, sizeof(temp_source), "temp_%ld%s", tid, is_c ? ".c" : ".cpp");
    snprintf(temp_binary, sizeof(temp_binary), "a_%ld.out", tid);

    /* ── Step 1: Write source to temp file ── */
    FILE *fp = fopen(temp_source, "w");
    if (!fp) {
        perror("[Sandbox] fopen temp source");
        return VERDICT_CE;
    }
    fprintf(fp, "%s", source_code);
    fclose(fp);
    printf("[Sandbox] Source written to %s (%zu bytes, compiler=%s)\n",
           temp_source, strlen(source_code), compiler);

    /* ── Step 2: Compile with fork() + execlp(compiler) ── */
    pid_t compile_pid = fork();
    if (compile_pid < 0) {
        perror("[Sandbox] fork (compile)");
        return VERDICT_CE;
    }

    if (compile_pid == 0) {
        /* CHILD: compiler */
        int devnull = open("/dev/null", O_WRONLY);
        if (devnull >= 0) { dup2(devnull, STDERR_FILENO); close(devnull); }
        execlp(compiler, compiler, temp_source, "-o", temp_binary, (char *)NULL);
        perror("[Sandbox] execlp compiler");
        _exit(1);
    }

    int comp_status;
    waitpid(compile_pid, &comp_status, 0);

    if (!WIFEXITED(comp_status) || WEXITSTATUS(comp_status) != 0) {
        printf("[Sandbox] Compilation Error\n");
        unlink(temp_source);
        return VERDICT_CE;
    }
    printf("[Sandbox] Compilation successful\n");

    /* ── Step 3: Load test case files ── */
    char input_path[256], expected_path[256];
    snprintf(input_path, sizeof(input_path),
             "%s/%d/input.txt", TESTCASES_DIR, problem_id);
    snprintf(expected_path, sizeof(expected_path),
             "%s/%d/expected.txt", TESTCASES_DIR, problem_id);

    char input_data[4096]    = "";
    char expected_data[4096] = "";
    ssize_t input_len = read_file(input_path, input_data, sizeof(input_data));
    ssize_t expected_len = read_file(expected_path, expected_data,
                                     sizeof(expected_data));

    if (expected_len < 0) {
        printf("[Sandbox] Cannot read expected.txt for Problem %d\n",
               problem_id);
        return VERDICT_WA;
    }

    printf("[Sandbox] Test case loaded: input=%zd bytes, expected=%zd bytes\n",
           input_len < 0 ? 0 : input_len, expected_len);

    /* ── Step 4: Execute with TWO pipes (stdin + stdout) ── */
    int stdin_pipe[2], stdout_pipe[2];

    if (pipe(stdin_pipe) < 0) {
        perror("[Sandbox] pipe (stdin)");
        return VERDICT_WA;
    }
    if (pipe(stdout_pipe) < 0) {
        perror("[Sandbox] pipe (stdout)");
        close(stdin_pipe[0]); close(stdin_pipe[1]);
        return VERDICT_WA;
    }

    pid_t exec_pid = fork();
    if (exec_pid < 0) {
        perror("[Sandbox] fork (execute)");
        close(stdin_pipe[0]); close(stdin_pipe[1]);
        close(stdout_pipe[0]); close(stdout_pipe[1]);
        return VERDICT_WA;
    }

    if (exec_pid == 0) {
        /* ── CHILD: execution process ── */

        /* (a) setrlimit: CPU time = 2 seconds */
        struct rlimit cpu_limit = { .rlim_cur = 2, .rlim_max = 2 };
        if (setrlimit(RLIMIT_CPU, &cpu_limit) < 0) {
            perror("[Sandbox] setrlimit");
            _exit(1);
        }

        /* (b) dup2: wire up stdin from pipe */
        close(stdin_pipe[1]);                       /* close write end */
        dup2(stdin_pipe[0], STDIN_FILENO);          /* stdin ← pipe   */
        close(stdin_pipe[0]);

        /* (c) dup2: wire up stdout to pipe */
        close(stdout_pipe[0]);                      /* close read end  */
        dup2(stdout_pipe[1], STDOUT_FILENO);        /* stdout → pipe   */
        close(stdout_pipe[1]);

        /* Suppress stderr */
        int devnull = open("/dev/null", O_WRONLY);
        if (devnull >= 0) { dup2(devnull, STDERR_FILENO); close(devnull); }

        /* (d) execlp: run the binary */
        
        char exec_path[128];
        snprintf(exec_path, sizeof(exec_path), "./%s", temp_binary);
        
        execlp(exec_path, exec_path, (char *)NULL);
        perror("[Sandbox] execlp a.out");
        _exit(1);
    }

    /* ── PARENT ── */
    close(stdin_pipe[0]);    /* close read end of stdin pipe  */
    close(stdout_pipe[1]);   /* close write end of stdout pipe */

    /* Feed input to child's stdin */
    if (input_len > 0) {
        write(stdin_pipe[1], input_data, (size_t)input_len);
    }
    close(stdin_pipe[1]);    /* Send EOF to child */

    /* Read child's stdout */
    char output[4096];
    memset(output, 0, sizeof(output));
    ssize_t total = 0, n;
    while ((n = read(stdout_pipe[0], output + total,
                     sizeof(output) - 1 - total)) > 0) {
        total += n;
    }
    output[total] = '\0';
    close(stdout_pipe[0]);

    /* Wait for child */
    int exec_status;
    waitpid(exec_pid, &exec_status, 0);

    /* Check for SIGXCPU (TLE) */
    if (WIFSIGNALED(exec_status)) {
        printf("[Sandbox] Killed by signal %d → TLE\n",
               WTERMSIG(exec_status));
        unlink(temp_source);
        unlink(temp_binary);
        return VERDICT_TLE;
    }

    /* ── Step 5: Compare output ── */
    printf("[Sandbox] Output:   \"%s\"\n", output);
    printf("[Sandbox] Expected: \"%s\"\n", expected_data);

    if (strcmp(output, expected_data) == 0) {
        printf("[Sandbox] ✓ ACCEPTED\n");
        unlink(temp_source);
        unlink(temp_binary);
        return VERDICT_AC;
    } else {
        printf("[Sandbox] ✗ WRONG ANSWER\n");
        unlink(temp_source);
        unlink(temp_binary);
        return VERDICT_WA;
    }
}
