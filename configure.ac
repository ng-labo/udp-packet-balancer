AC_INIT([udpbalancer], [0.9]) # ,,, [https://github.com/ng-labo/udpbalancer])
#AC_CONFIG_SRCDIR([.udp-balancer.c])
AM_INIT_AUTOMAKE([-Wall -Werror foreign])
#AC_CONFIG_AUX_DIR([build-aux])
#AM_CONFIG_HEADER(config.h)
AC_PROG_CC
AC_PROG_INSTALL
AC_CHECK_LIB(socket,bind,gethostbyname,sendto,recvfrom)
AC_CHECK_HEADERS(stdlib.h unistd.h netinet/in.h netdb.h time.h)
AC_CHECK_FUNCS(memset strcpy)
AC_OUTPUT(Makefile)
