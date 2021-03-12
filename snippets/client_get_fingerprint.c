/*
 * Copyright (c) 2016 Thomas Pornin <pornin@bolet.org>
 *
 * Permission is hereby granted, free of charge, to any person obtaining 
 * a copy of this software and associated documentation files (the
 * "Software"), to deal in the Software without restriction, including
 * without limitation the rights to use, copy, modify, merge, publish,
 * distribute, sublicense, and/or sell copies of the Software, and to
 * permit persons to whom the Software is furnished to do so, subject to
 * the following conditions:
 *
 * The above copyright notice and this permission notice shall be 
 * included in all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, 
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND 
 * NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS
 * BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN
 * ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
 * CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <errno.h>
#include <signal.h>

#include <sys/types.h>
#include <sys/socket.h>
#include <netdb.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>

#include "bearssl.h"


/*
 * Connect to the specified host and port. The connected socket is
 * returned, or -1 on error.
 */
static int
host_connect(const char *host, const char *port)
{
        struct addrinfo hints, *si, *p;
        int fd;
        int err;

        memset(&hints, 0, sizeof hints);
        hints.ai_family = PF_UNSPEC;
        hints.ai_socktype = SOCK_STREAM;
        err = getaddrinfo(host, port, &hints, &si);
        if (err != 0) {
                fprintf(stderr, "ERROR: getaddrinfo(): %s\n",
                        gai_strerror(err));
                return -1;
        }
        fd = -1;
        for (p = si; p != NULL; p = p->ai_next) {
                struct sockaddr *sa;
                void *addr;
                char tmp[INET6_ADDRSTRLEN + 50];

                sa = (struct sockaddr *)p->ai_addr;
                if (sa->sa_family == AF_INET) {
                        addr = &((struct sockaddr_in *)sa)->sin_addr;
                } else if (sa->sa_family == AF_INET6) {
                        addr = &((struct sockaddr_in6 *)sa)->sin6_addr;
                } else {
                        addr = NULL;
                }
                if (addr != NULL) {
                        inet_ntop(p->ai_family, addr, tmp, sizeof tmp);
                } else {
                        sprintf(tmp, "<unknown family: %d>",
                                (int)sa->sa_family);
                }
                fprintf(stderr, "connecting to: %s\n", tmp);
                fd = socket(p->ai_family, p->ai_socktype, p->ai_protocol);
                if (fd < 0) {
                        perror("socket()");
                        continue;
                }
                if (connect(fd, p->ai_addr, p->ai_addrlen) < 0) {
                        perror("connect()");
                        close(fd);
                        continue;
                }
                break;
        }
        if (p == NULL) {
                freeaddrinfo(si);
                fprintf(stderr, "ERROR: failed to connect\n");
                return -1;
        }
        freeaddrinfo(si);
        fprintf(stderr, "connected.\n");
        return fd;
}

/*
 * Low-level data read callback for the simplified SSL I/O API.
 */
static int
sock_read(void *ctx, unsigned char *buf, size_t len)
{
        for (;;) {
                ssize_t rlen;

                rlen = read(*(int *)ctx, buf, len);
                if (rlen <= 0) {
                        if (rlen < 0 && errno == EINTR) {
                                continue;
                        }
                        return -1;
                }
                return (int)rlen;
        }
}

/*
 * Low-level data write callback for the simplified SSL I/O API.
 */
static int
sock_write(void *ctx, const unsigned char *buf, size_t len)
{
        for (;;) {
                ssize_t wlen;

                wlen = write(*(int *)ctx, buf, len);
                if (wlen <= 0) {
                        if (wlen < 0 && errno == EINTR) {
                                continue;
                        }
                        return -1;
                }
                return (int)wlen;
        }
}

struct ex_context {
        br_x509_minimal_context orig_ctx;

        br_x509_class orig_vtable;
        br_x509_class new_vtable;
        br_sha1_context sha1_cert;
        bool done_cert = false;
};

static void ex_start_chain(const br_x509_class **ctx, const char *server_name) {
        ex_context *xc = (ex_context *)ctx;
        br_sha1_init(&xc->sha1_cert);
        xc->orig_vtable.start_chain(ctx, server_name);
}


static void ex_append(const br_x509_class **ctx, const unsigned char *buf, size_t len) {
        ex_context *xc = (ex_context *)ctx;
        if (!xc->done_cert) {
                br_sha1_update(&xc->sha1_cert, buf, len);
        }
        xc->orig_vtable.append(ctx, buf, len);
}

static void ex_end_cert(const br_x509_class **ctx) {
    ex_context *xc = (ex_context *)ctx;
    xc->done_cert = true;

    uint8_t res[20];
    memset(res, 0, sizeof(res));
    br_sha1_out(&xc->sha1_cert, res);

    for (int i = 0; i < 20; ++i) {
            printf("%x ", res[i]);
    }
    printf("\n");


    xc->orig_vtable.end_cert(ctx);
  }


/*
 * Main program: this is a simple program that expects 2 or 3 arguments.
 * The first two arguments are a hostname and a port; the program will
 * open a SSL connection with that server and port. It will then send
 * a simple HTTP GET request, using the third argument as target path
 * ("/" is used as path if no third argument was provided). The HTTP
 * response, complete with header and contents, is received and written
 * on stdout.
 */
int
main(int argc, char *argv[])
{
        const char *host, *port, *path;
        int fd;
        
        br_ssl_client_context sc;
        ex_context xc;
        unsigned char iobuf[BR_SSL_BUFSIZE_BIDI];
        br_sslio_context ioc;

        if (argc < 3 || argc > 4) {
                return EXIT_FAILURE;
        }
        host = argv[1];
        port = argv[2];
        if (argc == 4) {
                path = argv[3];
        } else {
                path = "/";
        }

        /*
         * Ignore SIGPIPE to avoid crashing in case of abrupt socket close.
         */
        signal(SIGPIPE, SIG_IGN);

        /*
         * Open the socket to the target server.
         */
        fd = host_connect(host, port);
        if (fd < 0) {
                return EXIT_FAILURE;
        }

        br_ssl_client_init_full(&sc, &xc.orig_ctx, 0, 0);

        xc.orig_vtable = *xc.orig_ctx.vtable;
        xc.new_vtable = *xc.orig_ctx.vtable;
        xc.orig_ctx.vtable = &xc.new_vtable;
        xc.new_vtable.start_chain = ex_start_chain;
        xc.new_vtable.append = ex_append;
        xc.new_vtable.end_cert = ex_end_cert;



        br_ssl_engine_set_buffer(&sc.eng, iobuf, sizeof iobuf, 1);

        /*
         * Reset the client context, for a new handshake. We provide the
         * target host name: it will be used for the SNI extension. The
         * last parameter is 0: we are not trying to resume a session.
         */
        br_ssl_client_reset(&sc, host, 0);

        /*
         * Initialise the simplified I/O wrapper context, to use our
         * SSL client context, and the two callbacks for socket I/O.
         */
        br_sslio_init(&ioc, &sc.eng, sock_read, &fd, sock_write, &fd);

        br_sslio_flush(&ioc);

        int err = br_ssl_engine_last_error(&sc.eng);

        br_sslio_flush(&ioc);

        close(fd);

        return 0;
}
