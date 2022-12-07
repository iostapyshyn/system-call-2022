#define _GNU_SOURCE
#include <stdio.h>
#include <spawn.h>
#include <unistd.h>
#include <errno.h>
#include <fcntl.h>
#include <string.h>
#include <stdlib.h>
#include <stdbool.h>
#include <pthread.h>
#include <sys/types.h>
#include <sys/wait.h>

#define die(msg) do { perror(msg); exit(EXIT_FAILURE); } while(0)
#define BUFFER_SIZE 4096

/* For each filter process, we will generate a proc object */
struct proc {
    char *cmd;  // command line
    pid_t pid;  // process id of running process. 0 if exited
    int stdin;  // stdin file descriptor of process (pipe)
    int stdout; // stdout file descriptor of process

    // For the output, we save the last char that was printed by this
    // process. We use this to prefix all lines with a banner a la
    // "[CMD]".
    char last_char;
};

static int nprocs;         // Number of started filter processes
static struct proc *procs; // Dynamically-allocated array of procs

// This function starts the filter (proc->cmd) as a new child process
// and connects its stdin and stdout via pipes (proc->{stdin,stdout})
// to the parent process.
//
// We also start the process wrapped by stdbuf(1) to force
// line-buffered stdio for a more interactive experience on the terminal
static int start_proc(struct proc *proc) {
    // We build an array for execv that uses the shell to execute the
    // given command. Furthermore, we use the stdbuf tool to start the
    // filter with line-buffered output.

    //
    // HINT: We use the glibc asprintf(3), as I'm too lazy doing the
    //       strlen()+malloc()+snprintf() myself.
    //       Therefore we have to define _GNU_SOURCE at the top.
    char *stdbuf_cmd;
    asprintf(&stdbuf_cmd, "stdbuf -oL %s", proc->cmd);
    char *argv[] = {"sh", "-c", stdbuf_cmd, 0 };

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

    // "magic variable": array of pointers to environment variables.
    // This symbol is described in environ(2)
    extern char **environ;

    // We spawn the filter process.
    int e;
    if (!(e = posix_spawn(&proc->pid, "/bin/sh", &fa, 0, argv,  environ))) {
        // On success, we free the allocated memory.
        posix_spawn_file_actions_destroy(&fa);
        free(stdbuf_cmd);

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
        free(stdbuf_cmd);
        return -1;
    }
}

void do_write(int fd, char *buffer, size_t buffer_size) {
    size_t acc = 0;
    int ret;

    while (acc < buffer_size) {
        ret = write(fd, buffer+acc, buffer_size-acc);
        if (ret < 0)
            die("write");
        acc += ret;
    }
}

void print_cmd(int fd, struct proc *proc) {
    do_write(fd, "[", 1);
    do_write(fd, proc->cmd, strlen(proc->cmd));
    do_write(fd, "] ", 2);
}

/* Reap the child, close its stdout and set its PID to 0. */
void reap_proc(struct proc *proc) {
    int wstatus, exitcode = -1;

    if (waitpid(proc->pid, &wstatus, 0) < 0)
        die("waitpid");

    if (WIFEXITED(wstatus))
        exitcode = WEXITSTATUS(wstatus);
    else if (WIFSIGNALED(wstatus))
        exitcode = 128 + WTERMSIG(wstatus);

    printf("[%s] filter exited. exitcode=%d\n", proc->cmd, exitcode);
    close(proc->stdout);

    proc->pid = 0;
}

void drain_proc(int outfd, struct proc *proc, char *buffer, size_t buffer_size) {
    int bytes = read(proc->stdout, buffer, buffer_size);
    if (bytes < 0)
        die("read");

    if (!bytes) {               /* EOF: process likely died. */
        reap_proc(proc);
        return;
    }

    char *ptr = buffer;
    for (int i = 0; i < bytes; i++) {
        if (proc->last_char == '\n') {
            size_t len = buffer+i-ptr;

            if (len)
                do_write(outfd, ptr, len);

            print_cmd(outfd, proc);
            ptr += len;
        }

        proc->last_char = buffer[i];
    }

    if (ptr < buffer + bytes)
        do_write(outfd, ptr, buffer+bytes-ptr);
}

bool drain_input(int infd, char *buffer, size_t buffer_size) {
    int bytes = read(infd, buffer, buffer_size);
    if (bytes < 0)
        die("read");

    bool eof = !bytes;

    for (int i = 0; i < nprocs; i++) {
        struct proc *proc = &procs[i];

        if (eof) {
            close(proc->stdin); /* EOF: processes should die shortly and will be
                                 * reaped in drain_proc */
            continue;
        }

        do_write(proc->stdin, buffer, bytes);
    }

    return eof;
}

int main(int argc, char *argv[]) {
    if (argc <= 1) {
        fprintf(stderr, "usage: %s [CMD-1] (<CMD-2> <CMD-3> ...)", argv[0]);
        return -1;
    }

    // We allocate an array of proc objects
    nprocs = argc - 1;
    procs = malloc(nprocs * sizeof(struct proc));
    if (!procs)
        die("malloc");

    char *buffer = malloc(BUFFER_SIZE);
    if (!buffer)
        die("malloc");

    // Initialize proc objects and start the filter
    for (int i = 0; i < nprocs; i++) {
        procs[i].cmd  = argv[i+1];
        procs[i].last_char = '\n';
        int rc = start_proc(&procs[i]);
        if (rc < 0) die("start_filter");

        fprintf(stderr, "[%s] Started filter as pid %d\n", procs[i].cmd, procs[i].pid);
    }

    bool eof = false;
    bool some = true;

    while (1) {
        int nfds = 0;
        fd_set rfds;
        FD_ZERO(&rfds);

        if (!eof) {
            nfds = STDIN_FILENO+1;
            FD_SET(STDIN_FILENO, &rfds);
        }

        some = false;
        for (int i = 0; i < nprocs; i++) {
            if (procs[i].pid == 0)
                continue;

            FD_SET(procs[i].stdout, &rfds);
            if (procs[i].stdout+1 > nfds)
                nfds = procs[i].stdout+1;

            some = true;
        }
        if (!some)
            break;

        if (select(nfds, &rfds, NULL, NULL, NULL) < 0)
            die("select");

        /* First, drain the children */
        for (int i = 0; i < nprocs; i++) {
            struct proc *proc = &procs[i];

            if (!FD_ISSET(proc->stdout, &rfds))
                continue;
            drain_proc(STDOUT_FILENO, proc, buffer, BUFFER_SIZE);
        }

        /* Now, drain the standard input */
        if (!FD_ISSET(STDIN_FILENO, &rfds))
            continue;

        eof = drain_input(STDIN_FILENO, buffer, BUFFER_SIZE);
    }

    free(procs);
    free(buffer);

    return 0;
}
