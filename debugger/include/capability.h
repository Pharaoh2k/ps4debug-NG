// SPDX-License-Identifier: GPL-3.0-only

#ifndef _CAPABILITY_H
#define _CAPABILITY_H

// Capability level reported after the NUL in the CMD_BRANDING reply.
// It is independent of PS4DEBUG_NG_VERSION_STR; bump it only for new wire features.
//   1.0 - 0xBDAACC04 bulk write
//   1.1 - content-verified process writes, truthful terminal status
//   1.2 - CMD_DEBUG_ATTACH accepts an optional debug_port (see debug_port.h)
#define PACKET_CAPABILITY_LEVEL "1.2"

// Minimum level at which clients may send the 12-byte CMD_DEBUG_ATTACH body.
#define CAPABILITY_DEBUG_ATTACH_PORT "1.2"

#endif
