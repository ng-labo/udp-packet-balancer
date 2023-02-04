AC_INIT([udpbalancer], [0.9]) # ,,, [https://github.com/ng-labo/udp-packet-balancer])
AM_INIT_AUTOMAKE([-Wall -Werror foreign])
AC_PROG_CC
AC_PROG_INSTALL
AC_CHECK_LIB(socket,bind,getaddrinfo,freeaddrinfo,gethostbyname,sendto,recvfrom)
AC_CHECK_HEADERS(stdlib.h unistd.h time.h netdb.h netinet/in.h netinet/ip.h netinet/udp.h arpa/inet.h sys/uio.h)
AC_CHECK_FUNCS(memset memcpy strcpy strlen)
AC_OUTPUT(Makefile)
