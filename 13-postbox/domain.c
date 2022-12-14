static char *socket_fn = "socket";

int domain_prepare(int epoll_fd) {
    int ret, fd;

    ret = unlink(socket_fn);
    if (ret && errno != ENOENT)
        die("unlink");

    fd = socket(AF_UNIX, SOCK_STREAM, 0);
    if (fd < 0)
        die("socket");

    struct sockaddr_un addr = {
        .sun_family = AF_UNIX,
    };
    strcpy(addr.sun_path, socket_fn);

    ret = bind(fd, &addr, sizeof(addr));
    if (ret < 0)
        die("bind");

    ret = listen(fd, 12);
    if (ret < 0)
        die("listen");

    epoll_add(epoll_fd, fd, EPOLLIN);

    printf("... by socket: echo 2 | nc -U %s\n", socket_fn);
    return fd;
}


void domain_accept(int epoll_fd, int sock_fd, int events) {
    int fd;
    (void) events;

    fd = accept(sock_fd, NULL, NULL);
    if (fd < 0)
        die("accept");

    epoll_add(epoll_fd, fd, EPOLLIN);
}


void domain_recv(int epoll_fd, int sock_fd, int events) {
    static char buffer[4096];
    ssize_t bytes;

    struct ucred ucred;
    socklen_t socklen = sizeof(ucred);
    int rc = getsockopt(sock_fd, SOL_SOCKET, SO_PEERCRED, &ucred, &socklen);
    if (rc < 0)
        die("getsockopt");

    /* Preamble */
    printf("%s[pid=%d,uid=%d,gid=%d]: ", socket_fn, ucred.pid, ucred.uid, ucred.gid);
    fflush(stdout);

    if (events & EPOLLIN) {
        bytes = read(sock_fd, buffer, ARRAY_SIZE(buffer));
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
        epoll_del(epoll_fd, sock_fd);
        printf("[closed]\n");
        fflush(stdout);
    }
}
