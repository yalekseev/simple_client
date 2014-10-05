#include <errno.h>
#include <fcntl.h>
#include <netdb.h>
#include <stdio.h>
#include <getopt.h>
#include <signal.h>
#include <stdlib.h>
#include <string.h>
#include <syslog.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <sys/types.h>
#include <sys/socket.h>

enum { MAXLINE = 1024 };

#define MAX(a,b) ((a)>(b)?(a):(b))

void process_requests(int in, int sd, int out) {
    ssize_t n, nwritten;
    char to[MAXLINE], fr[MAXLINE];
    char *toiptr, *tooptr, *friptr, *froptr;

    {
        int val = fcntl(sd, F_GETFL, 0);
        fcntl(sd, F_SETFL, val | O_NONBLOCK);

        val = fcntl(in, F_GETFL, 0);
        fcntl(in, F_SETFL, val | O_NONBLOCK);

        val = fcntl(out, F_GETFL, 0);
        fcntl(out, F_SETFL, val | O_NONBLOCK);
    }

    toiptr = tooptr = to;
    friptr = froptr = fr;
    int stdineof = 0;

    int maxfd = MAX(MAX(in, out), sd) + 1;
    while (1) {
        fd_set rset, wset;
        FD_ZERO(&rset);
        FD_ZERO(&wset);

        if (stdineof == 0 && toiptr < &to[MAXLINE]) {
            FD_SET(in, &rset);
        }

        if (friptr < &fr[MAXLINE]) {
            FD_SET(sd, &rset);
        }

        if (tooptr != toiptr) {
            FD_SET(sd, &wset);
        }

        if (froptr != friptr) {
            FD_SET(out, &wset);
        }

        select(maxfd, &rset, &wset, NULL, NULL);

        // read from input
        if (FD_ISSET(in, &rset)) {
            if ((n = read(in, toiptr, &to[MAXLINE] - toiptr)) < 0) {
                if (errno != EWOULDBLOCK) {
                    // TODO:
                }
            } else if (n == 0) {
                fprintf(stderr, "EOF on stdin\n");

                stdineof = 1;
                if (tooptr == toiptr) {
                    shutdown(sd, SHUT_WR);
                }
            } else {
                fprintf(stderr, "read %d bytes from stdin\n", n);

                toiptr += n;
                FD_SET(sd, &wset);
            }
        }

        // read from socket
        if (FD_ISSET(sd, &rset)) {
            if ((n = read(sd, friptr, &fr[MAXLINE] - friptr)) < 0) {
                if (errno != EWOULDBLOCK) {
                    // TODO:
                }
            } else if (n == 0) {
                fprintf(stderr, "EOF on socket\n");
                if (stdineof) {
                    return;
                } else {
                    // TODO:
                }
            } else {
                fprintf(stderr, "read %d bytes from socket\n", n);
                friptr += n;
                FD_SET(out, &wset);
            }
        }

        // write to output
        if (FD_ISSET(out, &wset) && ((n = friptr - froptr) > 0)) {
            if ((nwritten = write(out, froptr, n)) < 0) {
                if (errno != EWOULDBLOCK) {
                    // TODO:
                }
            } else {
                fprintf(stderr, "wrote %d bytes to stdout\n", nwritten);
                froptr += nwritten;
                if (froptr == friptr) {
                    froptr = friptr = fr;
                }
            }
        }

        // write to socket
        if (FD_ISSET(sd, &wset) && ((n = toiptr - tooptr) > 0)) {
            if ((nwritten = write(sd, tooptr, n)) < 0) {
                if (errno != EWOULDBLOCK) {
                    // TODO:
                }
            } else {
                fprintf(stderr, "wrote %d bytes to socket\n", nwritten);
                tooptr += nwritten;
                if (tooptr == toiptr) {
                    toiptr = tooptr = to;
                    if (stdineof) {
                        shutdown(sd, SHUT_WR);
                    }
                }
            }
        }
    }
}

int create_socket(int socktype, const char *host, const char *port) {
    struct addrinfo hints, *result;
    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = socktype;
    int ret = getaddrinfo(host, port, &hints, &result);
    if (ret != 0) {
        fprintf(stderr, "getaddrinfo %s", gai_strerror(ret));
        return -1;
    }

    int fd;
    struct addrinfo *p;
    for (p = result; p != NULL; p = p->ai_next) {
        fd = socket(p->ai_family, p->ai_socktype, p->ai_protocol);
        if (-1 == fd) {
            fprintf(stderr, "socket %s\n", strerror(errno));
            continue;
        }

        if (-1 == connect(fd, p->ai_addr, p->ai_addrlen)) {
            fprintf(stderr, "connect %s\n", strerror(errno));
            continue;
        }
    }

    freeaddrinfo(result);

    if (NULL == p) {
        fprintf(stderr, "Failed to connect to host\n");
        return -1;
    }

    return fd;
}

int main(int argc, char *argv[]) {
    int socktype = SOCK_STREAM;
    char *host = "localhost";
    char *port = "50000";

    int opt;
    while ((opt = getopt(argc, argv, "tuh:p:")) != -1) {
        switch (opt) {
        case 't':
            socktype = SOCK_STREAM;
            break;
        case 'u':
            socktype = SOCK_DGRAM;
            break;
        case 'h':
            host = optarg;
            break;
        case 'p':
            port = optarg;
            break;
        case '?':
            fprintf(stderr, "Unknown option: %c\n", optopt);
            exit(EXIT_FAILURE);
        default:
            fprintf(stderr, "Failed to parse command line arguments\n");
            exit(EXIT_FAILURE);
        }
    }

    if (optind != argc) {
        fprintf(stderr, "Some unexpected command line arguments are given\n");
        exit(EXIT_FAILURE);
    }

    int sockfd = create_socket(socktype, host, port);

    process_requests(STDIN_FILENO, sockfd, STDOUT_FILENO);

    return 0;
}
