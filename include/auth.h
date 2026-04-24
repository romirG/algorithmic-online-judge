/*
 * auth.h - Authentication Module Interface
 * Algorithmic Online Judge
 *
 * Provides role-based authentication using fcntl() advisory
 * read locks on the users.dat binary file.
 */

#ifndef AUTH_H
#define AUTH_H

#include "database.h"

/*
 * authenticate_user() - Validates credentials against users.dat.
 *
 * Constraint: Applies fcntl() F_RDLCK (advisory read lock) on
 *             users.dat for the duration of the lookup.
 *
 * Parameters:
 *   id       - User ID to authenticate
 *   password - Plaintext password to verify
 *   role     - Output parameter; set to the user's role on success
 *
 * Returns: 1 on successful authentication, 0 on failure.
 */
int authenticate_user(int id, const char *password, int *role);

/*
 * Registers a new user with the given ID and password.
 * Uses an exclusive WRITE lock (F_WRLCK) on users.dat to prevent duplicate IDs.
 * Returns 1 on success, 0 on failure (e.g., ID already exists).
 */
int register_user(int id, const char *password, int role);

#endif /* AUTH_H */
