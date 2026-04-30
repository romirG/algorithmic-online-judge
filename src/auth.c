/*
 * auth.c  —  Role-based authentication and user registration against users.dat.
 * Verifies credentials under a shared read lock (login) and appends new records
 * under an exclusive write lock (register) to prevent concurrent duplicate IDs.
 *
 * OS concepts used:
 *   - fcntl() F_RDLCK : shared read lock during login (allows concurrent logins)
 *   - fcntl() F_WRLCK : exclusive write lock during registration (prevents duplicates)
 *   - pthread_mutex_t  : serializes threads within the same process before acquiring the file lock
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include <pthread.h>

#include "auth.h"

/* ── authenticate_user ────────────────────────────────────────────────────── */
int authenticate_user(int id, const char *password, int *role)
{
    int fd = open(USERS_FILE, O_RDONLY);
    if (fd < 0) {
        perror("open users.dat");
        return 0;
    }

    /* F_RDLCK is a shared (read) lock — multiple threads may hold it at once,
     * but it blocks if a writer holds F_WRLCK (e.g. someone is registering). */
    struct flock fl;
    memset(&fl, 0, sizeof(fl));
    fl.l_type   = F_RDLCK;
    fl.l_whence = SEEK_SET;
    fl.l_start  = 0;
    fl.l_len    = 0;  /* lock the whole file */

    printf("[AUTH] Acquiring READ lock on users.dat ...\n");
    if (fcntl(fd, F_SETLKW, &fl) < 0) {  /* F_SETLKW: block until lock is granted */
        perror("fcntl F_RDLCK");
        close(fd);
        return 0;
    }
    printf("[AUTH] READ lock acquired\n");

    /* Sequential scan of the binary file for a matching record */
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

    if (!authenticated)
        printf("[AUTH] Authentication failed for user %d\n", id);

    /* Release the lock before closing */
    fl.l_type = F_UNLCK;
    fcntl(fd, F_SETLK, &fl);
    printf("[AUTH] READ lock released\n");

    close(fd);
    return authenticated;
}

/* ── Mutex guards intra-process concurrency; fcntl guards inter-process ──── */
static pthread_mutex_t auth_mutex = PTHREAD_MUTEX_INITIALIZER;

/* ── register_user ────────────────────────────────────────────────────────── */
int register_user(int id, const char *password, int role)
{
    /* Acquire mutex first so two threads in the same server process
     * cannot both pass the duplicate-check before either writes. */
    pthread_mutex_lock(&auth_mutex);

    int fd = open(USERS_FILE, O_RDWR | O_CREAT, 0644);
    if (fd < 0) {
        perror("open users.dat");
        pthread_mutex_unlock(&auth_mutex);
        return 0;
    }

    /* F_WRLCK is exclusive — no other reader or writer may proceed while
     * we scan for duplicates and (conditionally) append the new record. */
    struct flock fl;
    memset(&fl, 0, sizeof(fl));
    fl.l_type   = F_WRLCK;
    fl.l_whence = SEEK_SET;
    fl.l_start  = 0;
    fl.l_len    = 0;

    printf("[AUTH] Acquiring WRITE lock on users.dat for registration ...\n");
    if (fcntl(fd, F_SETLKW, &fl) < 0) {
        perror("fcntl F_WRLCK");
        close(fd);
        pthread_mutex_unlock(&auth_mutex);
        return 0;
    }
    printf("[AUTH] WRITE lock acquired\n");

    /* Scan the entire file while holding the lock to detect duplicates */
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
        /* Seek to end and append the new user record atomically under the lock */
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

    fl.l_type = F_UNLCK;
    fcntl(fd, F_SETLK, &fl);
    printf("[AUTH] WRITE lock released\n");

    close(fd);
    pthread_mutex_unlock(&auth_mutex);
    return success;
}
