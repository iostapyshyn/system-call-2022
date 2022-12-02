#include <stdio.h>
#include <fcntl.h>
#include <unistd.h>

#define BUFFER_SIZE 4096

static char buffer[BUFFER_SIZE];

static int out(const char *fn)
{
    int fd = open(fn, O_RDONLY);
    int ret = 0, bytes;

    if (fd == -1) {
        perror("open");
        return -1;
    }

    while ((bytes = read(fd, buffer, BUFFER_SIZE)) > 0) {
        ret += bytes;
        if (write(STDOUT_FILENO, buffer, bytes) == -1) {
            perror("write");
            ret = -1;
            goto out_close;
        }
    }

    if (bytes == -1) {
        perror("read");
        ret = -1;
    }

out_close:
    if (close(fd) == -1)
        perror("close");

    return ret;
}

int main(int argc, char *argv[])
{
    for (int i = 1; i < argc; i++)
    {
        if (out(argv[i]) == -1)
            return 1;
    }

    return 0;
}
