// SPDX-License-Identifier: GPL-3.0-only

// LZ4 block framing for 0xBDAACC18 (wire format in read_lz4.h). No SDK
// headers here: this file is also built on the host by tests/unit.

#include "read_lz4.h"

#define LZ4_STATIC_LINKING_ONLY // LZ4_compress_fast_extState_fastReset
#include "lz4.h"

_Static_assert(READ_LZ4_STATE_SIZE == sizeof(LZ4_stream_t),
               "READ_LZ4_STATE_SIZE must be sizeof(LZ4_stream_t)");
_Static_assert(READ_LZ4_BLOCK_SIZE <= LZ4_MAX_INPUT_SIZE,
               "a block must fit one LZ4 call");

static void read_lz4_put32(uint8_t *p, uint32_t v) {
    p[0] = (uint8_t)v;
    p[1] = (uint8_t)(v >> 8);
    p[2] = (uint8_t)(v >> 16);
    p[3] = (uint8_t)(v >> 24);
}

const uint8_t *read_lz4_frame(void *state, uint8_t *raw, uint8_t *out,
                              uint32_t len, uint32_t *frame_len) {
    const char *src = (const char *)raw + READ_LZ4_HDR_SIZE;
    char *dst = (char *)out + READ_LZ4_HDR_SIZE;

    // Room for one byte less than the block: LZ4 gives up (returns 0) on
    // anything it cannot shrink, and the block is stored instead.
    int n = LZ4_compress_fast_extState_fastReset(state, src, dst, (int)len, (int)len - 1, 1);

    if (n > 0 && (uint32_t)n < len) {
        read_lz4_put32(out, (uint32_t)n);
        *frame_len = READ_LZ4_HDR_SIZE + (uint32_t)n;
        return out;
    }

    read_lz4_put32(raw, READ_LZ4_STORED | len);
    *frame_len = READ_LZ4_HDR_SIZE + len;
    return raw;
}

int read_lz4_stream(void *state, uint8_t *raw, uint8_t *out,
                    uint32_t status, uint64_t address, uint32_t length,
                    read_lz4_read_fn read_mem, read_lz4_send_fn send, void *ctx) {
    uint8_t preamble[8];

    if (!LZ4_initStream(state, READ_LZ4_STATE_SIZE)) {
        return -1;
    }

    read_lz4_put32(preamble, status);
    read_lz4_put32(preamble + 4, READ_LZ4_BLOCK_SIZE);
    if (send(ctx, preamble, sizeof(preamble))) {
        return -1;
    }

    while (length > 0) {
        uint32_t len = length < READ_LZ4_BLOCK_SIZE ? length : READ_LZ4_BLOCK_SIZE;
        uint32_t frame_len;
        const uint8_t *frame;

        __builtin_memset(raw + READ_LZ4_HDR_SIZE, 0, len);
        read_mem(ctx, address, raw + READ_LZ4_HDR_SIZE, len);

        frame = read_lz4_frame(state, raw, out, len, &frame_len);
        if (send(ctx, frame, frame_len)) {
            return -1;
        }

        address += len;
        length -= len;
    }

    return 0;
}
