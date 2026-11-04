/*
 * common.h - declarations shared by every IPC method
 * Operating Systems Lab - Inter-Process Communication (IPC), Group 2
 */
#ifndef COMMON_H
#define COMMON_H

#include <sys/types.h>

#define MAX_NUMS 1000 /* the largest list that can be sent in one go */

/* The data being sent: a count followed by that many integers. */
typedef struct {
    int count;
    int nums[MAX_NUMS];
} Dataset;

/*
 * Every IPC method has the same shape:
 *   1. create the channel
 *   2. fork() a receiver (the child); the parent is the sender
 *   3. send `data` `rounds` times (1 round for the demo, many for timing)
 *   4. wait for the receiver, then close / remove the channel
 *
 * Returns 0 only if every round was received exactly as it was sent.
 * `verbose` prints the step-by-step demo; `elapsed_sec` receives the time taken.
 */
typedef int (*ipc_run_fn)(const Dataset *data, int rounds, int verbose, double *elapsed_sec);

int ipc_pipe_run(const Dataset *data, int rounds, int verbose, double *elapsed_sec);
int ipc_fifo_run(const Dataset *data, int rounds, int verbose, double *elapsed_sec);
int ipc_shm_run(const Dataset *data, int rounds, int verbose, double *elapsed_sec);
int ipc_mq_run(const Dataset *data, int rounds, int verbose, double *elapsed_sec);

/* ---- helpers implemented in common.c ---- */

/* Install the Ctrl+C handler (so channels are still cleaned up) and ignore SIGPIPE. */
void setup_signals(void);
int was_interrupted(void);

double now_seconds(void);
void print_numbers(const int *nums, int count);

/* Sender side: print what is about to be sent. */
void sender_announce(const Dataset *d);

/*
 * Receiver side: compare what arrived (`got`) with the reference copy
 * (`expected`). Prints the numbers, "Received N numbers, sum = S" and the
 * verdict when `show` is non-zero. Returns 1 if identical, 0 otherwise.
 */
int receiver_check(const Dataset *expected, const Dataset *got, int show);

/* Parent: wait for the receiver child. Returns 0 if it exited with status 0. */
int wait_for_receiver(pid_t pid);

/* Byte-stream helpers used by the pipe and the FIFO (both are plain byte streams). */
int stream_send(int fd, const Dataset *d, int rounds);
int stream_receive(int fd, const Dataset *expected, int rounds, int show_first);

#endif /* COMMON_H */
