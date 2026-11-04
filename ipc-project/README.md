# Inter-Process Communication (IPC)

**Operating Systems Laboratory - Project Assignment - Group 2**

| Member | ID |
|---|---|
| Tamim Siddiquey | 1072420005101005 |
| Sadat Mahmud Sandhi | 1072420005101006 |

A command-line C program in which a **sender** process (the parent) sends a list of
numbers to a **receiver** process (a child created with `fork()`) through four different
IPC methods, and the receiver checks that it got exactly what was sent.

| # | IPC method | System calls used |
|---|---|---|
| 1 | Anonymous pipe | `pipe`, `read`, `write`, `close` |
| 2 | Named pipe (FIFO) | `mkfifo`, `open`, `read`, `write`, `close`, `unlink` |
| 3 | Shared memory + semaphores | `shm_open`, `ftruncate`, `mmap`, `sem_init`, `sem_wait`, `sem_post`, `sem_destroy`, `munmap`, `shm_unlink` |
| 4 | Message queue (System V) | `msgget`, `msgsnd`, `msgrcv`, `msgctl(IPC_RMID)` |

The assignment asks for at least two methods; this project implements all four.

## Requirements checklist

| Requirement | Where it is met |
|---|---|
| At least two IPC methods | four methods, one file each: `src/ipc_pipe.c`, `ipc_fifo.c`, `ipc_shm.c`, `ipc_mq.c` |
| Sender writes, receiver reads | parent = sender, `fork()`ed child = receiver |
| Setup and cleanup of each channel | each method creates, uses, then closes/removes its channel; the demo prints a `setup` and a `cleanup` line |
| Shared memory protected by a semaphore | `mutex` semaphore around every access, plus `empty`/`full` semaphores for hand-off (`src/ipc_shm.c`) |
| Input: typed or fixed test data | fixed data 1..10 by default, numbers on the command line, or `-i` to type them |
| Receiver prints exactly what it received | prints every number it got |
| Confirmation line | `Received 10 numbers, sum = 55` |
| Verify received data equals sent data | receiver compares what arrived byte-for-byte with the sent data and prints `IDENTICAL` or `MISMATCH`; the parent also checks the receiver's exit status |
| Short printed comparison | printed at the end of every full run (and written up below) |

## Set up in WSL + VS Code

1. **Install the tools** (once, in the WSL terminal):
   ```bash
   sudo apt update
   sudo apt install build-essential gdb
   ```
2. **Put the project in the Linux file system**, e.g. your home folder (`~/ipc-project`),
   *not* under `/mnt/c/...` (slower, and file permissions behave differently).
   If you downloaded the zip on Windows:
   ```bash
   cp /mnt/c/Users/<your-windows-name>/Downloads/ipc-project.zip ~
   cd ~ && unzip ipc-project.zip && cd ipc-project
   ```
3. **Open it in VS Code from WSL:**
   ```bash
   code .
   ```
   VS Code shows *WSL: Ubuntu* in the bottom-left corner. Install the **C/C++** extension
   (`ms-vscode.cpptools`) *inside WSL* when it offers to.
4. **Build and run** - either from the VS Code terminal (`` Ctrl+` ``) or with tasks:

   | What | Terminal | VS Code |
   |---|---|---|
   | Build | `make` | `Ctrl+Shift+B` |
   | Run the demo | `make run` | `Terminal > Run Task > run demo` |
   | Type your own numbers | `make run ARGS=-i` | `Run Task > run interactive...` |
   | Debug | `make debug` | `F5` (pick sender/parent or receiver/child) |

## Usage

```text
./build/ipc_demo [options] [number ...]

  number ...          the integers to send (default test data: 1..10)
  -i, --interactive   type the numbers in instead
  -m, --method NAME   pipe | fifo | shm | mq | all   (default: all)
  -n, --no-bench      skip the timing comparison
  -h, --help          show this help
```

```bash
./build/ipc_demo                       # fixed data 1..10, all four methods, then the comparison
./build/ipc_demo 5 10 15 20 25         # your own numbers
./build/ipc_demo -m shm 3 1 4 1 5 9    # only shared memory
./build/ipc_demo -i                    # type the numbers
```

Up to 1000 integers can be sent. The timing comparison runs only when two or more methods
are selected.

## Example output (default run, first method)

```text
=== 1) Anonymous pipe ===
  setup  : pipe() gave read fd=3, write fd=4; fork() created receiver pid 279
[sender   278] Sending 10 numbers (sum = 55):
    1 2 3 4 5 6 7 8 9 10
[receiver 279] Received 10 numbers:
    1 2 3 4 5 6 7 8 9 10
[receiver 279] Received 10 numbers, sum = 55
[receiver 279] Compared with the data that was sent: IDENTICAL
  cleanup: both pipe ends closed, receiver exited (status 0)
  RESULT : OK - the receiver got exactly what the sender sent
```

The other three methods print the same way, each with its own `setup` and `cleanup` line.

## How each method works

**Anonymous pipe.** `pipe()` returns two descriptors, one to read and one to write. The
child inherits both across `fork()`; each side closes the end it does not use. It has no
name, so it only works between related processes.

**Named pipe (FIFO).** The same idea, but the pipe has a path in the file system
(`/tmp/ipc_demo_fifo_<pid>`), so unrelated processes could use it too. Each side `open()`s
it; `open()` blocks until the other side arrives. `unlink()` removes it at the end.

**Shared memory + semaphores.** Both processes map the same block of memory
(`shm_open` + `mmap`), so there is no copy through the kernel, but the processes must
coordinate themselves. Three semaphores live inside the block:
`mutex` (only one process touches the data at a time), `empty` (slot is free, the sender may
write) and `full` (slot holds new data, the receiver may read). Waits use a 5-second
timeout so a crashed partner cannot hang the program.

**Message queue.** The kernel holds a queue of typed messages. The sender posts a header
message (how many numbers follow) and then data messages of up to 256 numbers each; the
receiver takes them with `msgrcv()`. A queue survives the process that made it, so it
must be removed with `msgctl(IPC_RMID)`.

**Verification.** After `fork()` the child holds a copy of the original data. It uses that
copy *only* as a reference: the numbers it prints and checks are the ones that came through
the channel, compared with `memcmp`. The receiver exits with status 0 if they are
identical, and the parent reads that status. Both the timing runs and the demo use this
check on every round.

## Comparison of the methods

|  | Anonymous pipe | FIFO | Shared memory + sem | Message queue |
|---|---|---|---|---|
| **Setup** | easiest: 1 call | easy: `mkfifo` + `open`, delete file afterwards | hardest: 6 calls, 3 semaphores | easy: 1 call, must be removed explicitly |
| **Extra synchronization** | no | no | **yes** (semaphores) | no |
| **Related processes only?** | yes | no | no | no |
| **Data model** | byte stream | byte stream | raw memory | discrete typed messages |
| **Speed** (example run, 4000 bytes per round) | ~3 us | ~3 us | ~25 us | ~6 us |

- **Easier to set up:** the anonymous pipe. One call, no name, nothing left to delete. Shared
  memory is the most work because of the extra semaphores.
- **Faster:** in our test the pipe and the FIFO were fastest. The numbers differ from run to
  run and between computers, so run the program yourself for your own table. Shared memory
  looks slow here because it avoids *copying*, but our single-slot buffer forces a semaphore
  hand-off and a context switch for every round. It wins when the processes share a large
  buffer and do not hand off after every few bytes.
- **Needs extra synchronization:** only shared memory. Without the semaphores, sender and
  receiver could read and write the same bytes at the same moment. For pipes, FIFOs and
  message queues the kernel does the waiting: `read()` and `msgrcv()` simply block until data
  arrives.

## Project layout

```text
ipc-project/
├── Makefile
├── README.md
├── src/
│   ├── main.c        command line, runs the four methods, timing + comparison
│   ├── common.h/.c   shared helpers: verification, byte-stream transfer, Ctrl+C handling
│   ├── ipc_pipe.c    method 1
│   ├── ipc_fifo.c    method 2
│   ├── ipc_shm.c     method 3
│   └── ipc_mq.c      method 4
└── .vscode/          build tasks, debug configs, IntelliSense settings
```

## Cleanup and troubleshooting

- **Normal runs leave nothing behind**: pipes are closed, the FIFO file is unlinked, shared
  memory and semaphores are destroyed, and the message queue is removed. The first
  `Ctrl+C` also triggers this cleanup; press it twice to force-quit.
- If a run was killed hard (for example `kill -9`), clear leftovers with:
  ```bash
  make cleanipc      # FIFO file and shared-memory object
  ipcs -q            # list message queues
  ipcrm -q <msqid>   # remove one
  ```
- `make: gcc: command not found` -> `sudo apt install build-essential`.
- Debugging a forked program: the two `F5` configurations choose which process gdb follows
  after `fork()`. Interactive input (`-i`) does not work under the debugger, so they pass
  fixed arguments - edit `args` in `.vscode/launch.json` to change them.
- WSL 2 is recommended (`wsl -l -v` shows the version). It runs a real Linux kernel, which
  provides everything used here: FIFOs, POSIX shared memory and semaphores, System V queues.
