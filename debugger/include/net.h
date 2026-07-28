// SPDX-License-Identifier: GPL-3.0-only

#ifndef _NET_H
#define _NET_H

#include <ps4.h>
#include "errno.h"

#define	SO_USELOOPBACK	0x0040
#define	SO_LINGER		0x0080
#define	SO_NOSIGPIPE	0x0800
#define	SO_SNDBUF		0x1001
#define	SO_RCVBUF		0x1002
#define	SO_DEBUG	0x00000001
#define	SO_ACCEPTCONN	0x00000002
#define	SO_REUSEADDR	0x00000004
#define	SO_KEEPALIVE	0x00000008
#define	SO_DONTROUTE	0x00000010
#define	SO_BROADCAST	0x00000020

#define FD_SETSIZE 1024
typedef unsigned long fd_mask;

typedef struct {
    unsigned long fds_bits[FD_SETSIZE / 8 / sizeof(long)];
} fd_set;

#define FD_ZERO(s) do { int __i; unsigned long *__b=(s)->fds_bits; for(__i=sizeof (fd_set)/sizeof (long); __i; __i--) *__b++=0; } while(0)
#define FD_SET(d, s)   ((s)->fds_bits[(d)/(8*sizeof(long))] |= (1UL<<((d)%(8*sizeof(long)))))
#define FD_CLR(d, s)   ((s)->fds_bits[(d)/(8*sizeof(long))] &= ~(1UL<<((d)%(8*sizeof(long)))))
#define FD_ISSET(d, s) !!((s)->fds_bits[(d)/(8*sizeof(long))] & (1UL<<((d)%(8*sizeof(long)))))

int net_select(int fd, fd_set *readfds, fd_set *writefds, fd_set *exceptfds, struct timeval *timeout);

int net_send_all(int fd, const void *data, int length);
int net_recv_all(int fd, void *data, int length, int force);
int net_send_int32(int fd, uint32_t status);

int net_get_ip_address(char *out );

#endif
