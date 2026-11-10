// SPDX-License-Identifier: GPL-3.0-only
//
// Minimal host-build stub for the Orbis <ps4.h> so the protocol-facing
// sources (cmd_guards.c) can be compiled and fuzzed on a dev machine.
// NOT used by the payload build.

#ifndef PS4_H_HOST_STUB
#define PS4_H_HOST_STUB

#include <stddef.h>
#include <stdint.h>
#include <stdarg.h>

// protocol.h embeds struct sockaddr_in in struct server_client.
struct in_addr {
    uint32_t s_addr;
};
struct sockaddr_in {
    uint8_t  sin_len;
    uint8_t  sin_family;
    uint16_t sin_port;
    struct   in_addr sin_addr;
    char     sin_zero[8];
};

#endif
