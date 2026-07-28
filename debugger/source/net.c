// SPDX-License-Identifier: GPL-3.0-only

#include "net.h"

int net_select(int fd, fd_set *readfds, fd_set *writefds, fd_set *exceptfds, struct timeval *timeout) {
    return syscall(93, fd, readfds, writefds, exceptfds, timeout);
}

int net_send_all(int fd, const void *data, int length) {
    const unsigned char *bytes = (const unsigned char *)data;
    int left = length;
    int offset = 0;
    int sent = 0;

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
        } else {
            offset += sent;
            left -= sent;
        }
    }

    return offset;
}

int net_recv_all(int fd, void *data, int length, int force) {
    int left = length;
    int offset = 0;
    int recv = 0;

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
                continue;
            }
            return recv;
        }

        offset += recv;
        left -= recv;
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
