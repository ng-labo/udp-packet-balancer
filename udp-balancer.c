/**
 udp pakcet balancer
 */
#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <stdlib.h>
#include <time.h>
#include <arpa/inet.h>
#include "udp-balancer.h"

extern int raw_send_from_to(int s, const void* msg, size_t msglen, struct sockaddr* saddr, struct sockaddr* daddr, int ttl, int flags);
extern int make_raw_udp_socket(size_t socketbuflen, int af);

static int newbranchindex_naive(struct variables*, int);
static int newbranchindex_leastbranch(struct variables*, int);

#define DEBUG 1
void usage() {
    fprintf(stderr, "udp-balancer [options] accpter-ipaddress:port branch-1-ipaddress:port branch-2... \n");
    fprintf(stderr, "    -v : print information in processing\n");
    fprintf(stderr, "    -l : least connection port in balancing method \n");
    fprintf(stderr, "    -s : spoofing mode(source address nat)\n");
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
    char *n = bufhost;

    bufhost[0] = '\0';
    bufport[0] = '\0';
    char w = ':';
    if (*a == '[') {
        w = ']';
        a++;
    }
    while (*a != '\0') {
        if (*a == w) {
            if (w == ']') {
                if (*(a + 1) != ':') break;
                a++;
            }
            n = bufport;
            a++;
            continue;
        }
        if (n != NULL) {
            *n++ = *a;
            *n = '\0';
        }
        a++;
    }

    strcpy(host, bufhost);
    *port = (unsigned short) atoi(bufport);
    if (*port == 0) {
        error("Invalid port number");
    }

}

int ipver(const char* host, unsigned char* buf) {
    int i4 = inet_pton(AF_INET, host, buf);
    int i6 = inet_pton(AF_INET6, host, buf);
    if (i4 == 1) return AF_INET;
    else if (i6 == 1) return AF_INET6;
    /*
    struct hostent *he = gethostbyname(host);
    if (he == NULL) {
        fprintf(stderr,"ERROR, no such host as %s\n", bufhost);
        exit(1);
    }
    */
    return -1;
}

void initialize(int ac, char* av[], struct variables* ctx) {
    memset(ctx, 0, sizeof(struct variables));
    av++;

    ctx->method = rotation;
    ctx->newbranchindex = &newbranchindex_naive;
    for (;*av != NULL && **av == '-'; av++) {
        if (strcmp(*av, "-v")==0) ctx->verbose = 1;
        if (strcmp(*av, "-s")==0) ctx->spoof = 1;
        if (strcmp(*av, "-l")==0) {
            ctx->method = leastconn;
            ctx->newbranchindex = &newbranchindex_leastbranch;
        }
    }

    if (*av == NULL) usage();

    parse_host_port((const char*) *av, ctx->selfhost, &(ctx->selfport));

    av++;
    ctx->branchnum = 0;
    for (int i = 0; *av != NULL && i < BRANCHNUMMAX; ctx->branchnum++, av++, i++) {
        char h[128];
        unsigned short p;
        parse_host_port((const char*) *av, h, &p);
        if (strlen(h) == 0) strcpy(h, "127.0.0.1");
        set_sockaddr_in(h, p, &(ctx->branch[i].s_addr));
        strcpy(ctx->branch[i].hostargs, *av);
    }

    ctx->socketbuflen = SOCKETBUFLEN;
    ctx->sockfd = -1;
    ctx->selfaddr = (struct sockaddr*) &ctx->selfaddr_buf;
    // setzero_sockaddr_in(&(ctx->selfaddr));
    for (int i = 0; i < CONNUM; i++) {
        ctx->client[i].fd = -1;
        // setzero_sockaddr_in(&(ctx->caddr[i]));
        // setzero_sockaddr_in(&(ctx->branchaddr[i]));
    }
}

int init_acceptor(struct variables *ctx) {
    int optval = 1;
    char service[64];
    struct addrinfo hints, *res;
    memset(&hints, 0, sizeof(hints));
    snprintf(service, 64, "%d", ctx->selfport);

    char *node = NULL;
    hints.ai_family = AF_INET6;
    if (strlen(ctx->selfhost) != 0) {
        unsigned char buf[sizeof(struct sockaddr_in6)];
        int af = ipver(ctx->selfhost, buf);
        if (af == AF_INET) {
            node = ctx->selfhost;
            hints.ai_family = AF_INET;
        } else if (af == AF_INET6) {
            node = ctx->selfhost;
            hints.ai_family = AF_INET6;
        } else {
            error("invalid host");
        }
    }
    hints.ai_flags = AI_PASSIVE;
    hints.ai_socktype = SOCK_DGRAM;
    if (getaddrinfo(NULL, service, &hints, &res) != 0) {
        error("getaddrinfo");
    }
#if DEBUG
        printf("res->ai_addrlen=%d\n",res->ai_addrlen);
#endif

    ctx->sockfd = socket(res->ai_family, res->ai_socktype, 0);
    if (ctx->sockfd < 0) error("socket");
    setsockopt(ctx->sockfd, SOL_SOCKET, SO_REUSEADDR, (const void *) &optval, sizeof(int));

    memcpy((void*) ctx->selfaddr, (void*) res->ai_addr, res->ai_addrlen);
    ctx->selfaddrlen = res->ai_addrlen;

    freeaddrinfo(res);

    if (bind(ctx->sockfd, ctx->selfaddr, ctx->selfaddrlen) < 0) error("bind");

    return 0;
}

static inline int nfds(struct variables *ctx) {
    int ret = -1;
    struct client* clientp = ctx->client;
    for (int i = 0; i < CONNUM; i++, clientp++) {
        if (clientp->fd >= 0 && clientp->fd > ret) {
            ret = clientp->fd;
        }
    }
    if (ctx->sockfd > ret) {
        ret = ctx->sockfd;
    }
    return (ret + 1);
}

static inline int newbranchindex_naive(struct variables* ctx, int ignore) {
    return ctx->new_connection % ctx->branchnum;
}

static inline int newbranchindex_leastbranch(struct variables* ctx, int ignore) {
    int cont =ctx->branch[0].activecount;
    int index = 0;
    for (int i = 1; i < ctx->branchnum; i++) {
        if (ctx->branch[i].activecount < cont) {
            cont = ctx->branch[i].activecount;
            index = i;
        }
    }
    return index;
}

static inline int getbranchindex(struct sockaddr_in* t, struct variables* ctx) {
    time_t now = time(NULL);
    for (int i = 0; i < CONNUM; i++) {
        if(ctx->client[i].caddr.sin_port == t->sin_port &&
           ctx->client[i].caddr.sin_addr.s_addr == t->sin_addr.s_addr) {
            ctx->client[i].lasttscon = now;
            return i;
        }
    }
    time_t l = now - FORGETTING_IN_SEC;
    for (int i = 0; i < CONNUM; i++) {
        if (ctx->client[i].lasttscon > 0 && ctx->client[i].fd > -1 && ctx->client[i].lasttscon < l) {
            if (ctx->verbose) printf("close branchfd[%d]=%d\n", i, ctx->client[i].fd);
            close(ctx->client[i].fd);
            ctx->client[i].fd = -1;
            ctx->client[i].lasttscon = 0;
            setzero_sockaddr_in(&(ctx->client[i].caddr));
            ctx->branch[ctx->client[i].branchindexinconn].activecount--;
            // ctx->branchindexinconn[i] = -1
        }
    }
    for (int i = 0; i < CONNUM; i++) {
        if (ctx->client[i].fd == -1) {
            ctx->client[i].fd = branchsocket(ctx->spoof);
            int newindex = ctx->newbranchindex(ctx, i);
            ctx->client[i].branchaddr = ctx->branch[newindex].s_addr;
            ctx->client[i].caddr = *t;

            ctx->client[i].branchindexinconn = newindex;
            ctx->branch[newindex].activecount++;

            if (ctx->verbose) printf("assign branchfd[%d]=%d\n", i, ctx->client[i].fd);

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
            if (ctx->client[i].fd >= 0) FD_SET(ctx->client[i].fd, &rset);
        }

        if (select(nfds(ctx), &rset, NULL, NULL, NULL) < 0) {
            error("select");
        }

        for (int i = 0; i < CONNUM; i++) {
            // socket corresponding for branch-i
            int fd = ctx->client[i].fd;
            if (fd < 0) continue;
            if (!FD_ISSET(fd, &rset)) continue;
            // this is a signaled fd.
            struct sockaddr_in c;
            int sz = sizeof(c);
            // receive.
            int len = recvfrom(fd, buffer, sizeof(buffer), 0, (struct sockaddr*) &c, &sz);
            if (len < 0) {
#if DEBUG
                printf("failed to recvfrom from branch(%d)\n", i);
#endif
                ctx->error_recvfrom++;
            }
            // relay to client i.
            if (sendto(ctx->sockfd, (const char*) buffer, len, 0,
                       (struct sockaddr*) &(ctx->client[i].caddr), sizeof(ctx->client[i].caddr)) < 0) {
#if DEBUG
                printf("failed to sendto client for (i=%d, fd=%d)\n", i, fd);
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
                printf("failed to recvfrom from client in accepting socket\n");
#endif
                ctx->error_recvfrom++;
            }
#if DEBUG
            printf("Message from UDP client: len=%d (S_addr=%d port=%d)\n", len, t.sin_addr.s_addr,t.sin_port);
#endif

            int branchidx = getbranchindex(&t, ctx);
            ctx->new_connection++;
#if DEBUG
            printf("getbranchindex=%d\n", branchidx);
#endif
            if (branchidx < 0) {
#if DEBUG
                printf("failed get branch-index corresponding to client\n");
#endif
                ctx->failed_assign++;
            } else if (ctx->spoof) {
                if (raw_send_from_to(ctx->client[branchidx].fd, buffer, len,
                                    (struct sockaddr*) &t,
                                    (struct sockaddr*) &(ctx->client[branchidx].branchaddr), 63, 1) < 0) {
#if DEBUG
                    printf("failed to send raw packet to branch[%d]\n", branchidx);
#endif
                    ctx->error_sendto++;
                }
            } else if (sendto(ctx->client[branchidx].fd, buffer, len, 0,
                              (struct sockaddr*) &(ctx->client[branchidx].branchaddr),
                              sizeof(ctx->client[branchidx].branchaddr)) < 0) {
#if DEBUG
                printf("failed to send udp packet to branch[%d]\n", branchidx);
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
    if (ctx.verbose) {
        printf("balancing method=%s\n", ctx.method==leastconn ? "least" : "naive");
        printf("accepting port=%d %s\n", ctx.selfport, ctx.spoof ? "with nat" : "");
        for (int i=0; i < ctx.branchnum; i++)
            printf("branch[%d]=%s\n", i, ctx.branch[i].hostargs);
    }

    init_acceptor(&ctx);

    process(&ctx);

    return 0;
}
