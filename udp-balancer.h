#ifndef __UDPBALANCER_H__
#define __UDPBALANCER_H__

#include <netinet/in.h>
#include <unistd.h>
#include <netdb.h>

#define PACKETBUFLEN 1518

#define CONNUM 10
#define BRANCHNUMMAX 3
#define SOCKETBUFLEN 1048576

enum selectmethod { rotation, leastconn };

struct variables {
    // setting and derived
    int spoof; // reserved
    enum selectmethod method; // reserved
    int socketbuflen;

    int (*newbranchindex)(struct variables*, int);

    int branchnum;
    char branch_hostargs[BRANCHNUMMAX][80]; // keep from argument
    struct sockaddr_in branch_s_addr[BRANCHNUMMAX]; // caching when initializing

    char selfhost[64];
    unsigned short selfport;

    // processing values
    int sockfd;
    struct sockaddr_in selfaddr;
    int branchfd[CONNUM];
    struct sockaddr_in caddr[CONNUM];
    struct sockaddr_in branchaddr[CONNUM];
    time_t lasttscon[CONNUM];
    int branchindexinconn[CONNUM];

    int activecount[BRANCHNUMMAX];
    
    // statistics counter
    unsigned long error_recvfrom;
    unsigned long error_sendto;
    unsigned long failed_assign;
};

#endif // __UDPBALANCER_H__
