#include <stdio.h>
#include <unistd.h>
#include <time.h>
#include <stdlib.h>
#include <string.h>
#include <netdb.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/timerfd.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include "../src/liburing.h"

#define BUFSIZE 1024

/*
 * error - wrapper for perror
 */
void error(char *msg)
{
    perror(msg);
    exit(1);
}

int main(int argc, char **argv)
{
    int fd, port;
    struct sockaddr_in saddr;
    int optval; /* flag value for setsockopt */
    int ret, n;
    int tfd;

    if (argc != 2) {
        fprintf(stderr, "usage: %s <port>\n", argv[0]);
        exit(1);
    }
    port = atoi(argv[1]);

    fd = socket(AF_INET, SOCK_STREAM | SOCK_NONBLOCK, 0);
    if (fd < 0)
        error("ERROR opening socket");

    optval = 1;
    setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, (const void *)&optval, sizeof(int));

    saddr = (struct sockaddr_in){
        .sin_family = AF_INET,
        .sin_addr =
            {
                .s_addr = htonl(INADDR_ANY),
            },
        .sin_port = htons((unsigned short)port),
    };

    if (bind(fd, (struct sockaddr *)&saddr, sizeof(saddr)) < 0)
        error("ERROR on binding");

    if (listen(fd, 5) < 0)
        error("ERROR on listen");

    struct itimerspec exp = {};
    exp.it_value.tv_sec = 1;
    exp.it_interval.tv_sec = 1;
    tfd = timerfd_create(CLOCK_MONOTONIC, TFD_NONBLOCK);
    if (tfd < 0)
        error("timerfd_create");

    struct io_uring ring;
    ret = io_uring_queue_init(64, &ring, 0);
    if (ret < 0)
        error("io_uring_queue_init");

    struct io_uring_sqe *sqe, *sqe_tfd;
    struct io_uring_cqe *cqe;
    do {

	    sqe = io_uring_get_sqe(&ring);
	    if (sqe == NULL)
		    error("io_uring_get_sqe");
	    sqe_tfd = io_uring_get_sqe(&ring);
	    if (sqe_tfd == NULL)
		    error("io_uring_get_sqe");

	    if (timerfd_settime(tfd, 0, &exp, NULL) == -1)
		    error("timerfd_settime");

	    io_uring_prep_poll_add(sqe, fd, POLL_IN);
	    io_uring_prep_poll_add(sqe_tfd, tfd, POLL_IN);
	    sqe->user_data = (uint64_t)&fd;
	    sqe_tfd->user_data = (uint64_t)&tfd;

	    ret = io_uring_submit(&ring);
	    if (ret < 0)
		    error("io_uring_submit");

	    ret = io_uring_wait_completion(&ring, &cqe);
	    if (ret < 0)
		    error("io_uring_wait_completion");
    } while (cqe->user_data == (uint64_t)&tfd);

    while (1) {
        int c;
        char buf[BUFSIZE];
        c = accept(fd, NULL, 0);
        if (c < 0)
            error("ERROR on accept");

        n = read(c, buf, sizeof(buf));
        if (n < 0)
            error("ERROR reading from socket");
        printf("server received %d bytes: %s", n, buf);

        n = write(c, buf, n);
        if (n < 0)
            error("ERROR writing to socket");

        close(c);
    }
}
