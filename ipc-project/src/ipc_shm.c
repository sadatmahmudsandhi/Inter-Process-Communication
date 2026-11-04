/*
 * ipc_shm.c - IPC method 3: POSIX shared memory protected by semaphores
 *
 * Both processes map the SAME block of memory, so no data is copied through the
 * kernel - but nothing stops them from touching it at the same moment, so we
 * must synchronize ourselves. Three (unnamed, process-shared) semaphores live
 * inside the shared block:
 *
 *   mutex : binary semaphore (starts at 1). Only the process holding it may touch
 *           the data - this is the semaphore that "protects the shared memory".
 *   empty : starts at 1. The sender waits on it before writing; the slot is free.
 *   full  : starts at 0. The receiver waits on it before reading; the slot holds
 *           new data.
 *
 * sender  : wait(empty) -> wait(mutex) -> write -> post(mutex) -> post(full)
 * receiver: wait(full)  -> wait(mutex) -> read  -> post(mutex) -> post(empty)
 */
#include "common.h"

#include <errno.h>
#include <fcntl.h>
#include <semaphore.h>
#include <signal.h>
#include <stdio.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

#define SEM_TIMEOUT_SEC 5 /* never hang forever if the other process dies */

typedef struct {
    sem_t mutex;
    sem_t empty;
    sem_t full;
    Dataset data;
} Shared;

/* sem_wait with a timeout. Returns 0 on success, -1 on timeout / Ctrl+C / error. */
static int sem_wait_limit(sem_t *s)
{
    struct timespec ts;
    clock_gettime(CLOCK_REALTIME, &ts);
    ts.tv_sec += SEM_TIMEOUT_SEC;

    while (sem_timedwait(s, &ts) == -1) {
        if (errno == EINTR && !was_interrupted())
            continue;
        return -1;
    }
    return 0;
}

int ipc_shm_run(const Dataset *data, int rounds, int verbose, double *elapsed_sec)
{
    char name[64];
    snprintf(name, sizeof name, "/ipc_demo_shm_%d", (int)getpid());

    /* 1. create the channel: shared-memory object, size it, map it, init semaphores */
    shm_unlink(name); /* remove a stale object with the same name, if any */
    int fd = shm_open(name, O_CREAT | O_EXCL | O_RDWR, 0600);
    if (fd == -1) {
        perror("shm_open");
        return -1;
    }
    if (ftruncate(fd, (off_t)sizeof(Shared)) == -1) {
        perror("ftruncate");
        close(fd);
        shm_unlink(name);
        return -1;
    }
    Shared *sh = mmap(NULL, sizeof *sh, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    close(fd); /* the mapping stays valid after closing the descriptor */
    if (sh == MAP_FAILED) {
        perror("mmap");
        shm_unlink(name);
        return -1;
    }
    /* second argument 1 = shared between processes */
    if (sem_init(&sh->mutex, 1, 1) == -1 || sem_init(&sh->empty, 1, 1) == -1 ||
        sem_init(&sh->full, 1, 0) == -1) {
        perror("sem_init");
        munmap(sh, sizeof *sh);
        shm_unlink(name);
        return -1;
    }

    fflush(stdout);
    double start = now_seconds();
    pid_t pid = fork();
    if (pid < 0) {
        perror("fork");
        sem_destroy(&sh->mutex);
        sem_destroy(&sh->empty);
        sem_destroy(&sh->full);
        munmap(sh, sizeof *sh);
        shm_unlink(name);
        return -1;
    }

    if (pid == 0) {
        /* ---------- receiver (child) ---------- */
        /* `data` is only the reference copy used for the final comparison */
        Dataset got;
        int ok = 1;
        for (int r = 0; r < rounds; r++) {
            if (sem_wait_limit(&sh->full) != 0 || sem_wait_limit(&sh->mutex) != 0) {
                ok = 0;
                break;
            }
            int n = sh->data.count; /* copy out while holding the mutex... */
            if (n < 1 || n > MAX_NUMS)
                n = 0;
            got.count = n;
            memcpy(got.nums, sh->data.nums, (size_t)n * sizeof(int));
            sem_post(&sh->mutex);   /* ...then release it before doing anything slow */
            sem_post(&sh->empty);   /* slot is free again */
            if (!receiver_check(data, &got, verbose && r == 0))
                ok = 0;
        }
        munmap(sh, sizeof *sh);
        _exit(ok ? 0 : 2);
    }

    /* ---------- sender (parent) ---------- */
    if (verbose) {
        printf("  setup  : shm_open(\"%s\") + ftruncate(%zu bytes) + mmap(); sem_init x3 "
               "(mutex=1, empty=1, full=0);\n"
               "           fork() created receiver pid %d\n",
               name, sizeof(Shared), (int)pid);
        sender_announce(data);
    }

    /* 2. use the channel */
    int ok = 1;
    for (int r = 0; r < rounds; r++) {
        if (sem_wait_limit(&sh->empty) != 0 || sem_wait_limit(&sh->mutex) != 0) {
            ok = 0;
            break;
        }
        sh->data.count = data->count;
        memcpy(sh->data.nums, data->nums, (size_t)data->count * sizeof(int));
        sem_post(&sh->mutex);
        sem_post(&sh->full); /* tell the receiver there is new data */
    }
    if (!ok)
        kill(pid, SIGTERM);

    /* 3. close / remove the channel */
    int got = wait_for_receiver(pid);
    *elapsed_sec = now_seconds() - start;

    sem_destroy(&sh->mutex);
    sem_destroy(&sh->empty);
    sem_destroy(&sh->full);
    munmap(sh, sizeof *sh);
    shm_unlink(name);

    if (verbose)
        printf("  cleanup: receiver exited (%s), semaphores destroyed, memory unmapped, "
               "shared-memory object removed with shm_unlink()\n",
               got == 0 ? "status 0" : "failure");

    return (ok && got == 0) ? 0 : -1;
}
