/*
 * auth.h  —  Interface for role-based authentication and user registration.
 * Both functions use fcntl() advisory locks on users.dat to ensure
 * concurrent reads/writes are safely serialized.
 */

#ifndef AUTH_H
#define AUTH_H

#include "database.h"

/*
 * Verifies (id, password) against users.dat under an F_RDLCK advisory read lock.
 * Multiple threads may authenticate concurrently because read locks are shared.
 * Sets *role on success. Returns 1 on success, 0 on failure.
 */
int authenticate_user(int id, const char *password, int *role);

/*
 * Appends a new User record to users.dat under an F_WRLCK exclusive write lock.
 * Also uses a pthread_mutex to serialize concurrent threads within the same process.
 * Returns 1 on success, 0 if the user ID already exists (duplicate rejected).
 */
int register_user(int id, const char *password, int role);

#endif /* AUTH_H */
