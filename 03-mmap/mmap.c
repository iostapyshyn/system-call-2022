#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <stdio.h>
#include <errno.h>
#include <unistd.h>
#include <sys/mman.h>
#include <stdlib.h>

#define PAGE_SIZE 4096

struct {
    int foobar __attribute__((aligned(PAGE_SIZE)));
} persistent = { .foobar = 23 };

/* Not persistent */
int barfoo = 42;

int setup_persistent(char *fn) {
    int fd = open(fn, O_RDWR | O_CREAT, 0666);
    if (fd == -1) {
        perror("open");
        return -1;
    }

    void *start = &persistent;
    size_t len = sizeof(persistent);

    if (ftruncate(fd, len) == -1) {
        perror("ftruncate");
        return -1;
    }

    void *ret = mmap(start, len,
                     PROT_READ|PROT_WRITE,
                     MAP_SHARED | MAP_FIXED,
                     fd, 0);
    if (ret == MAP_FAILED) {
        perror("mmap");
        return -1;
    }

    close(fd);

    return 0;
}

int main() {
    printf("section persistent: %lx--%lx\n",
           (unsigned long) &persistent,
           (unsigned long) &persistent + sizeof(persistent));
    // Install the persistent mapping
    if (setup_persistent("mmap.persistent") == -1) {
        perror("setup_persistent");
        return -1;
    }

    // For foobar, we see that each invokation of the programm will
    // yield an incremented result.
    // For barfoo, which is *NOT* in the persistent section, we will
    // always get the same result.
    printf("foobar(%p) = %d\n", (void *) &persistent.foobar, persistent.foobar);
    printf("barfoo(%p) = %d\n", (void *) &barfoo, barfoo++);
    persistent.foobar++;

    {// This is ugly and you should not do this in production code.

        // In order to see the memory mappings of the currently
        // running process, we use the pmap (for process-map) tool to
        // query the kernel (/proc/self/maps)
        char cmd[256];
        snprintf(cmd, 256, "pmap %d", getpid());
        printf("---- system(\"%s\"):\n", cmd);
        system(cmd);
    }

    return 0;
}
