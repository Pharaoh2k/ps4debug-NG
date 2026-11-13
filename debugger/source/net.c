// SPDX-License-Identifier: GPL-3.0-only

#include "net.h"

// Non-blocking sockets return EWOULDBLOCK when the peer stalls; without a
// backoff these loops spun at 100% CPU, and force-recv spun forever.
#define NET_IO_BACKOFF_US   1000
#define NET_IO_TIMEOUT_US   (10u * 1000u * 1000u) /* 10s */
#define NET_IO_MAX_STALLS   (NET_IO_TIMEOUT_US / NET_IO_BACKOFF_US)

int net_select(int fd, fd_set *readfds, fd_set *writefds, fd_set *exceptfds, struct timeval *timeout) {
    return syscall(93, fd, readfds, writefds, exceptfds, timeout);
}

int net_send_all(int fd, const void *data, int length) {
    const unsigned char *bytes = (const unsigned char *)data;
    int left = length;
    int offset = 0;
    int sent = 0;
    int stalls = 0;

    errno = NULL;

    while (left > 0) {
        if (left > 0x10000) {
            sent = write(fd, bytes + offset, 0x10000);
        } else {
            sent = write(fd, bytes + offset, left);
        }

        if (sent <= 0) {
            if(errno && errno != EWOULDBLOCK) {
                return sent;
            }
            // Time out only when no bytes have moved for NET_IO_TIMEOUT_US;
            // progress resets the stall count.
            if (++stalls > NET_IO_MAX_STALLS) {
                return -1;
            }
            sceKernelUsleep(NET_IO_BACKOFF_US);
        } else {
            offset += sent;
            left -= sent;
            stalls = 0;
        }
    }

    return offset;
}

int net_recv_all(int fd, void *data, int length, int force) {
    int left = length;
    int offset = 0;
    int recv = 0;
    int stalls = 0;

    errno = NULL;

    while (left > 0) {
        if (left > 0x10000) {
            recv = read(fd, data + offset, 0x10000);
        } else {
            recv = read(fd, data + offset, left);
        }

        if (recv == 0) {
            return offset;
        }
        if (recv < 0) {
            if (force && errno == EWOULDBLOCK) {
                if (++stalls > NET_IO_MAX_STALLS) {
                    return -1;
                }
                sceKernelUsleep(NET_IO_BACKOFF_US);
                continue;
            }
            return recv;
        }

        offset += recv;
        left -= recv;
        stalls = 0;
    }

    return offset;
}

int net_send_int32(int fd, uint32_t status) {
    uint32_t d = status;

    return net_send_all(fd, &d, sizeof(uint32_t));
}

int net_get_ip_address(char *out) {
    char buf[256];

    if (sceNetCtlInit() < 0) {
        return -1;
    }

    int r = sceNetCtlGetInfo(SCE_NET_CTL_INFO_IP_ADDRESS, (SceNetCtlInfo *)buf);
    if (r < 0) {
        return -1;
    }

    memcpy(out, buf, 16);
    sceNetCtlTerm();
    return r;
}
