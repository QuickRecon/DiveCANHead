/*
 * Host-side adapter for the integration test shim.
 *
 * Compiled as part of the native_simulator runner (NOT as Zephyr app
 * code) so it can use raw Linux/glibc headers without colliding with
 * Zephyr's POSIX shims.  Provides POSIX shared memory wrappers.
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
#include <sys/mman.h>
#else
#error "shim adapter requires Linux host"
#endif

void *shim_host_shm_create(const char *name, unsigned long size)
{
    int fd = shm_open(name, O_CREAT | O_RDWR, 0600);
    if (fd < 0) {
        return NULL;
    }
    if (ftruncate(fd, (off_t)size) < 0) {
        close(fd);
        shm_unlink(name);
        return NULL;
    }
    void *ptr = mmap(NULL, size, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    close(fd);
    if (ptr == MAP_FAILED) {
        shm_unlink(name);
        return NULL;
    }
    memset(ptr, 0, size);
    return ptr;
}

void shim_host_shm_unlink(const char *name)
{
    shm_unlink(name);
}
