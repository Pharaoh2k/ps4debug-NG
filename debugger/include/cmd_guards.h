// SPDX-License-Identifier: GPL-3.0-only

#ifndef PS4DEBUG_NG_CMD_GUARDS_H
#define PS4DEBUG_NG_CMD_GUARDS_H

#include <stdint.h>
#include "protocol.h"

// Hard cap on packet->datalen accepted from a client. Anything larger is
// rejected with CMD_TOO_MUCH_DATA before a single byte is allocated; the old
// code malloc'd + prefaulted the full client-supplied size, so one packet
// could hang the console.
#define CMD_MAX_DATALEN (16u * 1024u * 1024u)

// Minimum packet->datalen a command requires, i.e. the fixed-size struct its
// handler reads from packet->data. Handlers that take additional variable
// data beyond the fixed struct validate the extra length themselves.
// Returns 0 for commands that take no payload, and for commands unknown to
// the table (their handlers keep their own checks).
uint32_t cmd_min_datalen(uint32_t cmd);

#endif
