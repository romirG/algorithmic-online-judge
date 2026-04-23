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
