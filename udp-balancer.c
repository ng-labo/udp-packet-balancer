/**
 udp pakcet balancer
 */
#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <stdlib.h>
#include <time.h>
#include "udp-balancer.h"

extern int raw_send_from_to(int s, const void* msg, size_t msglen, struct sockaddr* saddr_generic, struct sockaddr* daddr_generic, int ttl, int flags);
extern int make_raw_udp_socket(size_t socketbuflen, int af);

static int newbranchindex_naive(struct variables*, int);

void usage() {
    fprintf(stderr, "udp-balancer [-s] accpter-ipaddress:port branch-1-ipaddress:port branch-2... \n");
    exit(0);
}

void error(const char *msg) {
    perror(msg);
    exit(1);
}

void setzero_sockaddr_in(struct sockaddr_in* c) {
    memset(c, sizeof(struct sockaddr_in) , 0);
}

void set_sockaddr_in(const char* hostname, unsigned short portno, struct sockaddr_in* sa) {
    struct hostent *server = gethostbyname(hostname);
    if (server == NULL) {
        fprintf(stderr,"ERROR, no such host as %s\n", hostname);
        exit(1);
    }
    setzero_sockaddr_in(sa);
    sa->sin_family = AF_INET;
    memcpy((char*) &(sa->sin_addr.s_addr), (char*) server->h_addr, server->h_length);
    sa->sin_port = htons(portno);
}

int branchsocket(int spoof) {
    int sockfd = -1;
    if (spoof) {
        sockfd = make_raw_udp_socket(-1, AF_INET);
        if (sockfd < 0) error("raw socket");
    } else {
        sockfd = socket(AF_INET, SOCK_DGRAM, 0);
        if (sockfd < 0) error("socket");
    }
    return sockfd;
}

void parse_host_port(const char* arg, char* host, unsigned short* port) {
    if (strlen(arg)>64) {
        error("weird length of argument");
    }
    // now ipv4 only

    char bufhost[64], bufport[64];
    const char *a = arg;
    char *h = bufhost;
    char *p = bufport;

    int notfound = 1;
    *h = '\0';;
    *p = '\0';;
    while (*a != '\0') {
        if (*a == ':') {
            notfound = 0;
            a++;
            continue;
        }
        if (notfound == 1) {
            *h++ = *a;
            *h = '\0';
        } else {
            *p++ = *a;
            *p = '\0';
        }
        a++;
    }
    if (notfound==1) {
        error("couldnt read ip-address:port");
    }
    if (strlen(bufhost)==0) {
    }
    strcpy(host, bufhost);
    *port = (unsigned short) atoi(bufport);
}

void initialize(int ac, char* av[], struct variables* ctx) {
    memset(ctx, 0, sizeof(struct variables));
    av++;

    for (;*av != NULL && **av == '-'; av++) {
        if (strcmp(*av, "-s")==0) ctx->spoof = 1;
    }

    ctx->newbranchindex = &newbranchindex_naive;

    if (*av == NULL) usage();

    parse_host_port((const char*) *av, ctx->selfhost, &(ctx->selfport));

    av++;
    ctx->branchnum = 0;
    for (int i = 0; *av != NULL; ctx->branchnum++, av++, i++) {
        char h[128];
        unsigned short p;
        parse_host_port((const char*) *av, h, &p);
        set_sockaddr_in(h, p, &(ctx->branch_s_addr[i]));
        strcpy(ctx->branch_hostargs[i], *av);
    }

    ctx->socketbuflen = SOCKETBUFLEN;
    ctx->sockfd = -1;
    // setzero_sockaddr_in(&(ctx->selfaddr));
    for (int i = 0; i < CONNUM; i++) {
        ctx->branchfd[i] = -1;
        // setzero_sockaddr_in(&(ctx->caddr[i]));
        // setzero_sockaddr_in(&(ctx->branchaddr[i]));
    }
}

int init_acceptor(struct variables *ctx) {
    int optval = 1;
    ctx->sockfd = socket(AF_INET, SOCK_DGRAM, 0);
    if (ctx->sockfd < 0) error("socket");
    setsockopt(ctx->sockfd, SOL_SOCKET, SO_REUSEADDR, (const void *) &optval, sizeof(int));

	ctx->selfaddr.sin_family = AF_INET;
	ctx->selfaddr.sin_addr.s_addr = htonl(INADDR_ANY);
	ctx->selfaddr.sin_port = htons((unsigned short) ctx->selfport);

	if (bind(ctx->sockfd, (struct sockaddr*) &ctx->selfaddr, sizeof(ctx->selfaddr)) < 0)
        error("bind");

    return 0;
}

static inline int nfds(struct variables *ctx) {
    int ret = -1;
    for (int i = 0; i < CONNUM; i++) {
        if (ctx->branchfd[i] >= 0 && ctx->branchfd[i] > ret) {
            ret = ctx->branchfd[i];
        }
    }
    if (ctx->sockfd > ret) {
        ret = ctx->sockfd;
    }
    return (ret + 1);
}

static inline int newbranchindex_naive(struct variables* ctx, int connindex) {
    return connindex % ctx->branchnum;
}

static inline int getbranchindex(struct sockaddr_in* t, struct variables* ctx) {
    time_t now = time(NULL);
    for (int i = 0; i < CONNUM; i++) {
        if(ctx->caddr[i].sin_port == t->sin_port &&
           ctx->caddr[i].sin_addr.s_addr == t->sin_addr.s_addr) {
            ctx->lasttscon[i] = now;
            return i;
        }
    }
    time_t l = now - 60;
    for (int i = 0; i < CONNUM; i++) {
        if (ctx->lasttscon[i] > 0 && ctx->branchfd[i] > -1 && ctx->lasttscon[i] < l) {
            close(ctx->branchfd[i]);
            ctx->branchfd[i] = -1;
            ctx->lasttscon[i] = 0;
            setzero_sockaddr_in(&(ctx->caddr[i]));
            printf("cleanup index=%d\n", i);
            ctx->activecount[ctx->branchindexinconn[i]]--;
        }
    }
    for (int i = 0; i < CONNUM; i++) {
        if (ctx->branchfd[i] == -1) {
            ctx->branchfd[i] = branchsocket(ctx->spoof);
            int newindex = ctx->newbranchindex(ctx, i);
            ctx->branchaddr[i] = ctx->branch_s_addr[newindex];
            ctx->caddr[i] = *t;

            ctx->branchindexinconn[i] = newindex;
            ctx->activecount[newindex]++;
#if DEBUG
            printf("branchfd[%d]=%d\n", i, ctx->branchfd[i]);
#endif
            return i;
        }
    }
    return -1;
}

void process(struct variables* ctx) {
    char buffer[PACKETBUFLEN];
    fd_set rset;
    for (;;) {
        FD_ZERO(&rset);
        FD_SET(ctx->sockfd, &rset);
        for (int i = 0; i < CONNUM; i++) {
            if (ctx->branchfd[i] >= 0) FD_SET(ctx->branchfd[i], &rset);
        }

        if (select(nfds(ctx), &rset, NULL, NULL, NULL) < 0) {
            error("select");
        }

        for (int i = 0; i < CONNUM; i++) {
            // socket corresponding for branch-i
            int fd = ctx->branchfd[i];
            if (fd < 0) continue;
            if (!FD_ISSET(fd, &rset)) continue;
            // this is a signaled fd.
            struct sockaddr_in c;
            int sz = sizeof(c);
            // receive.
            int len = recvfrom(fd, buffer, sizeof(buffer), 0, (struct sockaddr*) &c, &sz);
            if (len < 0) {
#if DEBUG
                printf("failed to recvfrom from boss\n");
#endif
                ctx->error_recvfrom++;
            }
            // relay to client i.
            if (sendto(ctx->sockfd, (const char*) buffer, len, 0,
                       (struct sockaddr*) &(ctx->caddr[i]), sizeof(ctx->caddr[i])) < 0) {
#if DEBUG
                printf("failed i=%d, fd=%d\n", i, fd);
#endif
                ctx->error_sendto++;
            }
        }

        if (FD_ISSET(ctx->sockfd, &rset)) {
            struct sockaddr_in t;
            int sz = sizeof(t);
            int len = recvfrom(ctx->sockfd, buffer, sizeof(buffer), 0, (struct sockaddr*) &t, &sz);
            if (len < 0) {
#if DEBUG
                printf("failed recvfrom\n");
#endif
                ctx->error_recvfrom++;
            }
#if DEBUG
            printf("Message from UDP client: len=%d (S_addr=%d port=%d)\n", len, t.sin_addr.s_addr,t.sin_port);
#endif

            int branchidx = getbranchindex(&t, ctx);
#if DEBUG
            printf("getbranchindex=%d\n", branchidx);
#endif
            if (branchidx < 0) {
#if DEBUG
                printf("failed get branch-index corresponding to client\n");
#endif
                ctx->failed_assign++;
			} else if (ctx->spoof) {
                if (raw_send_from_to(ctx->branchfd[branchidx], buffer, len,
                                    (struct sockaddr*) &t,
                                    (struct sockaddr*) &(ctx->branchaddr[branchidx]), 63, 1)<0) {
#if DEBUG
                    printf("failed to send raw packet to boss\n");
#endif
                    ctx->error_sendto++;
}
            } else if (sendto(ctx->branchfd[branchidx], buffer, len, 0,
                              (struct sockaddr*) &(ctx->branchaddr[branchidx]),
                              sizeof(ctx->branchaddr[branchidx])) < 0) {
#if DEBUG
                    printf("failed to send to boss\n");
#endif
                    ctx->error_sendto++;
                }
        }
    }
}

int main(int ac, char** av) {
    struct variables ctx;

    if (ac <3) usage(); // and exit

    initialize(ac, av, &ctx);
    printf("accepting port=%d %s\n", ctx.selfport, ctx.spoof ? "with nat" : "");
    for (int i=0; i < ctx.branchnum; i++) {
        printf("branch[%d]=%s\n", i, ctx.branch_hostargs[i]);
    }

    init_acceptor(&ctx);

    process(&ctx);

    return 0;
}
