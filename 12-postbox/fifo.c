static const char * const fifo_fn = "fifo";

int fifo_prepare(int epoll_fd) {
    int ret, fd;

    /* Re-create the FIFO node */
    ret = unlink(fifo_fn);
    if (ret && errno != ENOENT)
        die("unlink");

    ret = mknod(fifo_fn, 0666 | S_IFIFO, 0);
    if (ret < 0)
        die("mknod");

    fd = open(fifo_fn, O_RDONLY | O_NONBLOCK);
    if (fd < 0)
        die("open");

    epoll_add(epoll_fd, fd, EPOLLIN);

    printf("... by fifo:   echo 1 > %s\n", fifo_fn);
    fflush(stdout);
    return fd;
}

void fifo_handle(int epoll_fd, int fifo_fd, int events) {
    static char buffer[4096];
    ssize_t bytes;

    /* Preamble */
    printf("%s: ", fifo_fn);
    fflush(stdout);

    if (events & EPOLLIN) {
        bytes = read(fifo_fd, buffer, ARRAY_SIZE(buffer));
        if (bytes < 0)
            die("read");

        ssize_t acc = 0;
        while (acc < bytes) {
            ssize_t ret = write(STDOUT_FILENO, buffer, bytes-acc);
            if (ret < 0)
                die("write");

            acc += ret;
        }
    }

    if (events & EPOLLHUP && !bytes) {
        epoll_del(epoll_fd, fifo_fd);
        printf("[closed]\n");
        fflush(stdout);
    }

}
