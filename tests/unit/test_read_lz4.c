// SPDX-License-Identifier: GPL-3.0-only
//
// Host-side tests for the 0xBDAACC18 compressed read (read_lz4.c).
// Build and run with: make -C tests/unit (or make -C debugger test)
//
// Every response is decoded with the stock LZ4 block decoder and compared
// with what CMD_PROC_READ's own chunking returns for the same fake memory.

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "read_lz4.h"
#include "lz4.h"

static int failures;

#define CHECK(cond) do { \
    if (!(cond)) { \
        fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); \
        failures++; \
    } \
} while (0)

#define STATUS_OK 0x80000000u // CMD_SUCCESS

static uint64_t rng = 0x9E3779B97F4A7C15ull;
static uint32_t next_rand(void) {
    rng ^= rng << 13;
    rng ^= rng >> 7;
    rng ^= rng << 17;
    return (uint32_t)(rng >> 16);
}

static uint32_t get32(const uint8_t *p) {
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

// ---- Fake target memory -------------------------------------------------
// MEM_BASE..MEM_BASE+MEM_SIZE is mapped; inside it, [hole_lo, hole_hi) is
// unreadable. A read that touches a hole or leaves the mapping copies
// nothing and leaves the buffer as it was (zeroed by the caller), the same
// as a failing sys_proc_rw.

#define MEM_BASE 0x100000000ull
#define MEM_SIZE (6u << 20)

struct fake {
    uint8_t *mem;
    uint64_t hole_lo, hole_hi;
    // reads recorded by read_mem
    uint64_t read_addr[256];
    uint32_t read_len[256];
    int reads;
    // captured response
    uint8_t *wire;
    size_t wire_len, wire_cap;
    int sends;
    int fail_send_at; // 1-based send to fail, 0 = never
};

static void fake_read(void *ctx, uint64_t address, void *buf, uint32_t len) {
    struct fake *f = (struct fake *)ctx;
    if (f->reads < 256) {
        f->read_addr[f->reads] = address;
        f->read_len[f->reads] = len;
    }
    f->reads++;
    if (address < MEM_BASE || address + len > MEM_BASE + MEM_SIZE) return;
    if (address < f->hole_hi && address + len > f->hole_lo) return;
    memcpy(buf, f->mem + (address - MEM_BASE), len);
}

static int fake_send(void *ctx, const void *buf, uint32_t len) {
    struct fake *f = (struct fake *)ctx;
    f->sends++;
    if (f->fail_send_at && f->sends == f->fail_send_at) return -1;
    if (f->wire_len + len > f->wire_cap) {
        f->wire_cap = (f->wire_len + len) * 2;
        f->wire = realloc(f->wire, f->wire_cap);
    }
    memcpy(f->wire + f->wire_len, buf, len);
    f->wire_len += len;
    return 0;
}

static void fake_reset(struct fake *f) {
    f->reads = 0;
    f->wire_len = 0;
    f->sends = 0;
    f->fail_send_at = 0;
}

// What CMD_PROC_READ returns: proc_read_handle's chunk loop, verbatim.
static uint8_t *expected_proc_read(struct fake *f, uint64_t address, uint32_t length) {
    uint8_t *out = calloc(1, length ? length : 1);
    uint8_t *data = malloc(0x10000);
    size_t off = 0;
    if (length > 0) {
        uint64_t last_aligned = address + ((length - 1) & ~(uint64_t)0xFFFF);
        for (;;) {
            memset(data, 0, 0x10000);
            if (address == last_aligned) break;
            fake_read(f, address, data, 0x10000);
            memcpy(out + off, data, 0x10000);
            off += 0x10000;
            address += 0x10000;
        }
        uint64_t tail_size = length - ((length - 1) & ~(uint64_t)0xFFFF);
        fake_read(f, address, data, (uint32_t)tail_size);
        memcpy(out + off, data, tail_size);
    }
    free(data);
    return out;
}

// Decodes a whole response; returns the bytes (length bytes) or NULL if the
// stream breaks any rule of the wire format.
static uint8_t *decode_response(const uint8_t *w, size_t n, uint32_t length,
                                int *lz4_frames, int *stored_frames) {
    *lz4_frames = *stored_frames = 0;
    if (n < 8 || get32(w) != STATUS_OK || get32(w + 4) != READ_LZ4_BLOCK_SIZE) return NULL;
    uint8_t *out = malloc(length ? length : 1);
    size_t pos = 8;
    uint32_t done = 0;
    while (done < length) {
        uint32_t blen = length - done < READ_LZ4_BLOCK_SIZE ? length - done : READ_LZ4_BLOCK_SIZE;
        if (pos + 4 > n) goto bad;
        uint32_t hdr = get32(w + pos);
        uint32_t psize = hdr & READ_LZ4_SIZE_MASK;
        pos += 4;
        if (pos + psize > n) goto bad;
        if (hdr & READ_LZ4_STORED) {
            if (psize != blen) goto bad;
            memcpy(out + done, w + pos, blen);
            (*stored_frames)++;
        } else {
            if (psize == 0 || psize >= blen) goto bad;
            int d = LZ4_decompress_safe((const char *)w + pos, (char *)out + done, (int)psize, (int)blen);
            if (d != (int)blen) goto bad;
            (*lz4_frames)++;
        }
        pos += psize;
        done += blen;
    }
    if (pos != n) goto bad; // nothing after the last frame
    return out;
bad:
    free(out);
    return NULL;
}

static void *state;
static uint8_t *raw, *out;

// Runs one request through read_lz4_stream and checks it end to end.
static void check_request(struct fake *f, uint64_t address, uint32_t length,
                          int *lz4_frames, int *stored_frames) {
    uint8_t *expect = expected_proc_read(f, address, length);
    fake_reset(f);

    CHECK(read_lz4_stream(state, raw, out, STATUS_OK, address, length, fake_read, fake_send, f) == 0);

    // Same reads as CMD_PROC_READ: consecutive blocks from address.
    uint32_t nblocks = (length + READ_LZ4_BLOCK_SIZE - 1) / READ_LZ4_BLOCK_SIZE;
    CHECK(f->reads == (int)nblocks);
    for (int i = 0; i < f->reads && i < 256; i++) {
        CHECK(f->read_addr[i] == address + (uint64_t)i * READ_LZ4_BLOCK_SIZE);
        uint32_t want = (uint32_t)i + 1 < nblocks ? READ_LZ4_BLOCK_SIZE
                        : length - (nblocks - 1) * READ_LZ4_BLOCK_SIZE;
        CHECK(f->read_len[i] == want);
    }
    CHECK(f->sends == 1 + (int)nblocks); // preamble + one send per frame

    // Never more than 4 bytes over the raw size per block.
    CHECK(f->wire_len <= 8 + (size_t)length + 4u * nblocks);

    int lz, st;
    uint8_t *got = decode_response(f->wire, f->wire_len, length, &lz, &st);
    CHECK(got != NULL);
    if (got) {
        CHECK(memcmp(got, expect, length) == 0);
        free(got);
    }
    if (lz4_frames) *lz4_frames = lz;
    if (stored_frames) *stored_frames = st;
    free(expect);
}

static void test_request_ok(void) {
    struct read_lz4_request rq = { 1234, 0x400000, 0x10000, 0 };
    uint8_t buf[32] = {0};

    CHECK(read_lz4_request_ok(&rq, READ_LZ4_PACKET_SIZE));
    CHECK(!read_lz4_request_ok(NULL, READ_LZ4_PACKET_SIZE));
    CHECK(!read_lz4_request_ok(&rq, 0));
    CHECK(!read_lz4_request_ok(&rq, 16)); // a CMD_PROC_READ body
    CHECK(!read_lz4_request_ok(&rq, 19));
    memcpy(buf, &rq, sizeof(rq));
    CHECK(!read_lz4_request_ok(buf, 21)); // trailing bytes are not ignored
    rq.flags = 1;
    CHECK(!read_lz4_request_ok(&rq, READ_LZ4_PACKET_SIZE)); // reserved flags
    rq.flags = 0x80000000u;
    CHECK(!read_lz4_request_ok(&rq, READ_LZ4_PACKET_SIZE));

    // Wire layout: pid@0, address@4, length@12, flags@16, little-endian.
    const uint8_t wire[20] = { 0xD2, 0x04, 0, 0,  0, 0, 0x40, 0, 0, 0, 0, 0,
                               0, 0, 1, 0,  0, 0, 0, 0 };
    const struct read_lz4_request *w = (const struct read_lz4_request *)wire;
    CHECK(w->pid == 1234 && w->address == 0x400000 && w->length == 0x10000 && w->flags == 0);
    CHECK(read_lz4_request_ok(wire, 20));
}

// One block on its own: both frame kinds and their invariants.
static void test_frames(void) {
    uint8_t *dec = malloc(READ_LZ4_BLOCK_SIZE);
    uint32_t lens[] = { 1, 2, 3, 4, 5, 12, 13, 16, 31, 64, 255, 256, 4096, 65535, 65536 };

    for (int pattern = 0; pattern < 4; pattern++) {
        for (size_t k = 0; k < sizeof(lens) / sizeof(lens[0]); k++) {
            uint32_t len = lens[k];
            uint8_t *blk = raw + READ_LZ4_HDR_SIZE;
            for (uint32_t i = 0; i < len; i++) {
                switch (pattern) {
                case 0: blk[i] = 0; break;                        // zero page
                case 1: blk[i] = (uint8_t)next_rand(); break;      // packed asset
                case 2: blk[i] = (uint8_t)(i % 7 == 0 ? next_rand() : i); break; // mixed
                default: blk[i] = (uint8_t)(i & 0xF0); break;     // runs
                }
            }
            memcpy(dec, blk, len); // keep the input: a stored frame reuses raw
            uint32_t flen = 0;
            const uint8_t *fr = read_lz4_frame(state, raw, out, len, &flen);
            uint32_t hdr = get32(fr);
            uint32_t psize = hdr & READ_LZ4_SIZE_MASK;

            CHECK(flen == READ_LZ4_HDR_SIZE + psize);
            if (hdr & READ_LZ4_STORED) {
                CHECK(fr == raw);
                CHECK(psize == len);
                CHECK(memcmp(fr + 4, dec, len) == 0);
            } else {
                uint8_t *tmp = malloc(len);
                CHECK(fr == out);
                CHECK(psize > 0 && psize < len);
                CHECK(LZ4_decompress_safe((const char *)fr + 4, (char *)tmp, (int)psize, (int)len) == (int)len);
                CHECK(memcmp(tmp, dec, len) == 0);
                free(tmp);
            }

            // Pinned outcomes: a 1-byte block cannot shrink, random data is
            // stored, a zero page compresses to a few hundred bytes at most.
            if (len == 1) CHECK(hdr == (READ_LZ4_STORED | 1u));
            if (pattern == 1 && len >= 64) CHECK(hdr == (READ_LZ4_STORED | len));
            if (pattern == 0 && len == 65536) CHECK(!(hdr & READ_LZ4_STORED) && psize < 512);
        }
    }
    free(dec);
}

static void fill_memory(struct fake *f) {
    // 0-1 MiB: "code"-like bytes, 1-2 MiB: zeros, 2-3 MiB: random,
    // 3-6 MiB: structs with small counters and a few random fields.
    for (uint32_t i = 0; i < (1u << 20); i++) f->mem[i] = (uint8_t)((i * 31) ^ (i >> 5));
    memset(f->mem + (1u << 20), 0, 1u << 20);
    for (uint32_t i = 2u << 20; i < (3u << 20); i++) f->mem[i] = (uint8_t)next_rand();
    for (uint32_t i = 3u << 20; i < MEM_SIZE; i += 32) {
        uint32_t v[8] = { i, i / 32, 100, 0, 0, next_rand() & 0xFF, 0x3F800000u, 0 };
        memcpy(f->mem + i, v, sizeof(v));
    }
}

static void test_streams(struct fake *f) {
    int lz = 0, st = 0;

    // Empty read: only the preamble.
    fake_reset(f);
    CHECK(read_lz4_stream(state, raw, out, STATUS_OK, MEM_BASE, 0, fake_read, fake_send, f) == 0);
    CHECK(f->wire_len == 8 && f->reads == 0);
    CHECK(get32(f->wire) == STATUS_OK && get32(f->wire + 4) == READ_LZ4_BLOCK_SIZE);

    // Block-size edges.
    uint32_t lens[] = { 1, 7, 65535, 65536, 65537, 131072, 3 * 65536 + 123 };
    for (size_t k = 0; k < sizeof(lens) / sizeof(lens[0]); k++) {
        check_request(f, MEM_BASE, lens[k], NULL, NULL);
        check_request(f, MEM_BASE + 0x1003, lens[k], NULL, NULL); // unaligned start
    }

    // The zero MiB compresses, the random MiB is stored.
    check_request(f, MEM_BASE + (1u << 20), 1u << 20, &lz, &st);
    CHECK(lz == 16 && st == 0);
    fake_reset(f);
    check_request(f, MEM_BASE + (2u << 20), 1u << 20, &lz, &st);
    CHECK(lz == 0 && st == 16);

    // The whole mapping, both kinds mixed in one stream (state reused).
    check_request(f, MEM_BASE, MEM_SIZE, &lz, &st);
    CHECK(lz > 0 && st > 0 && lz + st == (int)(MEM_SIZE / READ_LZ4_BLOCK_SIZE));
    CHECK(f->wire_len < MEM_SIZE / 2); // only the random MiB is incompressible

    // Unreadable memory decodes as zeros, exactly like CMD_PROC_READ: a hole
    // inside random data, and a read running off the end of the mapping.
    f->hole_lo = MEM_BASE + (2u << 20) + 0x8000;
    f->hole_hi = f->hole_lo + 0x30000;
    check_request(f, MEM_BASE + (2u << 20), 0x80000, NULL, NULL);
    check_request(f, MEM_BASE + MEM_SIZE - 0x18000, 0x40000, NULL, NULL);
    check_request(f, 0x1000, 0x20000, &lz, &st); // nothing mapped at all
    CHECK(lz == 2 && st == 0);
    f->hole_lo = f->hole_hi = 0;

    // Random requests.
    for (int i = 0; i < 300; i++) {
        uint64_t a = MEM_BASE - 0x20000 + (next_rand() % (MEM_SIZE + 0x40000));
        uint32_t len = next_rand() % (i < 250 ? 0x30000u : 0x200000u);
        if (next_rand() & 1) {
            f->hole_lo = MEM_BASE + (next_rand() % MEM_SIZE);
            f->hole_hi = f->hole_lo + (next_rand() % 0x40000);
        } else {
            f->hole_lo = f->hole_hi = 0;
        }
        check_request(f, a, len, NULL, NULL);
    }
    f->hole_lo = f->hole_hi = 0;
}

// A failed send stops the stream: no more reads, -1 returned.
static void test_send_failure(struct fake *f) {
    fake_reset(f);
    f->fail_send_at = 1; // the preamble
    CHECK(read_lz4_stream(state, raw, out, STATUS_OK, MEM_BASE, 0x30000, fake_read, fake_send, f) == -1);
    CHECK(f->reads == 0);

    fake_reset(f);
    f->fail_send_at = 3; // the second frame
    CHECK(read_lz4_stream(state, raw, out, STATUS_OK, MEM_BASE, 0x50000, fake_read, fake_send, f) == -1);
    CHECK(f->reads == 2 && f->sends == 3);
}

static char *read_file(const char *path) {
    FILE *fp = fopen(path, "rb");
    if (!fp) return NULL;
    fseek(fp, 0, SEEK_END);
    long n = ftell(fp);
    fseek(fp, 0, SEEK_SET);
    char *buf = malloc((size_t)n + 1);
    if (buf && fread(buf, 1, (size_t)n, fp) != (size_t)n) { free(buf); buf = NULL; }
    if (buf) buf[n] = '\0';
    fclose(fp);
    return buf;
}

// proc.c must route the opcode to the handler.
static void test_dispatch(void) {
    char *src = read_file(PROC_C_PATH);
    CHECK(src != NULL);
    if (!src) return;
    CHECK(strstr(src, "case 0xBDAACC18u:             return proc_read_lz4_handle(fd, packet);") != NULL);
    free(src);
}

int main(void) {
    state = malloc(READ_LZ4_STATE_SIZE);
    raw = malloc(READ_LZ4_BUF_SIZE);
    out = malloc(READ_LZ4_BUF_SIZE);
    CHECK(LZ4_initStream(state, READ_LZ4_STATE_SIZE) != NULL);

    struct fake f;
    memset(&f, 0, sizeof(f));
    f.mem = malloc(MEM_SIZE);
    fill_memory(&f);

    test_request_ok();
    test_frames();
    test_streams(&f);
    test_send_failure(&f);
    test_dispatch();

    free(f.mem);
    free(f.wire);
    free(state);
    free(raw);
    free(out);

    if (failures) {
        fprintf(stderr, "%d test(s) failed\n", failures);
        return 1;
    }
    printf("test_read_lz4: all tests passed\n");
    return 0;
}
