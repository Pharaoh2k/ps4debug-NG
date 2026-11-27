// SPDX-License-Identifier: GPL-3.0-only

#include "proc.h"
#include "read_lz4.h"

// 0xBDAACC18: CMD_PROC_READ with LZ4-compressed blocks. Wire format in
// read_lz4.h; the decoded bytes match CMD_PROC_READ for the same request.

struct read_lz4_ctx {
    int fd;
    uint32_t pid;
};

static void read_lz4_read(void *ctx, uint64_t address, void *buf, uint32_t len) {
    struct read_lz4_ctx *c = (struct read_lz4_ctx *)ctx;
    sys_proc_rw(c->pid, address, buf, len, 0);
}

static int read_lz4_send(void *ctx, const void *buf, uint32_t len) {
    struct read_lz4_ctx *c = (struct read_lz4_ctx *)ctx;
    return net_send_all(c->fd, buf, (int)len) == (int)len ? 0 : -1;
}

int proc_read_lz4_handle(int fd, struct cmd_packet *packet) {
    if (!packet->data) {
        net_send_int32(fd, CMD_DATA_NULL);
        return 0;
    }

    if (!read_lz4_request_ok(packet->data, packet->datalen)) {
        net_send_int32(fd, CMD_ERROR);
        return 0;
    }

    struct read_lz4_request *rp = (struct read_lz4_request *)packet->data;

    uint8_t *mem = (uint8_t *)net_alloc_buffer(READ_LZ4_STATE_SIZE + 2 * READ_LZ4_BUF_SIZE);
    if (!mem) {
        net_send_int32(fd, CMD_DATA_NULL);
        return 0;
    }

    uint8_t *raw = mem + READ_LZ4_STATE_SIZE;
    uint8_t *out = raw + READ_LZ4_BUF_SIZE;
    struct read_lz4_ctx ctx = { fd, rp->pid };

    int r = read_lz4_stream(mem, raw, out, CMD_SUCCESS, rp->address, rp->length,
                            read_lz4_read, read_lz4_send, &ctx);

    free(mem);

    // A failed send leaves the response cut short; drop the client.
    return r ? 1 : 0;
}
