/*
 * ipc_pipe.c - IPC method 1: anonymous pipe
 *
 * pipe() gives two file descriptors: fd[0] = read end, fd[1] = write end.
 * The pipe has no name, so it only works between related processes: the child
 * inherits both descriptors across fork().
 *
 * Note: in the child, `data` is the copy inherited from the parent at fork().
 * It is used ONLY as the reference for the verification step; the numbers the
 * receiver prints and checks are the ones that arrived through the pipe.
 */
#include "common.h"

#include <stdio.h>
#include <unistd.h>

int ipc_pipe_run(const Dataset *data, int rounds, int verbose, double *elapsed_sec)
{
    int fd[2];

    /* 1. create the channel */
    if (pipe(fd) == -1) {
        perror("pipe");
        return -1;
    }

    fflush(stdout);
    double start = now_seconds();
    pid_t pid = fork();
    if (pid < 0) {
        perror("fork");
        close(fd[0]);
        close(fd[1]);
        return -1;
    }

    if (pid == 0) {
        /* ---------- receiver (child) ---------- */
        close(fd[1]); /* the receiver only reads: close the unused write end */
        int rc = stream_receive(fd[0], data, rounds, verbose);
        close(fd[0]);
        _exit(rc == 0 ? 0 : 2);
    }

    /* ---------- sender (parent) ---------- */
    close(fd[0]); /* the sender only writes: close the unused read end */
    if (verbose) {
        printf("  setup  : pipe() gave read fd=%d, write fd=%d; fork() created receiver pid %d\n",
               fd[0], fd[1], (int)pid);
        sender_announce(data);
    }

    /* 2. use the channel */
    int sent = stream_send(fd[1], data, rounds);

    /* 3. close / clean up */
    close(fd[1]); /* the receiver would see EOF here if it kept reading */
    int got = wait_for_receiver(pid);
    *elapsed_sec = now_seconds() - start;

    if (verbose)
        printf("  cleanup: both pipe ends closed, receiver exited (%s)\n",
               got == 0 ? "status 0" : "failure");

    return (sent == 0 && got == 0) ? 0 : -1;
}
