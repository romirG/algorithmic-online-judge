/*
 * sandbox.c  —  Isolated code compilation and execution engine.
 *
 * OS concepts used:
 *   - fork()      : creates a child process so the server is never at risk
 *   - execlp()    : replaces the child image with gcc/g++ (compile) or the binary (run)
 *   - pipe()×2    : one pipe feeds stdin to the child; another captures stdout
 *   - dup2()      : redirects the child's standard I/O descriptors to the pipes
 *   - setrlimit() : enforces a 2-second CPU time limit (RLIMIT_CPU → SIGXCPU on exceed)
 *   - waitpid()   : parent reaps the child and inspects the exit/signal status
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
#include "database.h"

/* ── Helper: read an entire file into a buffer ───────────────────────────── */
static ssize_t read_file(const char *path, char *buf, size_t buf_size)
{
    FILE *fp = fopen(path, "r");
    if (!fp) return -1;
    size_t n = fread(buf, 1, buf_size - 1, fp);
    buf[n] = '\0';
    fclose(fp);
    return (ssize_t)n;
}

/* ── evaluate_submission ─────────────────────────────────────────────────── */
int evaluate_submission(const char *source_code, int problem_id,
                        const char *file_ext)
{
    /* Choose compiler based on file extension */
    int is_c = (file_ext && strcmp(file_ext, ".c") == 0);
    const char *compiler = is_c ? "gcc" : "g++";

    /* Use the thread ID to name temp files uniquely, so concurrent submissions
     * from different users do not overwrite each other's files. */
    char temp_source[64], temp_binary[64];
    long tid = (long)pthread_self();
    snprintf(temp_source, sizeof(temp_source), "temp_%ld%s", tid, is_c ? ".c" : ".cpp");
    snprintf(temp_binary, sizeof(temp_binary), "a_%ld.out", tid);

    /* Step 1: Write source code to a temporary file */
    FILE *fp = fopen(temp_source, "w");
    if (!fp) {
        perror("[Sandbox] fopen temp source");
        return VERDICT_CE;
    }
    fprintf(fp, "%s", source_code);
    fclose(fp);
    printf("[Sandbox] Source written to %s (%zu bytes, compiler=%s)\n",
           temp_source, strlen(source_code), compiler);

    /* Step 2: Compile — fork() a child and exec() the compiler.
     * The parent blocks on waitpid() and checks the exit code. */
    pid_t compile_pid = fork();
    if (compile_pid < 0) {
        perror("[Sandbox] fork (compile)");
        return VERDICT_CE;
    }

    if (compile_pid == 0) {
        /* Child: suppress compiler error output, then exec the compiler */
        int devnull = open("/dev/null", O_WRONLY);
        if (devnull >= 0) { dup2(devnull, STDERR_FILENO); close(devnull); }
        execlp(compiler, compiler, temp_source, "-o", temp_binary, (char *)NULL);
        perror("[Sandbox] execlp compiler");
        _exit(1);  /* _exit() avoids flushing stdio in the child */
    }

    int comp_status;
    waitpid(compile_pid, &comp_status, 0);  /* reap child; prevents zombie */

    if (!WIFEXITED(comp_status) || WEXITSTATUS(comp_status) != 0) {
        printf("[Sandbox] Compilation Error\n");
        unlink(temp_source);
        return VERDICT_CE;
    }
    printf("[Sandbox] Compilation successful\n");

    /* Step 3: Load test case files for this problem */
    char input_path[256], expected_path[256];
    snprintf(input_path,    sizeof(input_path),    "%s/%d/input.txt",    TESTCASES_DIR, problem_id);
    snprintf(expected_path, sizeof(expected_path), "%s/%d/expected.txt", TESTCASES_DIR, problem_id);

    char input_data[4096]    = "";
    char expected_data[4096] = "";
    ssize_t input_len    = read_file(input_path,    input_data,    sizeof(input_data));
    ssize_t expected_len = read_file(expected_path, expected_data, sizeof(expected_data));

    if (expected_len < 0) {
        printf("[Sandbox] Cannot read expected.txt for Problem %d\n", problem_id);
        return VERDICT_WA;
    }
    printf("[Sandbox] Test case loaded: input=%zd bytes, expected=%zd bytes\n",
           input_len < 0 ? 0 : input_len, expected_len);

    /* Step 4: Execute with two pipes.
     * stdin_pipe  → parent writes test input  → child reads via stdin
     * stdout_pipe → child writes its output   → parent reads and compares */
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
        /* Child: wire up pipes and apply resource limits before exec */

        /* setrlimit enforces the CPU time limit; exceeding it sends SIGXCPU */
        struct rlimit cpu_limit = { .rlim_cur = 2, .rlim_max = 2 };
        if (setrlimit(RLIMIT_CPU, &cpu_limit) < 0) {
            perror("[Sandbox] setrlimit");
            _exit(1);
        }

        /* dup2: redirect stdin ← read end of stdin_pipe */
        close(stdin_pipe[1]);
        dup2(stdin_pipe[0], STDIN_FILENO);
        close(stdin_pipe[0]);

        /* dup2: redirect stdout → write end of stdout_pipe */
        close(stdout_pipe[0]);
        dup2(stdout_pipe[1], STDOUT_FILENO);
        close(stdout_pipe[1]);

        /* Suppress any runtime error output */
        int devnull = open("/dev/null", O_WRONLY);
        if (devnull >= 0) { dup2(devnull, STDERR_FILENO); close(devnull); }

        char exec_path[128];
        snprintf(exec_path, sizeof(exec_path), "./%s", temp_binary);
        execlp(exec_path, exec_path, (char *)NULL);
        perror("[Sandbox] execlp binary");
        _exit(1);
    }

    /* Parent: close the ends it does not use */
    close(stdin_pipe[0]);
    close(stdout_pipe[1]);

    /* Feed test input to child's stdin; closing the pipe sends EOF */
    if (input_len > 0)
        write(stdin_pipe[1], input_data, (size_t)input_len);
    close(stdin_pipe[1]);

    /* Read all output from child's stdout */
    char output[4096];
    memset(output, 0, sizeof(output));
    ssize_t total = 0, n;
    while ((n = read(stdout_pipe[0], output + total,
                     sizeof(output) - 1 - total)) > 0) {
        total += n;
    }
    output[total] = '\0';
    close(stdout_pipe[0]);

    /* Reap the execution child */
    int exec_status;
    waitpid(exec_pid, &exec_status, 0);

    /* WIFSIGNALED catches SIGXCPU (CPU limit exceeded → TLE) */
    if (WIFSIGNALED(exec_status)) {
        printf("[Sandbox] Killed by signal %d → TLE\n", WTERMSIG(exec_status));
        unlink(temp_source);
        unlink(temp_binary);
        return VERDICT_TLE;
    }

    /* Step 5: String comparison of actual vs expected output */
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
