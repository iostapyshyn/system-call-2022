int mqueue_prepare(int epoll_fd) {
    int ret;

    struct mq_attr attr;
    attr.mq_maxmsg = 10;
    attr.mq_msgsize = 128;

    ret = mq_open("/postbox", O_RDONLY|O_CREAT, 0666, &attr);
    if (ret < 0)
        die("mq_open");

    epoll_add(epoll_fd, ret, EPOLLIN);

    printf("... by mq_send: ./mq_send 4 (see also `cat /dev/mqueue/postbox`)\n");
    return ret;
}

void mqueue_handle(int epoll_fd, int msg_fd, int events) {
    static char buffer[128];
    unsigned int prio;
    ssize_t bytes = 0;

    if (events & EPOLLIN) {
        bytes = mq_receive(msg_fd, buffer, ARRAY_SIZE(buffer), &prio);
        if (bytes < 0) {
            if (errno == EAGAIN)
                return;
            die("read");
        }

        printf("mqueue[prio=%u]: ", prio);
        fflush(stdout);

        ssize_t acc = 0;
        while (acc < bytes) {
            ssize_t ret = write(STDOUT_FILENO, buffer, bytes-acc);
            if (ret < 0)
                die("write");

            acc += ret;
        }

        printf("\n");
    }

    if (events & EPOLLHUP && !bytes) {
        epoll_del(epoll_fd, msg_fd);
        mq_close(msg_fd);
        printf("mqueue: [closed]\n");
        /* Already closed */
        fflush(stdout);
    }
}
