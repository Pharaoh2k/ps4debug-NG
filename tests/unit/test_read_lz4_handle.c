// SPDX-License-Identifier: GPL-3.0-only
//
// Tests for the 0xBDAACC18 handler itself: request checks, status replies,
// the buffer it frees, and the bytes it sends. Includes the real
// debugger/source/proc_read_lz4.c against fake sockets and target memory,
// linked with the real read_lz4.c and LZ4 (built apart, without the SDK).
//
// Build and run with: make -C tests/unit (or make -C debugger test)

#define free test_free
void test_free(void *p);
#include "../../debugger/source/proc_read_lz4.c"
#undef free
void free(void *ptr); // libc.h's declaration was renamed above

// lz4.h cannot be included next to the SDK headers (see read_lz4.h).
int LZ4_decompress_safe(const char *src, char *dst, int compressedSize, int dstCapacity);

static int failures;

static void say(const char *s) { write(2, s, strlen(s)); }

#define CHECK(cond) do { \
    if (!(cond)) { \
        char m_[256]; \
        snprintf(m_, sizeof(m_), "FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); \
        say(m_); \
        failures++; \
    } \
} while (0)

// ---- fakes -------------------------------------------------------------------

#define FD        7
#define PID       4321
#define MEM_BASE  0x200000000ull
#define MEM_SIZE  (512u * 1024)

static uint8_t target[MEM_SIZE];
static int reads, bad_reads;

static uint8_t wire[2 * MEM_SIZE];
static size_t wire_len;
static uint32_t int32_reply;
static int int32_sends, send_calls, fail_send_at, short_send;

static int allocs, frees, fail_alloc;
static void *last_alloc;

// A read that leaves the mapping fails whole and copies nothing.
static int fake_fill(uint64_t a, void *d, uint64_t l) {
    if (a < MEM_BASE || a + l > MEM_BASE + MEM_SIZE) return -1;
    memcpy(d, target + (a - MEM_BASE), l);
    return 0;
}

int sys_proc_rw(uint64_t pid, uint64_t a, void *d, uint64_t l, uint64_t w) {
    reads++;
    if (pid != PID || w != 0) bad_reads++;
    return fake_fill(a, d, l);
}

int net_send_int32(int fd, uint32_t v) {
    CHECK(fd == FD);
    int32_reply = v;
    int32_sends++;
    return 4;
}

int net_send_all(int fd, const void *d, int l) {
    CHECK(fd == FD);
    send_calls++;
    if (fail_send_at && send_calls == fail_send_at) return short_send ? l - 1 : -1;
    memcpy(wire + wire_len, d, (size_t)l);
    wire_len += (size_t)l;
    return l;
}

void *net_alloc_buffer(size_t n) {
    if (fail_alloc) return NULL;
    allocs++;
    last_alloc = malloc(n);
    return last_alloc;
}

void test_free(void *p) {
    if (p && p == last_alloc) frees++;
    free(p);
}

static void reset(void) {
    reads = bad_reads = 0;
    wire_len = 0;
    int32_reply = 0;
    int32_sends = send_calls = fail_send_at = short_send = 0;
    allocs = frees = fail_alloc = 0;
    last_alloc = NULL;
}

static uint32_t get32(const uint8_t *p) {
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

static int run(const void *body, uint32_t datalen) {
    uint8_t copy[64];
    struct cmd_packet pk;
    memset(&pk, 0, sizeof(pk));
    pk.magic = PACKET_MAGIC;
    pk.cmd = 0xBDAACC18u;
    pk.datalen = datalen;
    if (body) memcpy(copy, body, datalen);
    pk.data = body ? copy : NULL;
    return proc_read_lz4_handle(FD, &pk);
}

// Decodes the captured response and compares it, block by block, with what
// CMD_PROC_READ returns: a zeroed block filled by the same read.
static int response_matches(uint64_t address, uint32_t length) {
    if (wire_len < 8 || get32(wire) != CMD_SUCCESS || get32(wire + 4) != READ_LZ4_BLOCK_SIZE) return 0;
    static uint8_t dec[READ_LZ4_BLOCK_SIZE];
    size_t pos = 8;
    for (uint32_t done = 0; done < length; ) {
        uint32_t n = length - done < READ_LZ4_BLOCK_SIZE ? length - done : READ_LZ4_BLOCK_SIZE;
        uint32_t hdr = get32(wire + pos), size = hdr & READ_LZ4_SIZE_MASK;
        pos += 4;
        if (hdr & READ_LZ4_STORED) {
            if (size != n) return 0;
            memcpy(dec, wire + pos, n);
        } else if (size == 0 || size >= n ||
                   LZ4_decompress_safe((const char *)wire + pos, (char *)dec, (int)size, (int)n) != (int)n) {
            return 0;
        }
        static uint8_t want[READ_LZ4_BLOCK_SIZE];
        memset(want, 0, n);
        fake_fill(address + done, want, n);
        if (memcmp(dec, want, n) != 0) return 0;
        pos += size;
        done += n;
    }
    return pos == wire_len;
}

int main(void) {
    for (uint32_t i = 0; i < MEM_SIZE; i++) {
        target[i] = (i < MEM_SIZE / 2) ? (uint8_t)(i / 64) : (uint8_t)(i * 2654435761u >> 13);
    }

    struct read_lz4_request rq = { PID, MEM_BASE + 0x123, 0x50000, 0 };

    // No body: CMD_DATA_NULL, nothing read, client kept.
    reset();
    CHECK(run(NULL, 0) == 0);
    CHECK(int32_reply == CMD_DATA_NULL && int32_sends == 1 && send_calls == 0 && reads == 0);

    // Wrong body size (a 16-byte CMD_PROC_READ body, or trailing bytes).
    reset();
    CHECK(run(&rq, 16) == 0);
    CHECK(int32_reply == CMD_ERROR && send_calls == 0 && reads == 0 && allocs == 0);
    uint8_t longer[24] = {0};
    memcpy(longer, &rq, sizeof(rq));
    reset();
    CHECK(run(longer, 24) == 0);
    CHECK(int32_reply == CMD_ERROR && send_calls == 0 && reads == 0);

    // Reserved flags set.
    rq.flags = 2;
    reset();
    CHECK(run(&rq, sizeof(rq)) == 0);
    CHECK(int32_reply == CMD_ERROR && send_calls == 0 && reads == 0);
    rq.flags = 0;

    // Out of memory: CMD_DATA_NULL, client kept.
    reset();
    fail_alloc = 1;
    CHECK(run(&rq, sizeof(rq)) == 0);
    CHECK(int32_reply == CMD_DATA_NULL && send_calls == 0 && reads == 0);

    // A good request: status comes in the stream (no separate int32), the
    // pid is the requested one, reads only, buffer freed, bytes match.
    reset();
    CHECK(run(&rq, sizeof(rq)) == 0);
    CHECK(int32_sends == 0);
    CHECK(reads == 5 && bad_reads == 0);
    CHECK(allocs == 1 && frees == 1);
    CHECK(response_matches(rq.address, rq.length));

    // Range running off the mapping: the failed block decodes as zeros.
    struct read_lz4_request edge = { PID, MEM_BASE + MEM_SIZE - 0x8000, 0x20000, 0 };
    reset();
    CHECK(run(&edge, sizeof(edge)) == 0);
    CHECK(response_matches(edge.address, edge.length));

    // Zero length: status and block size only.
    struct read_lz4_request empty = { PID, MEM_BASE, 0, 0 };
    reset();
    CHECK(run(&empty, sizeof(empty)) == 0);
    CHECK(wire_len == 8 && reads == 0 && frees == 1);

    // A failed or short send drops the client, frees the buffer, stops reading.
    reset();
    fail_send_at = 2;
    CHECK(run(&rq, sizeof(rq)) == 1);
    CHECK(reads == 1 && frees == 1);
    reset();
    fail_send_at = 3;
    short_send = 1;
    CHECK(run(&rq, sizeof(rq)) == 1);
    CHECK(reads == 2 && frees == 1);

    if (failures) {
        char m[64];
        snprintf(m, sizeof(m), "%d test(s) failed\n", failures);
        say(m);
        return 1;
    }
    say("test_read_lz4_handle: all tests passed\n");
    return 0;
}
