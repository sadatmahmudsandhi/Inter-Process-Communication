/*
 * ipc_fifo.c - IPC method 2: named pipe (FIFO)
 *
 * A FIFO is a pipe that has a name in the file system, so even unrelated
 * processes could use it. mkfifo() creates the special file, both sides open()
 * it like a normal file, and unlink() removes it at the end.
 * open() blocks until the other side has opened its end as well - that is the
 * only "synchronization" a FIFO needs.
 */
#include "common.h"

#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <stdio.h>
#include <sys/stat.h>
#include <unistd.h>

int ipc_fifo_run(const Dataset *data, int rounds, int verbose, double *elapsed_sec)
{
    char path[64];
    snprintf(path, sizeof path, "/tmp/ipc_demo_fifo_%d", (int)getpid());

    /* 1. create the channel (remove a stale file with the same name first) */
    unlink(path);
    if (mkfifo(path, 0600) == -1) {
        perror("mkfifo");
        return -1;
    }

    fflush(stdout);
    double start = now_seconds();
    pid_t pid = fork();
    if (pid < 0) {
        perror("fork");
        unlink(path);
        return -1;
    }

    if (pid == 0) {
        /* ---------- receiver (child) ---------- */
        int fd = open(path, O_RDONLY); /* blocks until the sender opens its end */
        if (fd == -1) {
            if (errno != EINTR)
                perror("receiver: open fifo");
            _exit(3);
        }
        int rc = stream_receive(fd, data, rounds, verbose);
        close(fd);
        _exit(rc == 0 ? 0 : 2);
    }

    /* ---------- sender (parent) ---------- */
    if (verbose)
        printf("  setup  : mkfifo(\"%s\"); fork() created receiver pid %d\n", path, (int)pid);

    int sent = -1;
    int fd = open(path, O_WRONLY); /* blocks until the receiver opens its end */
    if (fd == -1) {
        if (errno != EINTR)
            perror("sender: open fifo");
        kill(pid, SIGTERM); /* the receiver would wait for us forever */
    } else {
        if (verbose)
            sender_announce(data);
        /* 2. use the channel */
        sent = stream_send(fd, data, rounds);
        close(fd);
    }

    /* 3. close / remove the channel */
    int got = wait_for_receiver(pid);
    *elapsed_sec = now_seconds() - start;
    unlink(path);

    if (verbose)
        printf("  cleanup: both ends closed, receiver exited (%s), FIFO file removed with unlink()\n",
               got == 0 ? "status 0" : "failure");

    return (sent == 0 && got == 0) ? 0 : -1;
}
