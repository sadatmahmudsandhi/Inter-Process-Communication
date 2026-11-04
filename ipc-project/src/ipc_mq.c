/*
 * ipc_mq.c - IPC method 4: System V message queue
 *
 * The kernel keeps a queue of discrete, typed messages. msgsnd() appends one,
 * msgrcv() takes one (blocking if the queue is empty), so the kernel does the
 * synchronization for us. A queue outlives the process that made it, so it
 * must be removed explicitly with msgctl(IPC_RMID) - otherwise it stays behind
 * (check with `ipcs -q`).
 *
 * Protocol for one round (a message holds at most MQ_CHUNK numbers):
 *   1 header message (type MSG_HEADER) : count = how many numbers follow
 *   N data messages  (type MSG_DATA)   : count = numbers in this chunk, then the numbers
 */
#include "common.h"

#include <errno.h>
#include <signal.h>
#include <stdio.h>
#include <string.h>
#include <sys/ipc.h>
#include <sys/msg.h>
#include <unistd.h>

#define MQ_CHUNK 256 /* numbers per message (1 KB), well under the usual 8 KB limit */
#define MSG_HEADER 1
#define MSG_DATA 2

typedef struct {
    long mtype; /* required first member: the message type */
    int count;
    int nums[MQ_CHUNK];
} Message;

#define MSG_MAX_PAYLOAD (sizeof(Message) - sizeof(long)) /* msgsz excludes mtype */

static int mq_send(int qid, Message *m, int numbers)
{
    size_t size = sizeof(int) + (size_t)numbers * sizeof(int); /* count + numbers */
    while (msgsnd(qid, m, size, 0) == -1) {
        if (errno == EINTR && !was_interrupted())
            continue;
        return -1;
    }
    return 0;
}

static int mq_recv(int qid, Message *m, long type)
{
    while (msgrcv(qid, m, MSG_MAX_PAYLOAD, type, 0) == -1) {
        if (errno == EINTR && !was_interrupted())
            continue;
        return -1;
    }
    return 0;
}

static int send_dataset(int qid, const Dataset *d)
{
    Message m;

    m.mtype = MSG_HEADER;
    m.count = d->count;
    if (mq_send(qid, &m, 0) != 0)
        return -1;

    for (int off = 0; off < d->count; off += MQ_CHUNK) {
        int n = d->count - off;
        if (n > MQ_CHUNK)
            n = MQ_CHUNK;
        m.mtype = MSG_DATA;
        m.count = n;
        memcpy(m.nums, d->nums + off, (size_t)n * sizeof(int));
        if (mq_send(qid, &m, n) != 0)
            return -1;
    }
    return 0;
}

static int receive_dataset(int qid, Dataset *got)
{
    Message m;

    got->count = 0;
    if (mq_recv(qid, &m, MSG_HEADER) != 0)
        return -1;
    int total = m.count;
    if (total < 1 || total > MAX_NUMS)
        return -1;

    int have = 0;
    while (have < total) {
        if (mq_recv(qid, &m, MSG_DATA) != 0)
            return -1;
        if (m.count < 1 || m.count > MQ_CHUNK || have + m.count > total)
            return -1;
        memcpy(got->nums + have, m.nums, (size_t)m.count * sizeof(int));
        have += m.count;
    }
    got->count = total;
    return 0;
}

int ipc_mq_run(const Dataset *data, int rounds, int verbose, double *elapsed_sec)
{
    /* 1. create the channel (IPC_PRIVATE = a brand-new queue, no key clashes) */
    int qid = msgget(IPC_PRIVATE, IPC_CREAT | 0600);
    if (qid == -1) {
        perror("msgget");
        return -1;
    }

    fflush(stdout);
    double start = now_seconds();
    pid_t pid = fork();
    if (pid < 0) {
        perror("fork");
        msgctl(qid, IPC_RMID, NULL);
        return -1;
    }

    if (pid == 0) {
        /* ---------- receiver (child) ---------- */
        /* `data` is only the reference copy used for the final comparison */
        Dataset got;
        int ok = 1;
        for (int r = 0; r < rounds; r++) {
            if (receive_dataset(qid, &got) != 0) {
                ok = 0;
                break;
            }
            if (!receiver_check(data, &got, verbose && r == 0))
                ok = 0;
        }
        _exit(ok ? 0 : 2);
    }

    /* ---------- sender (parent) ---------- */
    if (verbose) {
        printf("  setup  : msgget(IPC_PRIVATE) gave queue id %d; fork() created receiver pid %d\n",
               qid, (int)pid);
        sender_announce(data);
    }

    /* 2. use the channel */
    int ok = 1;
    for (int r = 0; r < rounds; r++) {
        if (was_interrupted() || send_dataset(qid, data) != 0) {
            ok = 0;
            break;
        }
    }
    if (!ok)
        kill(pid, SIGTERM);

    /* 3. remove the channel */
    int got = wait_for_receiver(pid);
    *elapsed_sec = now_seconds() - start;
    msgctl(qid, IPC_RMID, NULL);

    if (verbose)
        printf("  cleanup: receiver exited (%s), queue %d removed with msgctl(IPC_RMID)\n",
               got == 0 ? "status 0" : "failure", qid);

    return (ok && got == 0) ? 0 : -1;
}
