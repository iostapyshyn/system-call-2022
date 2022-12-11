#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <stdio.h>
#include <errno.h>
#include <unistd.h>
#include <sys/mman.h>
#include <stdlib.h>
#include <stdbool.h>
#include <string.h>
#include <sys/xattr.h>
#include <stdint.h>

#define die(msg) do { perror(msg); exit(EXIT_FAILURE); } while(0)

// An helper function that maps a file to memory and returns its
// length in *len and an open file descriptor in *fd.
// On error, the function should return the null pointer;
char *map_file(char *fn, ssize_t *len, int *fd) {
    *fd = open(fn, O_RDONLY);
    if (*fd < 0)
        return NULL;

    *len = lseek(*fd, 0, SEEK_END);
    if (*len < 0)
        return NULL;

    void *map = mmap(NULL, *len, PROT_READ, MAP_PRIVATE, *fd, 0);
    if (map == MAP_FAILED)
        return NULL;

    return map;
}

// A (very) simple checksum function that calculates and additive checksum over a memory range.
uint64_t calc_checksum(void *data, size_t len) {
    uint64_t checksum = 0;

    // First sum as many bytes as uint64_t as possible
    uint64_t *ptr = (uint64_t *) data;
    while ((uint8_t *) ptr < ((uint8_t *) data + len)) {
        checksum += *ptr++;
    }

    // The rest (0-7 bytes) are added byte wise.
    char *cptr = (char *) ptr;
    while ((uint8_t *) cptr < ((uint8_t *) data + len)) {
        checksum += *cptr;
    }

    return checksum;
}

int main(int argc, char *argv[]) {
    // The name of the extended attribute where we store our checksum
    const char *xattr = "user.checksum";

    // Argument filename
    char *fn = NULL;

    // Should we reset the checksum?
    bool reset_checksum = false;

    // Argument parsing
    if (argc == 3 && strcmp(argv[1], "-r") == 0) {
        reset_checksum = true;
        fn = argv[2];
    } else if (argc == 2) {
        fn = argv[1];
    } else {
        fprintf(stderr, "usage: %s [-r] <FILE>\n", argv[0]);
        return 1;
    }

    int fd;
    off_t len;
    void *data;

    data = map_file(fn, &len, &fd);
    if (data == NULL)
        die("map_file");

    if (reset_checksum && fremovexattr(fd, xattr) < 0)
        die("fremovexattr");

    uint64_t saved;
    uint64_t checksum = calc_checksum(data, len);
    ssize_t attr_len = fgetxattr(fd, xattr, &saved, sizeof(saved));

    if (attr_len >= 0 && (saved != checksum || attr_len != sizeof(saved)))
        printf("Warning: checksum different!\n");
    else if (attr_len < 0)
        die("fgetxattr");

    if (fsetxattr(fd, xattr, &checksum, sizeof(checksum), 0) < 0)
        die("fsetxattr");

    return 0;
}
