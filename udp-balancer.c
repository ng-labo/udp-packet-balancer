/**
 udp pakcet balancer
 */
#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <stdlib.h>
#include <time.h>
#include "udp-balancer.h"

void usage() {
    fprintf(stderr, "udp-balancer branch-host-address branch-host-port accepting-port ...\n");
    exit(0);
}

void error(const char *msg) {
    perror(msg);
    exit(1);
}

void setzero_sockaddr_in(struct sockaddr_in* c) {
    memset(c, sizeof(struct sockaddr_in) , 0);
}

int branchsocket(const char* hostname, unsigned short portno, struct sockaddr_in* sa) {
    int sockfd;
    struct hostent *server;

    sockfd = socket(AF_INET, SOCK_DGRAM, 0);
    if (sockfd < 0) error("ERROR opening socket");
    server = gethostbyname(hostname);
    if (server == NULL) {
        fprintf(stderr,"ERROR, no such host as %s\n", hostname);
        exit(0);
    }

    setzero_sockaddr_in(sa);
    sa->sin_family = AF_INET;
    bcopy((char *)server->h_addr, (char *) &(sa->sin_addr.s_addr), server->h_length);
    sa->sin_port = htons(portno);

    return sockfd;
}

void initialize(int ac, char* av[], struct variables* ctx) {
    ctx->sockfd = -1;
    setzero_sockaddr_in(&(ctx->selfaddr));
    for(int i=0;i < CONNUM; i++) {
        setzero_sockaddr_in(&(ctx->caddr[i]));
        ctx->branchfd[i] = -1;
        setzero_sockaddr_in(&(ctx->branchaddr[i]));
    }

    strcpy(ctx->host, (const char *) av[1]);
    ctx->selfport = atoi(av[2]);;
    for (int i=3; i < ac; i++) {
        ctx->branchport[i-3] = atoi(av[i]);
    }
    ctx->branchnum = ac - 3;
}

static inline int nfds(struct variables *ctx) {
    int ret = -1;
    for(int i=0; i<CONNUM; i++) {
        if (ctx->branchfd[i]>=0 && ctx->branchfd[i]>ret) {
            ret = ctx->branchfd[i];
        }
    }
    if (ctx->sockfd>ret) {
        ret = ctx->sockfd;
    }
    return (ret + 1);
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

static inline int putbranchaddr(struct sockaddr_in* t, struct variables* ctx) {
    time_t now = time(NULL);
    for(int i=0; i<CONNUM; i++) {
        if(ctx->caddr[i].sin_port == t->sin_port &&
           ctx->caddr[i].sin_addr.s_addr == t->sin_addr.s_addr) {
            ctx->lasttscon[i] = now;
            return i;
        }
    }
    time_t l = now - 60;
    for(int i=0; i<CONNUM; i++) {
        if(ctx->lasttscon[i]>0 && ctx->branchfd[i]>-1 && ctx->lasttscon[i] < l) {
            close(ctx->branchfd[i]);
            ctx->branchfd[i] = -1;
            ctx->lasttscon[i] = 0;
            setzero_sockaddr_in(&(ctx->caddr[i]));
            printf("cleanup index=%d\n", i);
        }
    }
    for(int i=0; i<CONNUM; i++) {
        if(ctx->branchfd[i] == -1){
            ctx->caddr[i] = *t;
            ctx->branchfd[i] = branchsocket(ctx->host, ctx->branchport[i % ctx->branchnum], &(ctx->branchaddr[i]));
            //printf("branchfd[%d]=%d\n", i, ctx->branchfd[i]);
            return i;
            
        }
    }
    return -1;
}

int main(int ac, char** av) {
    struct variables ctx;
    char buffer[1518];
    fd_set rset;

    if (ac<4) usage(); // and exit

    initialize(ac, av, &ctx);
    printf("forwarding port=%d\n", ctx.selfport);
    printf("branchnum=%d\n", ctx.branchnum);
    for (int i=0; i<ctx.branchnum; i++) {
        printf("branch[%d]=%d\n", i, ctx.branchport[i]);
    }

    init_acceptor(&ctx);
    printf("bindok ctx.sockfd=%d\n", ctx.sockfd);

    for (;;) {
        FD_ZERO(&rset);
        FD_SET(ctx.sockfd, &rset);
        for(int i=0; i<CONNUM; i++) {
            if (ctx.branchfd[i]>=0) FD_SET(ctx.branchfd[i], &rset);
        }

        /*int r = */select(nfds(&ctx), &rset, NULL, NULL, NULL);

        for(int i=0; i<CONNUM; i++) {
            // branch-i socket
            int fd = ctx.branchfd[i];
            if(fd < 0) continue;
            if (!FD_ISSET(fd, &rset)) continue;
            // signaled
            struct sockaddr_in c;
            int sz = sizeof(c);
            bzero(buffer, sizeof(buffer));
            // receive
            int len = recvfrom(fd, buffer, sizeof(buffer), 0, (struct sockaddr*) &c, &sz);
            if (len<0) {
                printf("failed to recvfrom from boss\n");
            }
            // relay to client i
            if (sendto(ctx.sockfd, (const char*) buffer, len, 0,
                (struct sockaddr*) &(ctx.caddr[i]), sizeof(ctx.caddr[i]))<0) {
                printf("failed i=%d, fd=%d\n", i, fd);
            };
        }
        if (FD_ISSET(ctx.sockfd, &rset)) {
            struct sockaddr_in t;
            int sz = sizeof(t);
            bzero(buffer, sizeof(buffer));
            int len = recvfrom(ctx.sockfd, buffer, 1518, 0, (struct sockaddr*) &t, &sz);
            if (len<0) {
                printf("failed recvfrom\n");
            }
            //printf("Message from UDP client: len=%d (S_addr=%d port=%d)\n", len, t.sin_addr.s_addr,t.sin_port);

            int branchidx = putbranchaddr(&t, &ctx);
            if (branchidx<0) {
                printf("failed get branchaddr\n");
            } else {
                if (sendto(ctx.branchfd[branchidx], buffer, len, 0,
                       (struct sockaddr*) &(ctx.branchaddr[branchidx]),
                       sizeof(ctx.branchaddr[branchidx]))<0) {
                    printf("failed to send to boss\n");
                }
            }
        }
    }
    return 0;
}
