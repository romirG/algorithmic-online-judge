/*
 * sandbox.h - Execution Sandbox Interface (v2)
 */

#ifndef SANDBOX_H
#define SANDBOX_H

/*
 * evaluate_submission()
 *
 * Compiles and runs source_code for the given problem_id.
 * Reads test input from  data/testcases/<problem_id>/input.txt
 * Reads expected output from data/testcases/<problem_id>/expected.txt
 *
 * Uses: fork(), execlp(), pipe() x2, dup2(), setrlimit(), waitpid()
 *
 * Returns: VERDICT_AC (1), VERDICT_WA (0), VERDICT_CE (-1), VERDICT_TLE (-2)
 */
int evaluate_submission(const char *source_code, int problem_id);

#endif /* SANDBOX_H */
