int signalfd_prepare(int epoll_fd) {
    int ret;
    sigset_t sigset;

    sigemptyset(&sigset);
    sigaddset(&sigset, SIGUSR1);
    sigaddset(&sigset, SIGUSR2);

    ret = sigprocmask(SIG_BLOCK, &sigset, NULL);
    if (ret < 0)
        die("sigprocmask");

    ret = signalfd(-1, &sigset, SFD_NONBLOCK);
    if (ret < 0)
        die("signalfd");

    epoll_add(epoll_fd, ret, EPOLLIN);

    printf("... by signal: /bin/kill -USR1 -q 3 %d \n", getpid());
    return ret;
}

void signalfd_handle(int epoll_fd, int signal_fd, int events) {
    struct signalfd_siginfo siginfo;
    ssize_t bytes;

    while (events & EPOLLIN) {
        bytes = read(signal_fd, &siginfo, sizeof(siginfo));
        if (bytes < 0) {
            if (errno == EAGAIN)
                break;
            die("read");
        }

        if (!bytes && events & EPOLLHUP) {
            epoll_del(epoll_fd, signal_fd);
            printf("signalfd: [closed]\n");
            close(signal_fd);
            break;
        }

        printf("signalfd[uid=%u,pid=%u] (%s): %d\n", siginfo.ssi_uid, siginfo.ssi_pid,
               strsignal(siginfo.ssi_signo), siginfo.ssi_int);
    }
}
