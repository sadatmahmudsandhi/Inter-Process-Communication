/*
 * common.c - helpers shared by every IPC method
 */
#include "common.h"

#include <errno.h>
#include <signal.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

/* ------------------------------------------------------------------ */
/* Ctrl+C handling                                                     */
/* ------------------------------------------------------------------ */
/*
 * The first Ctrl+C only sets a flag. Blocked calls (read, sem_wait, msgrcv...)
 * return EINTR, the code notices the flag, gives up, and the normal cleanup path
 * still closes the pipe / unlinks the FIFO / removes shared memory and the queue.
 * A second Ctrl+C kills the program the usual way.
 */
static volatile sig_atomic_t g_interrupted = 0;

static void on_sigint(int sig)
{
    (void)sig;
    if (g_interrupted) {
        struct sigaction dfl;
        memset(&dfl, 0, sizeof dfl);
        dfl.sa_handler = SIG_DFL;
        sigemptyset(&dfl.sa_mask);
        sigaction(SIGINT, &dfl, NULL);
        raise(SIGINT);
    }
    g_interrupted = 1;
}

void setup_signals(void)
{
    struct sigaction sa;
    memset(&sa, 0, sizeof sa);
    sa.sa_handler = on_sigint;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = 0; /* no SA_RESTART: blocked system calls return EINTR */
    sigaction(SIGINT, &sa, NULL);

    signal(SIGPIPE, SIG_IGN); /* a dead receiver gives EPIPE instead of killing us */
}

int was_interrupted(void)
{
    return g_interrupted != 0;
}

/* ------------------------------------------------------------------ */
/* Small utilities                                                     */
/* ------------------------------------------------------------------ */
double now_seconds(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (double)ts.tv_sec + (double)ts.tv_nsec / 1e9;
}

static long long sum_of(const int *nums, int count)
{
    long long s = 0;
    for (int i = 0; i < count; i++)
        s += nums[i];
    return s;
}

void print_numbers(const int *nums, int count)
{
    for (int i = 0; i < count; i++) {
        if (i % 10 == 0)
            printf("    ");
        printf("%d%c", nums[i], (i % 10 == 9 || i == count - 1) ? '\n' : ' ');
    }
}

void sender_announce(const Dataset *d)
{
    printf("[sender   %d] Sending %d numbers (sum = %lld):\n", (int)getpid(), d->count,
           sum_of(d->nums, d->count));
    print_numbers(d->nums, d->count);
}

int receiver_check(const Dataset *expected, const Dataset *got, int show)
{
    int valid = got->count >= 1 && got->count <= MAX_NUMS;
    int same = valid && got->count == expected->count &&
               memcmp(got->nums, expected->nums, (size_t)got->count * sizeof(int)) == 0;

    if (show) {
        int pid = (int)getpid();
        if (valid) {
            printf("[receiver %d] Received %d numbers:\n", pid, got->count);
            print_numbers(got->nums, got->count);
            printf("[receiver %d] Received %d numbers, sum = %lld\n", pid, got->count,
                   sum_of(got->nums, got->count));
        } else {
            printf("[receiver %d] Received an invalid length (%d)\n", pid, got->count);
        }
        printf("[receiver %d] Compared with the data that was sent: %s\n", pid,
               same ? "IDENTICAL" : "MISMATCH");
    }
    return same;
}

int wait_for_receiver(pid_t pid)
{
    int status;
    for (;;) {
        pid_t w = waitpid(pid, &status, 0);
        if (w == pid)
            break;
        if (w == -1 && errno == EINTR) {
            if (was_interrupted())
                kill(pid, SIGTERM); /* Ctrl+C: ask the receiver to stop too */
            continue;
        }
        perror("waitpid");
        return -1;
    }
    return (WIFEXITED(status) && WEXITSTATUS(status) == 0) ? 0 : -1;
}

/* ------------------------------------------------------------------ */
/* Byte-stream transfer (anonymous pipe and FIFO)                      */
/* ------------------------------------------------------------------ */

/* read()/write() may transfer less than asked, so loop until everything is done. */
static int read_full(int fd, void *buf, size_t n)
{
    char *p = buf;
    while (n > 0) {
        ssize_t r = read(fd, p, n);
        if (r < 0) {
            if (errno == EINTR && !was_interrupted())
                continue;
            return -1;
        }
        if (r == 0) /* EOF: the other side closed early */
            return -1;
        p += r;
        n -= (size_t)r;
    }
    return 0;
}

static int write_full(int fd, const void *buf, size_t n)
{
    const char *p = buf;
    while (n > 0) {
        ssize_t w = write(fd, p, n);
        if (w < 0) {
            if (errno == EINTR && !was_interrupted())
                continue;
            return -1;
        }
        p += w;
        n -= (size_t)w;
    }
    return 0;
}

/* Wire format on a stream: [int count][count x int]. */
int stream_send(int fd, const Dataset *d, int rounds)
{
    /* count and nums[] are contiguous at the start of Dataset, so one write sends both */
    size_t bytes = offsetof(Dataset, nums) + (size_t)d->count * sizeof(int);
    for (int r = 0; r < rounds; r++) {
        if (was_interrupted() || write_full(fd, d, bytes) != 0)
            return -1;
    }
    return 0;
}

int stream_receive(int fd, const Dataset *expected, int rounds, int show_first)
{
    Dataset got;
    int all_ok = 1;

    for (int r = 0; r < rounds; r++) {
        if (was_interrupted())
            return -1;
        if (read_full(fd, &got.count, sizeof got.count) != 0)
            return -1;
        if (got.count < 1 || got.count > MAX_NUMS)
            return -1;
        if (read_full(fd, got.nums, (size_t)got.count * sizeof(int)) != 0)
            return -1;
        if (!receiver_check(expected, &got, show_first && r == 0))
            all_ok = 0;
    }
    return all_ok ? 0 : -1;
}
