/*
 * sandbox.h - Execution Sandbox Interface
 * Algorithmic Online Judge
 *
 * Provides sandboxed compilation and execution of C++ submissions
 * using fork(), exec(), pipe(), setrlimit(), and dup2().
 */

#ifndef SANDBOX_H
#define SANDBOX_H

/*
 * evaluate_submission() - Compiles and runs a C++ source string
 *                         inside a sandboxed child process.
 *
 * Pipeline:
 *   1. Writes source_code to a temporary file (temp.cpp).
 *   2. fork()+execlp() to compile with g++.
 *   3. pipe()+fork()+execlp() to run the binary (a.out).
 *   4. setrlimit(RLIMIT_CPU, 2s) in the execution child.
 *   5. dup2() redirects child's stdout into the pipe.
 *   6. Parent reads output and compares to expected answer.
 *
 * Parameters:
 *   source_code - Null-terminated C++ source string.
 *
 * Returns: 1 if output matches expected answer (Accepted),
 *          0 otherwise (Wrong Answer / Compilation Error / TLE).
 */
int evaluate_submission(const char *source_code);

#endif /* SANDBOX_H */
