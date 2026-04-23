/*
 * sandbox.c - Execution Sandbox with IPC & Process Management
 * Algorithmic Online Judge
 *
 * OS Concepts Demonstrated:
 *   - pipe()       : Unnamed pipe for IPC between parent and child
 *   - fork()       : Process creation for compilation and execution
 *   - execlp()     : Program overlay (g++ for compile, ./a.out for run)
 *   - setrlimit()  : RLIMIT_CPU capped at 2 seconds (prevents TLE)
 *   - dup2()       : Redirect child's stdout into the pipe write-end
 *   - waitpid()    : Parent waits and inspects child's exit status
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <sys/resource.h>

#include "sandbox.h"

/* Hardcoded expected output for the single demo problem */
#define EXPECTED_OUTPUT "Hello World\n"

/* Temporary file names (written under mutex protection) */
#define TEMP_SOURCE     "temp.cpp"
#define TEMP_BINARY     "a.out"

/* ───────────────────────────────────────────────────────────
 * evaluate_submission()
 *
 * Full pipeline:
 *   1. Write source_code to temp.cpp
 *   2. fork() + execlp("g++") to compile
 *   3. pipe() + fork() + execlp("./a.out") to execute
 *   4. setrlimit(RLIMIT_CPU, 2s) in execution child
 *   5. dup2() redirects child stdout into pipe
 *   6. Parent reads pipe output, compares to expected answer
 *
 * NOTE: This function must be called while the caller holds
 *       the evaluation_mutex (defined in server.c).
 *
 * Returns: 1 = Accepted, 0 = Wrong Answer / CE / TLE / RE
 * ─────────────────────────────────────────────────────────── */
int evaluate_submission(const char *source_code)
{
    /* ── Step 1: Write source code to temporary file ── */
    FILE *fp = fopen(TEMP_SOURCE, "w");
    if (!fp) {
        perror("[Sandbox] fopen temp.cpp");
        return 0;
    }
    fprintf(fp, "%s", source_code);
    fclose(fp);
    printf("[Sandbox] Source written to %s (%zu bytes)\n",
           TEMP_SOURCE, strlen(source_code));

    /* ── Step 2: Compile with fork() + execlp("g++") ── */
    pid_t compile_pid = fork();
    if (compile_pid < 0) {
        perror("[Sandbox] fork (compile)");
        return 0;
    }

    if (compile_pid == 0) {
        /* ── CHILD: Compiler process ── */

        /* Suppress compiler warnings/errors from server terminal */
        int devnull = open("/dev/null", O_WRONLY);
        if (devnull >= 0) {
            dup2(devnull, STDERR_FILENO);
            close(devnull);
        }

        execlp("g++", "g++", TEMP_SOURCE, "-o", TEMP_BINARY, (char *)NULL);

        /* If execlp returns, it failed */
        perror("[Sandbox] execlp g++");
        _exit(1);
    }

    /* ── PARENT: Wait for compilation ── */
    int comp_status;
    waitpid(compile_pid, &comp_status, 0);

    if (!WIFEXITED(comp_status) || WEXITSTATUS(comp_status) != 0) {
        printf("[Sandbox] Compilation Error\n");
        return 0;   /* CE */
    }
    printf("[Sandbox] Compilation successful\n");

    /* ── Step 3: Execute with pipe() + fork() ── */
    int pipefd[2];
    if (pipe(pipefd) < 0) {
        perror("[Sandbox] pipe");
        return 0;
    }

    pid_t exec_pid = fork();
    if (exec_pid < 0) {
        perror("[Sandbox] fork (execute)");
        close(pipefd[0]);
        close(pipefd[1]);
        return 0;
    }

    if (exec_pid == 0) {
        /* ── CHILD: Execution process ── */

        /* (a) setrlimit: restrict CPU time to 2 seconds */
        struct rlimit cpu_limit;
        cpu_limit.rlim_cur = 2;   /* soft limit: 2 seconds */
        cpu_limit.rlim_max = 2;   /* hard limit: 2 seconds */
        if (setrlimit(RLIMIT_CPU, &cpu_limit) < 0) {
            perror("[Sandbox] setrlimit RLIMIT_CPU");
            _exit(1);
        }

        /* (b) dup2: redirect stdout into pipe write-end */
        close(pipefd[0]);                       /* close read-end  */
        dup2(pipefd[1], STDOUT_FILENO);         /* stdout -> pipe  */
        close(pipefd[1]);                       /* close original  */

        /* Suppress stderr */
        int devnull = open("/dev/null", O_WRONLY);
        if (devnull >= 0) {
            dup2(devnull, STDERR_FILENO);
            close(devnull);
        }

        /* (c) execlp: run the compiled binary */
        execlp("./a.out", "./a.out", (char *)NULL);

        /* If execlp returns, it failed */
        perror("[Sandbox] execlp a.out");
        _exit(1);
    }

    /* ── PARENT: Read child output from pipe ── */
    close(pipefd[1]);   /* Close write-end in parent */

    char output[4096];
    memset(output, 0, sizeof(output));

    ssize_t total = 0;
    ssize_t n;
    while ((n = read(pipefd[0], output + total,
                     sizeof(output) - 1 - total)) > 0) {
        total += n;
    }
    output[total] = '\0';
    close(pipefd[0]);

    /* Wait for the execution child */
    int exec_status;
    waitpid(exec_pid, &exec_status, 0);

    /* Check if child was killed by signal (e.g., SIGXCPU for TLE) */
    if (WIFSIGNALED(exec_status)) {
        printf("[Sandbox] Process killed by signal %d (likely TLE)\n",
               WTERMSIG(exec_status));
        return 0;   /* TLE / Runtime Error */
    }

    /* ── Step 4: Compare output with expected answer ── */
    printf("[Sandbox] Program output: \"%s\"\n", output);
    printf("[Sandbox] Expected:       \"%s\"\n", EXPECTED_OUTPUT);

    if (strcmp(output, EXPECTED_OUTPUT) == 0) {
        printf("[Sandbox] ✓ ACCEPTED\n");
        return 1;
    } else {
        printf("[Sandbox] ✗ WRONG ANSWER\n");
        return 0;
    }
}
