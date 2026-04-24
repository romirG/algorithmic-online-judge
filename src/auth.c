/*
 * auth.c - Role-Based Authentication with File Locking
 * Algorithmic Online Judge
 *
 * OS Concepts Demonstrated:
 *   - fcntl() advisory READ locks (F_RDLCK) on users.dat
 *   - Binary file I/O for sequential record scanning
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include <pthread.h>

#include "auth.h"

/* ───────────────────────────────────────────────────────────
 * authenticate_user()
 *
 * Opens users.dat, applies an advisory READ lock (F_RDLCK)
 * via fcntl(), scans for a matching (id, password) pair,
 * and writes the user's role into *role on success.
 *
 * CONSTRAINT: fcntl() F_RDLCK is held for the entire read
 *             to prevent writers from modifying the file
 *             while authentication is in progress.
 *
 * Returns: 1 on success, 0 on failure.
 * ─────────────────────────────────────────────────────────── */
int authenticate_user(int id, const char *password, int *role)
{
    int fd = open(USERS_FILE, O_RDONLY);
    if (fd < 0) {
        perror("open users.dat");
        return 0;
    }

    /* Apply advisory read lock on the entire file */
    struct flock fl;
    memset(&fl, 0, sizeof(fl));
    fl.l_type   = F_RDLCK;      /* Shared read lock */
    fl.l_whence = SEEK_SET;
    fl.l_start  = 0;
    fl.l_len    = 0;             /* Lock entire file */

    printf("[AUTH] Acquiring READ lock on users.dat ...\n");
    if (fcntl(fd, F_SETLKW, &fl) < 0) {   /* Blocking lock */
        perror("fcntl F_RDLCK");
        close(fd);
        return 0;
    }
    printf("[AUTH] READ lock acquired\n");

    /* Scan records for matching credentials */
    User user;
    int authenticated = 0;

    while (read(fd, &user, sizeof(User)) == sizeof(User)) {
        if (user.id == id && strcmp(user.password, password) == 0) {
            *role = user.role;
            authenticated = 1;
            printf("[AUTH] User %d authenticated (role=%d)\n", id, *role);
            break;
        }
    }

    if (!authenticated) {
        printf("[AUTH] Authentication failed for user %d\n", id);
    }

    /* Release the read lock */
    fl.l_type = F_UNLCK;
    fcntl(fd, F_SETLK, &fl);
    printf("[AUTH] READ lock released\n");

    close(fd);
    return authenticated;
}

static pthread_mutex_t auth_mutex = PTHREAD_MUTEX_INITIALIZER;

/* ───────────────────────────────────────────────────────────
 * register_user()
 *
 * Opens users.dat, applies an advisory WRITE lock (F_WRLCK)
 * via fcntl(), scans to ensure the ID doesn't already exist,
 * and if not, appends the new user record.
 *
 * CONSTRAINT: fcntl() F_WRLCK is held for the entire read and
 *             write to prevent concurrent duplicate registrations.
 *
 * Returns: 1 on success, 0 on failure (duplicate ID).
 * ─────────────────────────────────────────────────────────── */
int register_user(int id, const char *password, int role)
{
    pthread_mutex_lock(&auth_mutex);

    int fd = open(USERS_FILE, O_RDWR | O_CREAT, 0644);
    if (fd < 0) {
        perror("open users.dat");
        pthread_mutex_unlock(&auth_mutex);
        return 0;
    }

    struct flock fl;
    memset(&fl, 0, sizeof(fl));
    fl.l_type   = F_WRLCK;      /* Exclusive write lock */
    fl.l_whence = SEEK_SET;
    fl.l_start  = 0;
    fl.l_len    = 0;             /* Lock entire file */

    printf("[AUTH] Acquiring WRITE lock on users.dat for registration ...\n");
    if (fcntl(fd, F_SETLKW, &fl) < 0) {
        perror("fcntl F_WRLCK");
        close(fd);
        pthread_mutex_unlock(&auth_mutex);
        return 0;
    }
    printf("[AUTH] WRITE lock acquired\n");

    /* Scan to ensure ID doesn't exist */
    User user;
    int duplicate = 0;
    while (read(fd, &user, sizeof(User)) == sizeof(User)) {
        if (user.id == id) {
            duplicate = 1;
            break;
        }
    }

    int success = 0;
    if (duplicate) {
        printf("[AUTH] Registration failed: User %d already exists.\n", id);
    } else {
        /* Append new user */
        User new_user;
        memset(&new_user, 0, sizeof(User));
        new_user.id = id;
        strncpy(new_user.password, password, sizeof(new_user.password) - 1);
        new_user.role = role;

        lseek(fd, 0, SEEK_END);
        write(fd, &new_user, sizeof(User));
        printf("[AUTH] User %d registered successfully (role=%d).\n", id, role);
        success = 1;
    }

    /* Release lock */
    fl.l_type = F_UNLCK;
    fcntl(fd, F_SETLK, &fl);
    printf("[AUTH] WRITE lock released\n");

    close(fd);
    pthread_mutex_unlock(&auth_mutex);
    return success;
}
