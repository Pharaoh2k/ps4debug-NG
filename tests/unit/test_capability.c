// SPDX-License-Identifier: GPL-3.0-only
//
// Host-side tests for the capability level advertised by CMD_BRANDING.
// Build and run with: make -C tests/unit (or make -C debugger test)

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "capability.h"
#include "version.h"

static int failures;

#define CHECK(cond) do { \
    if (!(cond)) { \
        fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); \
        failures++; \
    } \
} while (0)

// Same layout server.c sends: brand, one NUL, capability level, no trailing NUL.
#define TEST_BRANDING "ps4debug-NG by Pharaoh2k v" PS4DEBUG_NG_VERSION_STR
static const char brand[] = TEST_BRANDING "\0" PACKET_CAPABILITY_LEVEL;

// Parse "major.minor" the way a capability-aware client would.
static int level_value(const char *s, size_t len) {
    int major = 0, minor = 0, i = 0;
    if (len == 0) return -1;
    while (i < (int)len && s[i] >= '0' && s[i] <= '9') major = major * 10 + (s[i++] - '0');
    if (i == 0 || i >= (int)len || s[i++] != '.') return -1;
    int start = i;
    while (i < (int)len && s[i] >= '0' && s[i] <= '9') minor = minor * 10 + (s[i++] - '0');
    if (i == start || i != (int)len) return -1;
    return major * 1000 + minor;
}

static int supports_attach_port(const char *level, size_t len) {
    int have = level_value(level, len);
    int need = level_value(CAPABILITY_DEBUG_ATTACH_PORT, strlen(CAPABILITY_DEBUG_ATTACH_PORT));
    return have >= 0 && have >= need;
}

static char *read_file(const char *path) {
    FILE *f = fopen(path, "rb");
    if (!f) return NULL;
    fseek(f, 0, SEEK_END);
    long n = ftell(f);
    fseek(f, 0, SEEK_SET);
    char *buf = malloc((size_t)n + 1);
    if (buf && fread(buf, 1, (size_t)n, f) != (size_t)n) { free(buf); buf = NULL; }
    if (buf) buf[n] = '\0';
    fclose(f);
    return buf;
}

int main(void) {
    // The advertised level is 1.2 and the feature gate is 1.2.
    CHECK(strcmp(PACKET_CAPABILITY_LEVEL, "1.2") == 0);
    CHECK(strcmp(CAPABILITY_DEBUG_ATTACH_PORT, "1.2") == 0);

    // Wire layout: C-string clients still see only the unchanged brand.
    size_t len = sizeof(brand) - 1;
    CHECK(strcmp(brand, TEST_BRANDING) == 0);
    CHECK(brand[strlen(TEST_BRANDING)] == '\0');
    const char *level = brand + strlen(TEST_BRANDING) + 1;
    size_t level_len = len - strlen(TEST_BRANDING) - 1;
    CHECK(level_len == 3 && memcmp(level, "1.2", 3) == 0);
    CHECK(brand[len - 1] != '\0'); // no trailing NUL on the wire

    // Feature gating: 1.2+ supports the attach port, older levels do not.
    CHECK(supports_attach_port(level, level_len));
    CHECK(supports_attach_port("1.10", 4));
    CHECK(supports_attach_port("2.0", 3));
    CHECK(!supports_attach_port("1.1", 3));
    CHECK(!supports_attach_port("1.0", 3));
    CHECK(!supports_attach_port("", 0));   // pre-capability servers send no level
    CHECK(!supports_attach_port("1", 1));

    // server.c must build the CMD_BRANDING reply from the shared macro.
    char *src = read_file(SERVER_C_PATH);
    CHECK(src != NULL);
    if (src) {
        CHECK(strstr(src, "PACKET_BRANDING \"\\0\" PACKET_CAPABILITY_LEVEL") != NULL);
        CHECK(strstr(src, "PACKET_BRANDING \"\\0\" \"1.") == NULL);
        free(src);
    }

    if (failures) {
        fprintf(stderr, "%d test(s) failed\n", failures);
        return 1;
    }
    printf("test_capability: all tests passed\n");
    return 0;
}
