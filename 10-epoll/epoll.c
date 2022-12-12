#define _GNU_SOURCE
#include <stdio.h>
#include <spawn.h>
#include <time.h>
#include <unistd.h>
#include <errno.h>
#include <fcntl.h>
#include <string.h>
#include <stdlib.h>
#include <stdbool.h>
#include <sys/types.h>
#include <sys/epoll.h>
#include <assert.h>
#include <limits.h>

#define MAX_EVENTS 16
#define die(msg) do { perror(msg); exit(EXIT_FAILURE); } while(0)
#define ARRAY_SIZE(x) (sizeof(x)/sizeof(*x))

/* For each filter process, we will generate a proc object */
struct proc {
    char    *cmd;   // command line
    pid_t   pid;    // process id of running process. 0 if exited
    int     stdin;  // stdin file descriptor of process (pipe)
    int     stdout; // stdout file descriptor of process
};

static int nprocs;         // Number of started filter processes
static struct proc *procs; // Dynamically-allocated array of procs

////////////////////////////////////////////////////////////////
// HINT: You have already seen this in the in the select exercise
////////////////////////////////////////////////////////////////

// This function starts the filter (proc->cmd) as a new child process
// and connects its stdin and stdout via pipes (proc->{stdin,stdout})
// to the parent process.
//
// We also start the process wrapped by stdbuf(1) to force
// line-buffered stdio for a more interactive experience on the terminal
static int start_proc(struct proc *proc) {
    // We build an array for execv that uses the shell to execute the
    // given command.
    char *argv[] = {"sh", "-c", proc->cmd, 0 };

    // We create two pipe pairs, where [0] is the reading end
    // and [1] the writing end of the pair. We also set the O_CLOEXEC
    // flag to close both descriptors when the child process is exec'ed.
    int stdin[2], stdout[2];
	if (pipe2(stdin,  O_CLOEXEC)) return -1;
    if (pipe2(stdout, O_CLOEXEC)) return -1;

    // For starting the filter, we use posix_spawn, which gives us an
    // interface around fork+exec to perform standard process
    // spawning. We use a filter action to copy our pipe descriptors to
    // the stdin (0) and stdout (1) handles within the child.
    // Internally, posix_spawn will do a dup2(2). For example,
    //
    //     dup2(stdin[0], STDIN_FILENO);
    posix_spawn_file_actions_t fa;
	posix_spawn_file_actions_init(&fa);
    posix_spawn_file_actions_adddup2(&fa, stdin[0],  STDIN_FILENO);
    posix_spawn_file_actions_adddup2(&fa, stdout[1], STDOUT_FILENO);

    // "magic variable": array of pointers to environment variantes.
    // This symbol is described in environ(2)
    extern char **environ;

    // We spawn the filter process.
    int e;
    if (!(e = posix_spawn(&proc->pid, "/bin/sh", &fa, 0, argv,  environ))) {
        // On success, we free the allocated memory.
        posix_spawn_file_actions_destroy(&fa);

        // We are within the parent process. Therefore, we copy our
        // pipe ends to the proc object and close the ends that are
        // also used within the child (to save file descriptors)

        // stdin of filter
        proc->stdin = stdin[1]; // write end
        close(stdin[0]);        // read end

        // stdout of filter
        proc->stdout = stdout[0]; // read end
        close(stdout[1]);         // write end

        return 0;
	} else {
        // posix_spawn failed.
        errno = e;
        return -1;
    }
}


// FIXME: Implement a 'int copy_splice(int in_fd, int out_fd);'

// This function prints an array of uint64_t (elements) as line with
// throughput measures. The function throttles its output to one line
// per second.

// Example Output:
//  2860.20MiB/s 2860.26MiB/s 2860.23MiB/s 2860.25MiB/s 2860.29MiB/s
void print_throughput(uint64_t *bytes, int elements) {
    static struct timespec last = { 0 };

    struct timespec now;
    if (clock_gettime(CLOCK_REALTIME, &now) < 0)
        die("clock_gettime");

    if (now.tv_sec > last.tv_sec && last.tv_sec > 0) {
        double delta = now.tv_sec - last.tv_sec;
        delta += (now.tv_nsec - last.tv_nsec) / 1e9;
        for (int i = 0; i < elements; i++) {
            fprintf(stderr, " %.2fMiB/s", bytes[i]/delta/1024/1024);
        }
        fprintf(stderr, "\n");
        memset(bytes, 0, elements * sizeof(uint64_t));
        last = now;
    } else if (last.tv_sec == 0) {
        last = now;
    }
}

ssize_t splice_copy(int fd_in, int fd_out) {
    static char buffer[4096];   /* Not thread-safe! */

    ssize_t ret;

    ret = splice(fd_in, NULL, fd_out, NULL, SIZE_MAX, SPLICE_F_NONBLOCK);
    if (ret >= 0)
        return ret;
    if (ret < 0 && errno == EAGAIN)
        return 0;

    /* Terminals cannot be spliced, copy normally through userspace: */

    ret = read(fd_in, buffer, ARRAY_SIZE(buffer));
    if (ret < 0)
        die("read");

    ssize_t len = ret, acc = 0;
    while (acc < len) {
        int ret = write(fd_out, buffer+acc, len-acc);
        if (ret < 0)
            die("write");
        acc += ret;
    }

    return acc;
}

int main(int argc, char *argv[]) {
    if (argc <= 1) {
        fprintf(stderr, "usage: %s [CMD-1]\n", argv[0]);
        return -1;
    }
    // We allocate an array of proc objects
    nprocs = argc - 1;
    procs  = malloc(nprocs * sizeof(struct proc));
    if (!procs) die("malloc");

    // Initialize proc objects and start the filter
    for (int i = 0; i < nprocs; i++) {
        procs[i].cmd  = argv[i+1];
        int rc = start_proc(&procs[i]);
        if (rc < 0) die("start_filter");

        fprintf(stderr, "[%s] Started filter as pid %d\n", procs[i].cmd, procs[i].pid);
    }

    int *input_fds  = malloc(sizeof(int) * (nprocs + 1));
    int *output_fds = malloc(sizeof(int) * (nprocs + 1));
    uint64_t *throughput = calloc(nprocs + 1, sizeof(uint64_t));
    if (!output_fds || !input_fds || !throughput)
        die("malloc");

    // Arrange file descriptors in pairs of input -> output
    input_fds[0] = STDIN_FILENO;
    for (int i = 0; i < nprocs; i++) {
        output_fds[i]  = procs[i].stdin;
        input_fds[i+1] = procs[i].stdout;
    }
    output_fds[nprocs] = STDOUT_FILENO;

    // Setup epoll to listen on the input descriptors
    int epfd = epoll_create1(0);
    if (epfd < 0)
        die("epoll_create");

    for (int i = 0; i < nprocs+1; i++) {
        struct epoll_event ev = {
            .events = EPOLLIN | EPOLLHUP,
            .data = {
                .u64 = i,
            },
        };

        int ret = epoll_ctl(epfd, EPOLL_CTL_ADD, input_fds[i], &ev);
        if (ret < 0)
            die("epoll_ctl");
    }

    int remaining_fds = nprocs+1;

    // Receive events and copy data around.
    while (remaining_fds) {
        struct epoll_event evs[MAX_EVENTS];
        int nfds;

        nfds = epoll_wait(epfd, evs, ARRAY_SIZE(evs), -1);
        if (nfds < 0)
            die("epoll_wait");

        for (int i = 0; i < nfds; i++) {
            int index = evs[i].data.u64;

            int ret = 0;
            if (evs[i].events & EPOLLIN) {
                ret = splice_copy(input_fds[index], output_fds[index]);
                if (ret < 0)
                    die("splice");
            }

            if (evs[i].events & EPOLLHUP && !ret) {
                ret = epoll_ctl(epfd, EPOLL_CTL_DEL, input_fds[index], NULL);
                if (ret < 0)
                    die("epoll_ctl");

                close(input_fds[index]);
                close(output_fds[index]);
                remaining_fds--;
            }

            throughput[index] += ret;
        }

        print_throughput(throughput, nprocs+1);
    }

    close(epfd);
    free(throughput);
    free(output_fds);
    free(input_fds);
    free(procs);

    return 0;
}
