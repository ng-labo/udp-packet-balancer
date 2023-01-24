#ifndef __SELECT_H__
#define __SELECT_H__

#include <netinet/in.h>
#include <unistd.h>
#include <netdb.h>

#define CONNUM 10
#define BRANCHNUMMAX 3

struct variables {
    int sockfd;
    unsigned short selfport;
    struct sockaddr_in selfaddr;
    int branchfd[CONNUM];
    struct sockaddr_in caddr[CONNUM];
    struct sockaddr_in branchaddr[CONNUM];
    time_t lasttscon[CONNUM];
    int branchnum;
    unsigned short branchport[BRANCHNUMMAX];
    char host[128];
};

#endif
