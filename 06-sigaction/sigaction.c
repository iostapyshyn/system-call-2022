#undef _GNU_SOURCE
#define _GNU_SOURCE
#include <unistd.h>
#include <string.h>
#include <signal.h>
#include <sys/mman.h>
#include <stdio.h>
#include <signal.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <sys/ucontext.h>

unsigned long PAGE_SIZE;

extern int main(void);

/* See Day 2: clone.
 *
 * Example: syscall_write("foobar = ", 23);
 */
int syscall_write(char *msg, int64_t number, char base) {
    write(1, msg, strlen(msg));

    char buffer[sizeof(number) * 8];
    char *p = &buffer[sizeof(number) * 8];
    int len = 1;
    *(--p) = '\n';
    if (number < 0) {
        write(1, "-", 1);
        number *= -1;
    }
    do {
        *(--p) =  "0123456789ABCDEF"[number % base];
        number /= base;
        len ++;
    } while (number != 0);
    write(1, p, len);
    return 0;
}

/* We have three different fault handlers to make our program
 * nearly "immortal":
 *
 * 1. sa_sigint:  Is invoked on Control-C.
 * 2. sa_sigsegv: Handle segmentation faults
 * 3. sa_sigill:  Jump over illegal instructions
*/

volatile bool do_exit = false;

void sa_sigint(int signo, siginfo_t *siginfo, void *ucontext) {
    (void) signo; (void) siginfo; (void) ucontext;

    write(STDOUT_FILENO, "SIGINT", sizeof("SIGINT"));
    do_exit = true;
}

void sa_sigsegv(int signo, siginfo_t *siginfo, void *ucontext) {
    (void) signo; (void) ucontext;

    syscall_write("SIGSEGV at 0x", (int64_t) siginfo->si_addr, 16);

    unsigned long addr = (unsigned long) siginfo->si_addr & ~(PAGE_SIZE-1);

    if (mmap((void *) addr, PAGE_SIZE, PROT_READ|PROT_WRITE,
             MAP_PRIVATE|MAP_ANONYMOUS|MAP_FIXED, 0, 0) == MAP_FAILED) {
        perror("mmap");
        _exit(1);
    }
}

void sa_sigill(int signo, siginfo_t *siginfo, void *ucontext) {
    (void) signo; (void) ucontext;

    unsigned long addr = (unsigned long) siginfo->si_addr;
    syscall_write("SIGILL at 0x", (int64_t) siginfo->si_addr, 16);

    unsigned long i, *frame = __builtin_frame_address(0);
    for (i = 0; *(unsigned long *) frame != addr; i++)
        frame = (unsigned long *) ((char *) frame + 1);

    syscall_write("delta = ", i, 10);

    *frame = addr+4;
}

int main(void) {
    // We get the actual page-size for this system. On x86, this
    // always return 4096, as this is the size of regular pages on
    // this architecture. We need this in the SIGSEGV handler.
    PAGE_SIZE = sysconf(_SC_PAGESIZE);

    struct sigaction sa_sigint_struct = {
        .sa_sigaction = sa_sigint,
        .sa_flags = SA_SIGINFO,
    };
    struct sigaction sa_sigsegv_struct = {
        .sa_sigaction = sa_sigsegv,
        .sa_flags = SA_SIGINFO,
    };
    struct sigaction sa_sigill_struct = {
        .sa_sigaction = sa_sigill,
        .sa_flags = SA_SIGINFO,
    };

    sigaction(SIGINT, &sa_sigint_struct, NULL);
    sigaction(SIGSEGV, &sa_sigsegv_struct, NULL);
    sigaction(SIGILL, &sa_sigill_struct, NULL);

    // We generate an invalid pointer that points _somewhere_! This is
    // undefined behavior, and we only hope for the best here. Perhaps
    // we should install a signal handler for SIGSEGV beforehand....
    uint32_t * addr = (uint32_t*)0xdeadbeef;

    // This will provoke a SIGSEGV
    *addr = 23;

    // Two ud2 instructions are exactly 4 bytes long
#define INVALID_OPCODE_32_BIT() asm("ud2; ud2;")

    // This will provoke a SIGILL
    INVALID_OPCODE_32_BIT();

    // Happy faulting, until someone sets the do_exit variable.
    // Perhaps the SIGINT handler?
    while(!do_exit) {
        sleep(1);
        addr += 22559;
        *addr = 42;
        INVALID_OPCODE_32_BIT();
    }

    { // Like in the mmap exercise, we use pmap to show our own memory
      // map, before exiting.
        char cmd[256];
        snprintf(cmd, 256, "pmap %d", getpid());
        printf("---- system(\"%s\"):\n", cmd);
        system(cmd);
    }

    return 0;
}
