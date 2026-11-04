/*
 * main.c - driver for the IPC demo
 *
 * Runs each chosen IPC method once with the user's data (the demo), then times
 * all of them on a larger data set and prints a short comparison.
 */
#include "common.h"

#include <errno.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define BENCH_NUMS 1000   /* numbers per round in the timing test */
#define BENCH_ROUNDS 5000 /* rounds per timing run */
#define BENCH_REPEAT 3    /* timing runs per method; the best one is reported */

typedef struct {
    const char *key;   /* name used with -m */
    const char *title; /* name shown in the output */
    ipc_run_fn run;
    int setup_calls;   /* system calls needed to create the channel */
    const char *sync;  /* does the programmer have to add synchronization? */
} Method;

static const Method METHODS[] = {
    {"pipe", "Anonymous pipe", ipc_pipe_run, 1, "no"},
    {"fifo", "Named pipe (FIFO)", ipc_fifo_run, 2, "no"},
    {"shm", "Shared memory + semaphores", ipc_shm_run, 6, "YES"},
    {"mq", "Message queue", ipc_mq_run, 1, "no"},
};
#define NMETHODS ((int)(sizeof METHODS / sizeof METHODS[0]))

static void usage(const char *prog)
{
    printf("Usage: %s [options] [number ...]\n"
           "\n"
           "Sends a list of integers from a sender process to a receiver process\n"
           "(created with fork) through different IPC methods and checks that the\n"
           "receiver got exactly what was sent.\n"
           "\n"
           "  number ...          the integers to send (default test data: 1..10)\n"
           "  -i, --interactive   type the numbers in instead\n"
           "  -m, --method NAME   pipe | fifo | shm | mq | all   (default: all)\n"
           "  -n, --no-bench      skip the timing comparison\n"
           "  -h, --help          show this help\n"
           "\n"
           "Examples:\n"
           "  %s\n"
           "  %s 5 10 15 20 25\n"
           "  %s -m shm 3 1 4 1 5 9 2 6\n"
           "  %s -i\n",
           prog, prog, prog, prog, prog);
}

static int parse_int(const char *s, int *out)
{
    char *end;
    errno = 0;
    long v = strtol(s, &end, 10);
    if (errno != 0 || end == s || *end != '\0' || v < INT_MIN || v > INT_MAX)
        return -1;
    *out = (int)v;
    return 0;
}

static void fill_default(Dataset *d)
{
    d->count = 10;
    for (int i = 0; i < 10; i++)
        d->nums[i] = i + 1;
}

static int read_interactive(Dataset *d)
{
    char line[16384];

    printf("Type up to %d integers separated by spaces or commas, then press Enter\n"
           "(just Enter = the default test data 1..10)\n> ",
           MAX_NUMS);
    fflush(stdout);

    if (!fgets(line, sizeof line, stdin)) {
        fprintf(stderr, "No input.\n");
        return -1;
    }
    if (!strchr(line, '\n') && !feof(stdin)) {
        fprintf(stderr, "That line is too long.\n");
        return -1;
    }

    d->count = 0;
    for (char *tok = strtok(line, " ,\t\r\n"); tok; tok = strtok(NULL, " ,\t\r\n")) {
        int v;
        if (parse_int(tok, &v) != 0) {
            fprintf(stderr, "'%s' is not a valid integer.\n", tok);
            return -1;
        }
        if (d->count >= MAX_NUMS) {
            fprintf(stderr, "Too many numbers (maximum %d).\n", MAX_NUMS);
            return -1;
        }
        d->nums[d->count++] = v;
    }
    if (d->count == 0)
        fill_default(d);
    return 0;
}

/* Times every selected method on a bigger data set and prints the comparison. */
static int run_benchmark(const Method **sel, int nsel)
{
    static Dataset big;
    big.count = BENCH_NUMS;
    for (int i = 0; i < BENCH_NUMS; i++)
        big.nums[i] = i + 1;

    double payload = (double)BENCH_NUMS * sizeof(int);
    double best_us = -1.0;
    const char *best_name = NULL;
    const char *best_key = NULL;
    int shm_measured = 0;
    int failures = 0;

    printf("\n=== Comparison: %d rounds x %d numbers (%d bytes per round), best of %d runs ===\n",
           BENCH_ROUNDS, BENCH_NUMS, (int)payload, BENCH_REPEAT);
    printf("%-28s %11s %11s %11s %9s\n", "Method", "Setup calls", "Extra sync", "us / round", "MB / s");

    for (int k = 0; k < nsel; k++) {
        const Method *m = sel[k];
        double best_t = -1.0;
        int ok = 1;

        for (int rep = 0; rep < BENCH_REPEAT; rep++) {
            double t;
            if (m->run(&big, BENCH_ROUNDS, 0, &t) != 0) {
                ok = 0;
                break;
            }
            if (best_t < 0 || t < best_t)
                best_t = t;
        }

        if (was_interrupted()) {
            printf("\nInterrupted - channels were cleaned up.\n");
            return failures + 1;
        }
        if (!ok) {
            printf("%-28s %11d %11s %11s %9s\n", m->title, m->setup_calls, m->sync, "FAILED", "-");
            failures++;
            continue;
        }

        double us = best_t / BENCH_ROUNDS * 1e6;
        double mbs = payload * BENCH_ROUNDS / best_t / 1e6;
        printf("%-28s %11d %11s %11.1f %9.0f\n", m->title, m->setup_calls, m->sync, us, mbs);
        if (!strcmp(m->key, "shm"))
            shm_measured = 1;
        if (best_us < 0 || us < best_us) {
            best_us = us;
            best_name = m->title;
            best_key = m->key;
        }
    }

    printf("\nShort comparison\n");
    printf("  Easiest to set up : anonymous pipe - one call, pipe(); no name, nothing to delete.\n"
           "                      A FIFO also needs a path (mkfifo) and an unlink() afterwards;\n"
           "                      shared memory is the longest: shm_open, ftruncate, mmap, 3x sem_init.\n");
    if (best_name)
        printf("  Fastest (measured): %s - %.1f us per %d-byte transfer on this machine.\n"
               "                      (numbers change from run to run and between PCs / WSL versions)\n",
               best_name, best_us, (int)payload);
    if (shm_measured && best_key && strcmp(best_key, "shm") != 0)
        printf("  Why not shared memory: it avoids copying data through the kernel, but this test uses a\n"
               "                      one-slot buffer, so every round needs a semaphore hand-off and a\n"
               "                      context switch. Pipes and queues can buffer several rounds at once.\n");
    printf("  Extra sync needed : only shared memory. Without the semaphores the two processes could\n"
           "                      read and write the same bytes at the same time. Pipes, FIFOs and\n"
           "                      message queues are synchronized by the kernel: read() / msgrcv()\n"
           "                      simply block until data arrives.\n"
           "  Also worth knowing: pipes and FIFOs are plain byte streams, a message queue keeps\n"
           "                      message boundaries and types.\n");
    return failures;
}

int main(int argc, char **argv)
{
    setvbuf(stdout, NULL, _IOLBF, 0); /* line-buffered, so sender and receiver output stay in order */

    Dataset data;
    data.count = 0;
    int interactive = 0;
    int bench = 1;
    const char *method_key = "all";

    for (int i = 1; i < argc; i++) {
        const char *a = argv[i];
        int v;

        if (!strcmp(a, "-h") || !strcmp(a, "--help")) {
            usage(argv[0]);
            return 0;
        } else if (!strcmp(a, "-i") || !strcmp(a, "--interactive")) {
            interactive = 1;
        } else if (!strcmp(a, "-n") || !strcmp(a, "--no-bench")) {
            bench = 0;
        } else if (!strcmp(a, "-m") || !strcmp(a, "--method")) {
            if (++i >= argc) {
                fprintf(stderr, "%s needs a value (pipe, fifo, shm, mq or all)\n", a);
                return 2;
            }
            method_key = argv[i];
        } else if (parse_int(a, &v) == 0) {
            if (data.count >= MAX_NUMS) {
                fprintf(stderr, "Too many numbers (maximum %d).\n", MAX_NUMS);
                return 2;
            }
            data.nums[data.count++] = v;
        } else {
            fprintf(stderr, "Unknown option or bad number: %s  (try --help)\n", a);
            return 2;
        }
    }

    if (interactive && data.count > 0) {
        fprintf(stderr, "Use either -i or numbers on the command line, not both.\n");
        return 2;
    }

    const Method *sel[NMETHODS];
    int nsel = 0;
    for (int k = 0; k < NMETHODS; k++) {
        if (!strcmp(method_key, "all") || !strcmp(method_key, METHODS[k].key))
            sel[nsel++] = &METHODS[k];
    }
    if (nsel == 0) {
        fprintf(stderr, "Unknown method '%s' (use pipe, fifo, shm, mq or all).\n", method_key);
        return 2;
    }

    if (interactive) {
        if (read_interactive(&data) != 0)
            return 2;
    } else if (data.count == 0) {
        fill_default(&data);
    }

    setup_signals();

    printf("Inter-Process Communication (IPC) demo - Operating Systems Lab, Group 2\n"
           "Sender = parent process, receiver = child process (created with fork()).\n"
           "Data to send: %d numbers\n",
           data.count);

    int failures = 0;
    for (int k = 0; k < nsel && !was_interrupted(); k++) {
        double t;
        printf("\n=== %d) %s ===\n", k + 1, sel[k]->title);
        if (sel[k]->run(&data, 1, 1, &t) == 0) {
            printf("  RESULT : OK - the receiver got exactly what the sender sent\n");
        } else {
            printf("  RESULT : FAILED\n");
            failures++;
        }
    }

    if (nsel >= 2 && bench && !was_interrupted())
        failures += run_benchmark(sel, nsel);

    if (was_interrupted())
        printf("\nInterrupted by Ctrl+C - all IPC channels were cleaned up.\n");

    return failures ? 1 : 0;
}
