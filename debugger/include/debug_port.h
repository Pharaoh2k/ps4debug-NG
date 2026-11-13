// SPDX-License-Identifier: GPL-3.0-only

#ifndef _DEBUG_PORT_H
#define _DEBUG_PORT_H

#include <stdint.h>

// Default TCP port for the asynchronous debug-interrupt channel (PS4 -> client).
#define DEBUG_PORT 755

// Offset of the optional debug_port field inside the CMD_DEBUG_ATTACH body.
#define DEBUG_ATTACH_PORT_OFFSET 8

// Resolve the interrupt-channel port requested in a CMD_DEBUG_ATTACH body.
//
//   datalen < 12            -> legacy request, use DEBUG_PORT (755)
//   datalen >= 12, port 0   -> use DEBUG_PORT (755)
//   datalen >= 12, 1..65535 -> use the requested port
//   datalen >= 12, > 65535  -> invalid, returns -1
//
// Kept free of SDK dependencies so it can be unit-tested on the host.
static inline int debug_attach_resolve_port(const void *data, uint32_t datalen, uint16_t *port) {
    uint32_t requested = 0;

    if (data && datalen >= DEBUG_ATTACH_PORT_OFFSET + sizeof(uint32_t)) {
        const uint8_t *p = (const uint8_t *)data + DEBUG_ATTACH_PORT_OFFSET;
        requested = (uint32_t)p[0] | ((uint32_t)p[1] << 8) |
                    ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
    }

    if (requested > 0xFFFF) {
        return -1;
    }

    *port = requested ? (uint16_t)requested : (uint16_t)DEBUG_PORT;
    return 0;
}

#endif
