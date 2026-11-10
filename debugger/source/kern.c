// SPDX-License-Identifier: GPL-3.0-only

#include "kern.h"
#include "kdbg.h"

// Client-controlled kernel R/W size; larger requests are rejected rather
// than malloc'd + prefaulted verbatim.
#define KERN_RW_MAX_LENGTH (16u * 1024u * 1024u)

int kern_base_handle(int fd, struct cmd_packet *packet) {
    uint64_t kbase = 0;
    sys_kern_base(&kbase);
    net_send_int32(fd, CMD_SUCCESS);
    net_send_all(fd, &kbase, 8);
    return 0;
}

int kern_read_handle(int fd, struct cmd_packet *packet) {
    struct cmd_kern_read_packet *rp = (struct cmd_kern_read_packet *)packet->data;
    if (!rp) {
        net_send_int32(fd, CMD_DATA_NULL);
        return 0;
    }
    if (rp->length > KERN_RW_MAX_LENGTH) {
        net_send_int32(fd, CMD_TOO_MUCH_DATA);
        return 0;
    }

    void *data = net_alloc_buffer(rp->length);
    if (!data) {
        net_send_int32(fd, CMD_DATA_NULL);
        return 1;
    }

    sys_kern_rw(rp->address, data, rp->length, 0);

    net_send_int32(fd, CMD_SUCCESS);
    net_send_all(fd, data, rp->length);
    free(data);
    return 0;
}

int kern_write_handle(int fd, struct cmd_packet *packet) {
    struct cmd_kern_write_packet *wp = (struct cmd_kern_write_packet *)packet->data;
    if (!wp) {
        net_send_int32(fd, CMD_DATA_NULL);
        return 0;
    }
    if (wp->length > KERN_RW_MAX_LENGTH) {
        net_send_int32(fd, CMD_TOO_MUCH_DATA);
        return 0;
    }

    void *data = net_alloc_buffer(wp->length);
    if (!data) {
        net_send_int32(fd, CMD_DATA_NULL);
        return 1;
    }

    net_send_int32(fd, CMD_SUCCESS);
    if (net_recv_all(fd, data, wp->length, 1) != (int)wp->length) {
        free(data);
        return 1;
    }
    sys_kern_rw(wp->address, data, wp->length, 1);
    net_send_int32(fd, CMD_SUCCESS);
    free(data);
    return 0;
}

int kern_handle(int fd, struct cmd_packet *packet) {
    switch (packet->cmd) {
        case CMD_KERN_BASE:   return kern_base_handle(fd, packet);
        case CMD_KERN_READ:   return kern_read_handle(fd, packet);
        case CMD_KERN_WRITE:  return kern_write_handle(fd, packet);
    }
    return 1;
}
