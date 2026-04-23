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

#endif /* AUTH_H */
