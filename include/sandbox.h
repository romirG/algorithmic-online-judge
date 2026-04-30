/*
 * sandbox.h  —  Interface for the isolated code evaluation engine.
 */

#ifndef SANDBOX_H
#define SANDBOX_H

/*
 * Compiles and runs source_code against the stored test case for problem_id.
 * Uses fork()+execlp() to compile (gcc/g++ chosen by file_ext), then a second
 * fork() with pipe()+dup2()+setrlimit(RLIMIT_CPU=2s) to execute and capture output.
 * Returns VERDICT_AC, VERDICT_WA, VERDICT_CE, or VERDICT_TLE.
 */
int evaluate_submission(const char *source_code, int problem_id,
                        const char *file_ext);

#endif /* SANDBOX_H */
