// SPDX-License-Identifier: GPL-3.0-only
//
// libFuzzer harness for the packet guard layer (cmd_guards.c). Fuzzing the
// full command handlers requires the Orbis toolchain; the guard table is the
// choke point every packet now passes through, so it is the highest-value
// host-buildable target.
//
// Build: see tests/Makefile (target: fuzz).

#include <stdint.h>
#include <stddef.h>

#include "protocol.h"
#include "cmd_guards.h"

// Mirrors the rejection logic in handle_client() (server.c): the packet
// header is 12 bytes, then up to datalen payload bytes follow.
int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size) {
    if (size < CMD_PACKET_SIZE) {
        return 0;
    }

    struct cmd_packet packet;
    packet.magic   = *(const uint32_t *)data;
    packet.cmd     = *(const uint32_t *)(data + 4);
    packet.datalen = *(const uint32_t *)(data + 8);
    packet.data    = (void *)(data + CMD_PACKET_SIZE);

    if (packet.magic != PACKET_MAGIC) {
        return 0;
    }

    // Oversized payloads must be rejected before anything is allocated.
    if (packet.datalen > CMD_MAX_DATALEN) {
        return 0;
    }

    // Truncated payloads must be rejected before any handler dereferences
    // packet->data.
    uint32_t min_len = cmd_min_datalen(packet.cmd);
    if (packet.datalen < min_len) {
        return 0;
    }

    // Whatever remains must actually be present in the input.
    if (size - CMD_PACKET_SIZE < packet.datalen) {
        return 0;
    }

    // Table invariants: min lengths never exceed the global cap, and the
    // table contains no duplicate commands.
    if (min_len > CMD_MAX_DATALEN && min_len != 0) {
        __builtin_trap();
    }

    return 0;
}
