/**
 raw socket processing in udp pakcet balancer
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <netinet/ip.h>
#include <netinet/in.h>
#include <sys/uio.h>
#include <netinet/udp.h>

/* 
   Compute IP header checksum IN NETWORK BYTE ORDER.
*/
static unsigned ip_header_checksum(const void * header)
{
    unsigned long csum = 0;
    unsigned size = ((struct ip *) header)->ip_hl;
    uint16_t *h = (uint16_t *) header;
    unsigned k;

    for (k = 0; k < size; ++k) {
        csum += *h++, csum += *h++;
    }
    while (csum > 0xffff) {
        csum = (csum & 0xffff) + (csum >> 16);
    }
    return ~csum & 0xffff;
}

/* 
   Compute UDP header checksum
*/
static uint16_t udp_sum_calc(uint16_t len_udp, uint32_t src_addr,
                             uint16_t src_port, uint32_t dest_addr,
                             uint16_t dest_port, const void * buff) {
    uint16_t prot_udp = 17;
    uint16_t chksum_init = 0;
    uint16_t udp_len_total = 0;
    uint32_t sum = 0;
    uint16_t pad = 0;

    uint16_t low;
    uint16_t high;
    int i;

    if( (len_udp % 2) != 0 ) {
        pad = 1;
    }

    low  = src_addr;
    high = ( src_addr>>16 );
    sum  += ( ( uint32_t ) high + ( uint32_t ) low );

    low  = dest_addr;
    high = ( dest_addr>>16 );
    sum  += ( ( uint32_t ) high + ( uint32_t ) low );

    udp_len_total = len_udp + 8;
    sum += ( ( uint32_t )prot_udp + ( uint32_t )udp_len_total );
    sum += ( ( uint32_t )src_port + ( uint32_t ) dest_port );
    sum += ( ( uint32_t ) udp_len_total + ( uint32_t ) chksum_init );

    for(i = 0; i< ( len_udp - pad ); i += 2) {
        high  = ntohs(*(uint16_t *)buff);
        buff +=2;
        sum  += ( uint32_t ) high;
    }

    if( pad ) {
        sum += ntohs( * ( unsigned char * ) buff );
    }

    while ( sum>>16 ) {
        sum = ( sum & 0xFFFF ) + ( sum >> 16 );
    }

    sum = ~sum;

    return ((uint16_t) htons(sum) );
}

int raw_send_from_to(int s, const void* msg, size_t msglen, struct sockaddr* saddr_generic, struct sockaddr* daddr_generic, int ttl, int flags) {
#define saddr ((struct sockaddr_in *) saddr_generic)
#define daddr ((struct sockaddr_in *) daddr_generic)
    int length;
    int sockerr;
    socklen_t sockerr_size = sizeof(sockerr);
    struct sockaddr_in dest_a;
    struct ip ih;
    struct udphdr uh;

	// sys/uio.h
    struct msghdr mh;
    struct iovec iov[3];

    uh.uh_sport = saddr->sin_port;
    uh.uh_dport = daddr->sin_port;
    uh.uh_ulen = htons (msglen + sizeof(uh));
    uh.uh_sum = flags ?
                udp_sum_calc(msglen, ntohl(saddr->sin_addr.s_addr), ntohs(saddr->sin_port),
                             ntohl(daddr->sin_addr.s_addr), ntohs(daddr->sin_port), msg)
                : 0;
  length = msglen + sizeof(uh) + sizeof(ih);

  // sys/uio.h
  ih.ip_hl = (sizeof(ih) + 3) / 4;
  ih.ip_v = 4;
  ih.ip_tos = 0;
  /* Depending on the target platform, te ip_off and ip_len fields
     should be in either host or network byte order.  Usually
     BSD-derivatives require host byte order, but at least OpenBSD
     since version 2.1 and FreeBSD since 11.0 use network byte
     order.  Linux uses network byte order for all IP header fields. */
#if defined (__linux__) || (defined (__OpenBSD__) && (OpenBSD > 199702)) || (defined (__FreeBSD_version) && (__FreeBSD_version > 1100030))
  ih.ip_len = htons(length);
  ih.ip_off = htons(0);
#else
  ih.ip_len = length;
  ih.ip_off = 0;
#endif
  ih.ip_id = htons(0);
  ih.ip_ttl = ttl;
  ih.ip_p = 17;
  ih.ip_sum = htons(0);
  ih.ip_src.s_addr = saddr->sin_addr.s_addr;
  ih.ip_dst.s_addr = daddr->sin_addr.s_addr;

  /* At least on Solaris, it seems clear that even the raw IP datagram
     transmission code will actually compute the IP header checksum
     for us.  Probably this is the case for all other systems on which
     this code works, so maybe we should just set the checksum to zero
     to avoid duplicate work.  I'm not even sure whether my IP
     checksum computation in ip_header_checksum() below is correct. */
  ih.ip_sum = ip_header_checksum (&ih);

  dest_a.sin_family = AF_INET;
  dest_a.sin_port = daddr->sin_port;
  dest_a.sin_addr.s_addr = daddr->sin_addr.s_addr;

  // sys/uio.h
  iov[0].iov_base = (char *) &ih;
  iov[0].iov_len = sizeof(ih);
  iov[1].iov_base = (char *) &uh;
  iov[1].iov_len = sizeof(uh);
  iov[2].iov_base = (char *) msg;
  iov[2].iov_len = msglen;

  bzero ((char *) &mh, sizeof(mh));
  mh.msg_name = (char *) &dest_a;
  mh.msg_namelen = sizeof(dest_a);
  mh.msg_iov = iov;
  mh.msg_iovlen = 3;

  if (sendmsg (s, &mh, 0) == -1) {
      if (getsockopt (s, SOL_SOCKET, SO_ERROR, (char *) &sockerr, &sockerr_size) == 0) {
          fprintf (stderr, "socket error: %d\n", sockerr);
          fprintf (stderr, "socket: %s\n", strerror (errno));
      }
      return -1;
  }
  return 0;
}

#undef saddr
#undef daddr

int make_raw_udp_socket(size_t sockbuflen, int af) {
    int s;
    if (af == AF_INET6) {
        fprintf (stderr, "Spoofing not supported for IPv6\n");
        return -1;
    }
    if ((s = socket (PF_INET, SOCK_RAW, IPPROTO_RAW)) == -1) return s;
    if (sockbuflen != -1) {
        if (setsockopt (s, SOL_SOCKET, SO_SNDBUF, (char *) &sockbuflen, sizeof sockbuflen) == -1) {
            fprintf (stderr, "setsockopt(SO_SNDBUF,%ld): %s\n", sockbuflen, strerror (errno));
        }
    }

#ifdef IP_HDRINCL
  /* Some BSD-derived systems require the IP_HDRINCL socket option for header spoofing.  */
  {
      int on = 1;
      if (setsockopt (s, IPPROTO_IP, IP_HDRINCL, (char *) &on, sizeof(on)) < 0) {
          fprintf (stderr, "setsockopt(IP_HDRINCL,%d): %s\n", on, strerror (errno));
      }
  }
#endif /* IP_HDRINCL */

  return s;
}

