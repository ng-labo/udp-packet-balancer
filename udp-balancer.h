#ifndef __UDPBALANCER_H__
#define __UDPBALANCER_H__

#include <netinet/in.h>
#include <unistd.h>
#include <netdb.h>

#define PACKETBUFLEN 1518

#define CONNUM 16
#define BRANCHNUMMAX 2
#define SOCKETBUFLEN 1048576

#define FORGETTING_IN_SEC 60

enum selectmethod { rotation, leastconn };

struct branch {
    char hostargs[80];
    struct sockaddr_in s_addr;
    int activecount;
};

struct client {
    int fd; // to branch socket
    struct sockaddr* caddr;
    size_t caddrlen;
    struct sockaddr_in6 caddr_buf; // as longer size structure
    time_t lasttscon;
    int connindex;;
};
    
struct variables {
    // setting and derived
    int spoof;
    enum selectmethod method;
    int socketbuflen;
    int verbose;

    int (*newbranchindex)(struct variables*, int);

    int branchnum;
    struct branch branch[BRANCHNUMMAX];

    char selfhost[64];
    unsigned short selfport;

    // processing values
    int sockfd;
    struct sockaddr* selfaddr;
    size_t selfaddrlen;
    struct sockaddr_in6 selfaddr_buf; // as longer size structure

    struct client brokers[CONNUM];
    
    // statistics counter
    unsigned long new_connection;
    unsigned long error_recvfrom;
    unsigned long error_sendto;
    unsigned long failed_assign;
};

#endif // __UDPBALANCER_H__
