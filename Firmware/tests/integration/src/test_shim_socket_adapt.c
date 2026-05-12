/*
 * Host-side socket adapter for the integration test shim.
 *
 * Compiled as part of the native_simulator runner (NOT as Zephyr app
 * code) so it can use raw Linux/glibc headers without colliding with
 * the Zephyr POSIX networking shims.  Provides thin wrappers over
 * socket/bind/listen/accept/read/write/close which the Zephyr-side
 * shim thread calls.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <fcntl.h>

#ifdef __linux
#include <sys/socket.h>
#include <sys/un.h>
#else
#error "shim socket adapter requires Linux host"
#endif

/* All identifiers here have the shim_host_* prefix to avoid colliding
 * with Zephyr's POSIX socket symbol names. */

int shim_host_listen_unix(const char *path)
{
    int fd = socket(AF_UNIX, SOCK_STREAM, 0);
    if (fd < 0) {
        return -errno;
    }

    /* Remove any stale socket node so bind() succeeds */
    (void)unlink(path);

    struct sockaddr_un addr;
    memset(&addr, 0, sizeof(addr));
    addr.sun_family = AF_UNIX;
    strncpy(addr.sun_path, path, sizeof(addr.sun_path) - 1);

    if (bind(fd, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        int err = -errno;
        close(fd);
        return err;
    }
    if (listen(fd, 1) < 0) {
        int err = -errno;
        close(fd);
        return err;
    }

    /* Make listening fd non-blocking so accept() returns -EAGAIN when
     * no client is waiting. The Zephyr-side thread polls with sleeps
     * between attempts. */
    int flags = fcntl(fd, F_GETFL, 0);
    (void)fcntl(fd, F_SETFL, flags | O_NONBLOCK);

    return fd;
}

int shim_host_accept(int listen_fd)
{
    int fd = accept(listen_fd, NULL, NULL);
    if (fd < 0) {
        return -errno;
    }
    /* Force non-blocking on the client fd.  Native_sim runs all
     * Zephyr threads on a single underlying scheduler context, so a
     * blocking host syscall (read()) would stall the simulator.  The
     * caller polls with k_msleep between attempts. */
    int flags = fcntl(fd, F_GETFL, 0);
    (void)fcntl(fd, F_SETFL, flags | O_NONBLOCK);
    return fd;
}

long shim_host_read(int fd, void *buf, unsigned long size)
{
    ssize_t n = read(fd, buf, size);
    if (n < 0) {
        return -errno;
    }
    return (long)n;
}

long shim_host_write(int fd, const void *buf, unsigned long size)
{
    ssize_t n = write(fd, buf, size);
    if (n < 0) {
        return -errno;
    }
    return (long)n;
}

int shim_host_close(int fd)
{
    return close(fd);
}
