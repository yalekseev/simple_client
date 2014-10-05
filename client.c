#include "tasks.h"
#include "server.h"

#include <errno.h>
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

void process_requests(FILE *in, int sd, FILE *out) {
    int         maxfdp1, val, stdineof;
    ssize_t     n, nwritten;
    fd_set      rset, wset;
    char        to[MAXLINE], fr[MAXLINE];
    char        *toiptr, *tooptr, *friptr, *froptr;

    val = Fcntl(sockfd, F_GETFL, 0);
    Fcntl(sockfd, F_SETFL, val | O_NONBLOCK);

    val = Fcntl(STDIN_FILENO, F_GETFL, 0);
    Fcntl(STDIN_FILENO, F_SETFL, val | O_NONBLOCK);

    val = Fcntl(STDOUT_FILENO, F_GETFL, 0);
    Fcntl(STDOUT_FILENO, F_SETFL, val | O_NONBLOCK);

    toiptr = tooptr = to;   /* initialize buffer pointers */
    friptr = froptr = fr;
    stdineof = 0;

    maxfdp1 = max(max(STDIN_FILENO, STDOUT_FILENO), sockfd) + 1;
    for ( ; ; ) {
        FD_ZERO(&rset);
        FD_ZERO(&wset);
        if (stdineof == 0 && toiptr < &to[MAXLINE])
            FD_SET(STDIN_FILENO, &rset);    /* read from stdin */
        if (friptr < &fr[MAXLINE])
            FD_SET(sockfd, &rset);          /* read from socket */
        if (tooptr != toiptr)
            FD_SET(sockfd, &wset);          /* data to write to socket */
        if (froptr != friptr)
            FD_SET(STDOUT_FILENO, &wset);   /* data to write to stdout */

        Select(maxfdp1, &rset, &wset, NULL, NULL);
/* end nonb1 */
/* include nonb2 */
        if (FD_ISSET(STDIN_FILENO, &rset)) {
            if ( (n = read(STDIN_FILENO, toiptr, &to[MAXLINE] - toiptr)) < 0) {
                if (errno != EWOULDBLOCK)
                    err_sys("read error on stdin");

            } else if (n == 0) {
#ifdef  VOL2
                fprintf(stderr, "%s: EOF on stdin\n", gf_time());
#endif
                stdineof = 1;           /* all done with stdin */
                if (tooptr == toiptr)
                    Shutdown(sockfd, SHUT_WR);/* send FIN */

            } else {
#ifdef  VOL2
                fprintf(stderr, "%s: read %d bytes from stdin\n", gf_time(), n);
#endif
                toiptr += n;            /* # just read */
                FD_SET(sockfd, &wset);  /* try and write to socket below */
            }
        }

        if (FD_ISSET(sockfd, &rset)) {
            if ( (n = read(sockfd, friptr, &fr[MAXLINE] - friptr)) < 0) {
                if (errno != EWOULDBLOCK)
                    err_sys("read error on socket");

            } else if (n == 0) {
#ifdef  VOL2
                fprintf(stderr, "%s: EOF on socket\n", gf_time());
#endif
                if (stdineof)
                    return;     /* normal termination */
                else
                    err_quit("str_cli: server terminated prematurely");

            } else {
#ifdef  VOL2
                fprintf(stderr, "%s: read %d bytes from socket\n",
                                gf_time(), n);
#endif
                friptr += n;        /* # just read */
                FD_SET(STDOUT_FILENO, &wset);   /* try and write below */
            }
        }
/* end nonb2 */
/* include nonb3 */
        if (FD_ISSET(STDOUT_FILENO, &wset) && ( (n = friptr - froptr) > 0)) {
            if ( (nwritten = write(STDOUT_FILENO, froptr, n)) < 0) {
                if (errno != EWOULDBLOCK)
                    err_sys("write error to stdout");

            } else {
#ifdef  VOL2
                fprintf(stderr, "%s: wrote %d bytes to stdout\n",
                                gf_time(), nwritten);
#endif
                froptr += nwritten;     /* # just written */
                if (froptr == friptr)
                    froptr = friptr = fr;   /* back to beginning of buffer */
            }
        }

        if (FD_ISSET(sockfd, &wset) && ( (n = toiptr - tooptr) > 0)) {
            if ( (nwritten = write(sockfd, tooptr, n)) < 0) {
                if (errno != EWOULDBLOCK)
                    err_sys("write error to socket");

            } else {
#ifdef  VOL2
                fprintf(stderr, "%s: wrote %d bytes to socket\n",
                                gf_time(), nwritten);
#endif
                tooptr += nwritten; /* # just written */
                if (tooptr == toiptr) {
                    toiptr = tooptr = to;   /* back to beginning of buffer */
                    if (stdineof)
                        Shutdown(sockfd, SHUT_WR);  /* send FIN */
                }
            }
        }
    }
}


int create_service_socket(int socktype, const char *service) {
    struct addrinfo hints, *result;
    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_UNSPEC;     /* Allow IPv4 or IPv6 */
    hints.ai_socktype = socktype;
    hints.ai_flags = AI_PASSIVE;     /* For wildcard IP address */
    hints.ai_protocol = 0;           /* Any protocol */
    hints.ai_canonname = NULL;
    hints.ai_addr = NULL;
    hints.ai_next = NULL;
    int ret = getaddrinfo(NULL, service, &hints, &result);
    if (ret != 0) {
        syslog(LOG_EMERG, "getaddrinfo %s", gai_strerror(ret));
        return -1;
    }

    int fd;
    struct addrinfo *p;
    for (p = result; p != NULL; p = p->ai_next) {
        fd = socket(p->ai_family, p->ai_socktype, p->ai_protocol);
        if (-1 == fd) {
            syslog(LOG_INFO, "socket %s", strerror(errno));
            continue;
        }

        if (socktype == SOCK_STREAM) {
            int optval = 1;
            if (setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &optval, sizeof(optval)) == -1) {
                syslog(LOG_INFO, "setsockopt %s", strerror(errno));
                close(fd);
                continue;
            }
        }

        if (bind(fd, p->ai_addr, p->ai_addrlen) == -1) {
            syslog(LOG_INFO, "bind %s", strerror(errno));
            close(fd);
            continue;
        }

        if (socktype == SOCK_STREAM) {
            int backlog = BACKLOG;
            if (getenv("LISTENQ") != NULL) {
                backlog = atoi(getenv("LISTENQ"));
            }

            if (listen(fd, backlog) == -1) {
                syslog(LOG_INFO, "listen %s", strerror(errno));
                close(fd);
                continue;
            }
        }

        break;
    }

    freeaddrinfo(result);

    if (NULL == p) {
        syslog(LOG_EMERG, "Failed to bind to any addresses");
        return -1;
    }

    return fd;
}

int main(int argc, char *argv[]) {
    int opt;
    while ((opt = getopt(argc, argv, "tuh:p:")) != -1) {
        switch (opt) {
        case 't':
        case 'u':
        case 'h':
        case 'p':
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

    process_requests(stdin, sd, stdout);

    return 0;
}
