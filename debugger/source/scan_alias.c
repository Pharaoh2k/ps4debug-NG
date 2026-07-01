// SPDX-License-Identifier: GPL-3.0-only

#include <ps4.h>
#include "scan_alias.h"
#include "proc_ptwalk.h"
#include "kdbg.h"
#include <stdint.h>

#define SA_SPAN        0x200000ULL
#define SA_PT_ENTRIES  512u
#define SA_TOUCH_IDX   511u
#define SA_PAGE        0x1000ULL
#define SA_ARENA_DEF   (64ULL << 20)

#define SA_PHYS_MASK   0x000FFFFFFFFFF000ULL
#define SA_DMAP_BOUND  0x1000000000ULL
#define SA_PTE_PRESENT 0x1ULL
#define SA_PTE_PCD     0x10ULL
#define SA_PTE_ALIAS   0x7ULL
#define SA_MAX_PAGES   511u
#define SA_VERIFY_MAX  32u
#define SA_MAX_SPANS   128u

#define SA_VERIFY_IDX  384u
#define SA_VERIFY_DIV  16

static inline int sa_kr(uint64_t kaddr, void *buf, size_t len) { sys_kern_rw(kaddr, buf, len, 0); return 0; }
static inline int sa_kw(uint64_t kaddr, const void *buf, size_t len) { sys_kern_rw(kaddr, (void *)buf, len, 1); return 0; }

static long sa_mmap(uint64_t len) {
    return syscall(477, 0L, (long)len, 3L , 0x1002L , -1L, 0L);
}
static void sa_munmap(uint64_t addr, uint64_t len) {
    syscall(73, (long)addr, (long)len);
}

struct scan_alias_ctx {
    uint32_t pid;
    uint32_t self_pid;
    uint64_t map_seq;
    uint64_t dmap;

    uint64_t arena_cap;
    uint64_t arena_raw;
    uint64_t arena_mapsz;
    uint64_t arena_end;
    uint64_t cursor;
    uint64_t used_pt[SA_MAX_SPANS];
    uint64_t used_count;

    int      map_active;
    uint64_t map_pt_base;
    uint64_t map_npages;

    int      sc_valid;
    uint64_t sc_span;
    int      sc_huge;
    uint64_t sc_phys_base;
    uint64_t sc_pte;
    uint64_t span_pt[SA_PT_ENTRIES];
};

static void sa_reclaim_or_leak(scan_alias_ctx *c);

static int sa_arena_alloc(scan_alias_ctx *c) {
    uint64_t mapsz = c->arena_cap + SA_SPAN;
    long raw = sa_mmap(mapsz);
    if (raw == -1 || raw == 0) return -1;
    uint64_t base = ((uint64_t)raw + (SA_SPAN - 1)) & ~(SA_SPAN - 1);
    c->arena_raw   = (uint64_t)raw;
    c->arena_mapsz = mapsz;
    c->arena_end   = base + c->arena_cap;
    c->cursor      = base;
    c->used_count  = 0;
    return 0;
}

static int sa_next_span(scan_alias_ctx *c, uint64_t *out_span) {
    if (c->cursor == 0 || c->cursor + SA_SPAN > c->arena_end) {
        if (c->arena_raw) sa_reclaim_or_leak(c);
        if (sa_arena_alloc(c) != 0) return -1;
    }
    *out_span = c->cursor;
    c->cursor += SA_SPAN;
    return 0;
}

scan_alias_ctx *scan_alias_begin(uint32_t pid, uint64_t arena_cap) {
    if (ptw_discover() != 1) return NULL;
    uint64_t dmap = proc_ptwalk_dmap_base();
    if (dmap == 0) return NULL;
    scan_alias_ctx *c = (scan_alias_ctx *)malloc(sizeof(*c));
    if (!c) return NULL;
    memset(c, 0, sizeof(*c));
    c->pid       = pid;
    c->self_pid  = (uint32_t)syscall(20);
    c->dmap      = dmap;
    c->arena_cap = arena_cap ? arena_cap : SA_ARENA_DEF;
    if (sa_arena_alloc(c) != 0) { free(c); return NULL; }
    return c;
}

void scan_alias_release(scan_alias_ctx *c) {
    if (!c || !c->map_active) return;
    uint64_t zeros[SA_MAX_PAGES];
    memset(zeros, 0, c->map_npages * 8);
    sa_kw(c->map_pt_base, zeros, (size_t)(c->map_npages * 8));
    c->map_active = 0;
    c->map_npages = 0;
    c->map_pt_base = 0;
}

static void sa_reclaim_or_leak(scan_alias_ctx *c) {
    if (c->arena_raw == 0) return;
    if (c->map_active) scan_alias_release(c);

    int clean = (c->used_count <= SA_MAX_SPANS);
    for (uint64_t i = 0; clean && i < c->used_count; i++) {
        uint64_t pt[SA_PT_ENTRIES];
        if (sa_kr(c->used_pt[i], pt, sizeof(pt)) != 0) { clean = 0; break; }
        for (unsigned k = 0; k < SA_VERIFY_IDX; k++)
            if (pt[k] & SA_PTE_PRESENT) { clean = 0; break; }
    }

    if (clean)
        sa_munmap(c->arena_raw, c->arena_mapsz);

    c->arena_raw = 0; c->arena_mapsz = 0; c->arena_end = 0; c->cursor = 0; c->used_count = 0;
}

const void *scan_alias_map(scan_alias_ctx *c, uint64_t tgt, uint64_t len,
                           uint64_t *out_mapped_len) {
    if (!c || len == 0) return NULL;
    if (c->map_active) scan_alias_release(c);

    uint64_t tgt_pg = tgt & ~(SA_PAGE - 1);
    uint64_t off    = tgt & (SA_PAGE - 1);
    uint64_t npages = (off + len + (SA_PAGE - 1)) >> 12;
    if (npages == 0 || npages > SA_MAX_PAGES) return NULL;

    uint64_t span;
    if (sa_next_span(c, &span) != 0) return NULL;

    *(volatile uint8_t *)(uintptr_t)(span + (uint64_t)SA_TOUCH_IDX * SA_PAGE) = 0;

    uint64_t pt_base = 0, pt_val = 0; int pt_lv = -1;
    if (proc_ptwalk_leaf_addr(c->self_pid, span, &pt_base, &pt_val, &pt_lv) != 0)
        return NULL;
    if (pt_lv != 3) return NULL;
    if (pt_base < c->dmap || pt_base >= c->dmap + SA_DMAP_BOUND)
        return NULL;

    uint64_t pt[SA_PT_ENTRIES];
    if (sa_kr(pt_base, pt, sizeof(pt)) != 0) return NULL;

    uint64_t vw[SA_VERIFY_MAX], vt[SA_VERIFY_MAX]; unsigned ns = 0;

    for (uint64_t k = 0; k < npages; k++) {
        uint64_t tpage  = tgt_pg + k * SA_PAGE;
        uint64_t span2m = tpage & ~(SA_SPAN - 1);
        uint64_t tphys;
        int      newly_resolved = 0;

        if (!c->sc_valid || c->sc_span != span2m) {
            int huge = 0; uint64_t pbase = 0, leaf_kaddr = 0, pte = 0;
            if (proc_ptwalk_span_resolve(c->pid, span2m, &huge, &pbase, &leaf_kaddr, &pte) != 0)
                return NULL;
            if (huge) {
                if (pte & SA_PTE_PCD) return NULL;
                c->sc_huge = 1; c->sc_phys_base = pbase; c->sc_pte = pte;
            } else {
                if (sa_kr(leaf_kaddr, c->span_pt, sizeof(c->span_pt)) != 0)
                    return NULL;
                c->sc_huge = 0;
            }
            c->sc_span = span2m; c->sc_valid = 1;
            newly_resolved = 1;
        }

        if (c->sc_huge) {
            tphys = c->sc_phys_base + (tpage - span2m);
        } else {
            uint64_t e = c->span_pt[(tpage >> 12) & 0x1FF];
            if (!(e & SA_PTE_PRESENT)) return NULL;
            if (e & SA_PTE_PCD)        return NULL;
            tphys = e & SA_PHYS_MASK;
        }

        if (pt[k] & SA_PTE_PRESENT) return NULL;
        pt[k] = (tphys & SA_PHYS_MASK) | SA_PTE_ALIAS;

        if ((newly_resolved || k == 0 || k == npages - 1) && ns < SA_VERIFY_MAX) {
            vw[ns] = span + k * SA_PAGE;
            vt[ns] = tpage;
            ns++;
        }
    }

    if ((pt_base & 0xFFFULL) != 0 || npages > SA_MAX_PAGES) return NULL;

    if (sa_kw(pt_base, pt, (size_t)(npages * 8)) != 0) {
        uint64_t zeros[SA_MAX_PAGES];
        memset(zeros, 0, (size_t)(npages * 8));
        sa_kw(pt_base, zeros, (size_t)(npages * 8));
        return NULL;
    }
    c->map_active  = 1;
    c->map_pt_base = pt_base;
    c->map_npages  = npages;

    if (c->used_count < SA_MAX_SPANS) c->used_pt[c->used_count] = pt_base;
    c->used_count++;

    int do_verify = ((c->map_seq % SA_VERIFY_DIV) == 0);
    c->map_seq++;
    if (do_verify) {
        for (unsigned s = 0; s < ns; s++) {
            uint8_t a[64], b[64];
            memcpy(a, (const void *)(uintptr_t)vw[s], sizeof(a));
            memset(b, 0, sizeof(b));
            sys_proc_rw(c->pid, vt[s], b, sizeof(b), 0);
            if (memcmp(a, b, sizeof(a)) != 0) {
                scan_alias_release(c);
                return NULL;
            }
        }
    }

    if (out_mapped_len) *out_mapped_len = len;
    return (const void *)(uintptr_t)(span + off);
}

void scan_alias_end(scan_alias_ctx *c) {
    if (!c) return;
    scan_alias_release(c);
    sa_reclaim_or_leak(c);
    free(c);
}

void scan_alias_rebind(scan_alias_ctx *c, uint32_t pid) {
    if (!c) return;
    if (c->map_active) scan_alias_release(c);
    c->pid      = pid;
    c->sc_valid = 0;
}
