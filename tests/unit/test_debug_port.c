// SPDX-License-Identifier: GPL-3.0-only
//
// Host-side unit tests for debug_attach_resolve_port() (CMD_DEBUG_ATTACH port option).
// Build and run with: make -C tests/unit (or make -C debugger test)

#include <stdio.h>
#include <string.h>

#include "debug_port.h"

static int failures;

#define CHECK(cond) do { \
    if (!(cond)) { \
        fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); \
        failures++; \
    } \
} while (0)

static void put_u32(uint8_t *buf, uint32_t v) {
    buf[0] = (uint8_t)v;
    buf[1] = (uint8_t)(v >> 8);
    buf[2] = (uint8_t)(v >> 16);
    buf[3] = (uint8_t)(v >> 24);
}

static int resolve(uint32_t datalen, uint32_t port_field, uint16_t *out) {
    uint8_t body[16];
    memset(body, 0, sizeof(body));
    put_u32(body + 0, 1234);         // pid
    put_u32(body + 4, 0x0100A8C0);   // client_ip 192.168.0.1
    put_u32(body + 8, port_field);   // debug_port (ignored when datalen < 12)
    *out = 0xBEEF;
    return debug_attach_resolve_port(body, datalen, out);
}

int main(void) {
    uint16_t port;

    // Legacy clients: 4-byte {pid} and 8-byte {pid, client_ip} bodies -> 755.
    CHECK(resolve(4, 7550, &port) == 0 && port == 755);
    CHECK(resolve(8, 7550, &port) == 0 && port == 755);
    // Truncated port field is treated as legacy, never read past datalen.
    CHECK(resolve(11, 7550, &port) == 0 && port == 755);
    // NULL body is handled safely.
    port = 0;
    CHECK(debug_attach_resolve_port(NULL, 12, &port) == 0 && port == 755);

    // Extended form: explicit port.
    CHECK(resolve(12, 7550, &port) == 0 && port == 7550);
    CHECK(resolve(12, 1, &port) == 0 && port == 1);
    CHECK(resolve(12, 755, &port) == 0 && port == 755);
    CHECK(resolve(12, 65535, &port) == 0 && port == 65535);
    // Extra trailing bytes are tolerated.
    CHECK(resolve(16, 7550, &port) == 0 && port == 7550);

    // Extended form: 0 means "use the default".
    CHECK(resolve(12, 0, &port) == 0 && port == 755);

    // Out-of-range values are rejected and leave the output untouched.
    CHECK(resolve(12, 65536, &port) == -1 && port == 0xBEEF);
    CHECK(resolve(12, 0x80000000u, &port) == -1 && port == 0xBEEF);
    CHECK(resolve(12, 0xFFFFFFFFu, &port) == -1 && port == 0xBEEF);

    if (failures) {
        fprintf(stderr, "%d test(s) failed\n", failures);
        return 1;
    }
    printf("test_debug_port: all tests passed\n");
    return 0;
}
