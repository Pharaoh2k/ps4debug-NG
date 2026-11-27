// SPDX-License-Identifier: GPL-3.0-only

#ifndef _READ_LZ4_H
#define _READ_LZ4_H

// 0xBDAACC18: CMD_PROC_READ with LZ4-compressed blocks (capability 1.3).
//
// Request body (exactly 20 bytes):
//   struct read_lz4_request { u32 pid; u64 address; u32 length; u32 flags; }
//   flags must be 0 (reserved).
//
// Response:
//   u32 CMD_SUCCESS, u32 block_size (65536), then one frame per block of the
//   requested range: every block is block_size bytes except the last, which
//   holds the remainder (1..block_size). Each frame is a u32 header followed
//   by the payload:
//     bit 31 clear: payload is an LZ4 block that decodes to exactly the block
//                   length; its size (bits 0-30) is less than the block length.
//     bit 31 set:   payload is the block stored as is; its size (bits 0-30)
//                   equals the block length.
//   Blocks are independent (no shared dictionary), so a client can decode
//   each one as it arrives. The decoded bytes are the bytes CMD_PROC_READ
//   returns for the same request: unreadable memory reads as zeros.
//
// Free of SDK and libc dependencies so it can be unit-tested on the host.
// The codec lives in read_lz4.c, the only file that includes lz4.h (whose
// <stddef.h> clashes with the libPS4 types).

#include <stdint.h>

#define READ_LZ4_PACKET_SIZE 20u
#define READ_LZ4_BLOCK_SIZE  0x10000u
#define READ_LZ4_STORED      0x80000000u
#define READ_LZ4_SIZE_MASK   0x7FFFFFFFu
#define READ_LZ4_HDR_SIZE    4u

// Sizes of the buffers read_lz4_stream needs. The state must be 8-byte
// aligned (malloc'd memory is); read_lz4.c checks it is an LZ4_stream_t.
#define READ_LZ4_STATE_SIZE  16416u
#define READ_LZ4_BUF_SIZE    (READ_LZ4_HDR_SIZE + READ_LZ4_BLOCK_SIZE)

struct read_lz4_request {
    uint32_t pid;
    uint64_t address;
    uint32_t length;
    uint32_t flags;
} __attribute__((packed));

_Static_assert(sizeof(struct read_lz4_request) == READ_LZ4_PACKET_SIZE,
               "read_lz4_request must match the 20-byte wire body");

// Whether a request body is well formed.
static inline int read_lz4_request_ok(const void *data, uint32_t datalen) {
    if (!data || datalen != READ_LZ4_PACKET_SIZE) {
        return 0;
    }
    return ((const struct read_lz4_request *)data)->flags == 0;
}

// Reads len bytes at address into buf; buf is zeroed first, so whatever it
// leaves unread stays zero.
typedef void (*read_lz4_read_fn)(void *ctx, uint64_t address, void *buf, uint32_t len);
// Sends len bytes; returns 0 when all of them were sent.
typedef int (*read_lz4_send_fn)(void *ctx, const void *buf, uint32_t len);

// Encodes one block into a frame. The block's len bytes (1..block size) are
// at raw + 4; raw[0..3] and all of out are scratch. Returns where the frame
// starts (raw or out) and its length in *frame_len. A block that LZ4 cannot
// shrink is stored, so a frame is never more than 4 bytes over the block.
// state must have been set up by read_lz4_stream or LZ4_initStream.
const uint8_t *read_lz4_frame(void *state, uint8_t *raw, uint8_t *out,
                              uint32_t len, uint32_t *frame_len);

// Sends the whole response after a valid request: status, block size, then
// one frame per block. state is READ_LZ4_STATE_SIZE bytes; raw and out are
// READ_LZ4_BUF_SIZE bytes each. Returns 0, or -1 if a send failed (the
// remaining blocks are not read).
int read_lz4_stream(void *state, uint8_t *raw, uint8_t *out,
                    uint32_t status, uint64_t address, uint32_t length,
                    read_lz4_read_fn read_mem, read_lz4_send_fn send, void *ctx);

#endif
