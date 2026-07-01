// SPDX-License-Identifier: GPL-3.0-only

#include <ps4.h>
#include "kdbg.h"
#include "proc_ptwalk.h"
#include <stdint.h>

#define PROC_VMSPACE   0x168
#define PROC_THREADS   0x10
#define THREAD_PCB     0x388
#define PCB_CR3        0x68
#define VMSPACE_PML4   0x200

#define PTE_PRESENT     0x1ULL
#define PTE_PS          0x80ULL
#define PTE_PCD         0x10ULL
#define PHYS_MASK       0x000FFFFFFFFFF000ULL
#define PHYS_BOUND      0x1000000000ULL
#define SAFE_TABLE_BOUND 0x100000000ULL

#define CANON_KERN      0x1FFFFULL

extern uint64_t kern_get_proc(int pid);

static inline int is_kptr(uint64_t v) { return (v >> 48) == 0xFFFFu; }
static inline void kr(uint64_t a, void *b, uint64_t n)        { sys_kern_rw(a, b, n, 0); }
static inline uint64_t kr64(uint64_t a) { uint64_t v = 0; kr(a, &v, 8); return v; }

static int      g_state = 0;
static uint64_t g_dmap  = 0;
static volatile uint64_t g_gt = 0x5E7C0DE5E7C0DE71ULL;

static uint64_t pid_cr3(uint32_t pid) {
    uint64_t kproc = kern_get_proc((int)pid);
    if (!kproc) return 0;
    uint64_t vmspace = kr64(kproc + PROC_VMSPACE);
    if (!is_kptr(vmspace)) return 0;
    uint64_t pml4 = kr64(vmspace + VMSPACE_PML4);
    if (!is_kptr(pml4) || (pml4 & 0xFFF)) return 0;
    uint64_t cr3 = (pml4 - g_dmap) & 0xFFFFFFFFFFFFFFFFULL;
    if (cr3 == 0 || (cr3 & 0xFFF) || cr3 >= PHYS_BOUND) return 0;
    return cr3;
}

static int walk_leaf(uint64_t cr3, uint64_t va, uint64_t *out_e, int *out_level) {
    uint64_t table = cr3 & PHYS_MASK;
    for (int level = 0; level < 4; level++) {
        if (table >= SAFE_TABLE_BOUND) { *out_e = 0; *out_level = level; return 1; }
        uint64_t idx = (va >> (39 - level * 9)) & 0x1FF;
        uint64_t e = kr64(g_dmap + table + idx * 8);
        if (!(e & PTE_PRESENT)) { *out_e = e; *out_level = level; return 1; }
        if (level == 3) { *out_e = e; *out_level = 3; return 0; }
        if ((level == 1 || level == 2) && (e & PTE_PS)) { *out_e = e; *out_level = level; return 0; }
        table = e & PHYS_MASK;
    }
    *out_e = 0; *out_level = -1; return 1;
}

static int dmap_validates(uint64_t dmap, uint64_t cr3, uint64_t gt_va, uint64_t gt_val) {
    if ((dmap >> 47) != CANON_KERN) return 0;
    if (dmap & 0xFFFFFFFFULL)        return 0;
    uint64_t saved = g_dmap; g_dmap = dmap;
    uint64_t e = 0; int lvl = -1;
    int ok = (walk_leaf(cr3, gt_va, &e, &lvl) == 0);
    if (ok) {
        uint64_t pgmask = (lvl == 3) ? 0xFFFULL : (lvl == 2) ? 0x1FFFFFULL : 0x3FFFFFFFULL;
        uint64_t phys = ((e & PHYS_MASK) & ~pgmask) | (gt_va & pgmask);
        if (phys < SAFE_TABLE_BOUND && !(e & PTE_PCD)) {
            if (kr64(dmap + phys) != gt_val) ok = 0;
        }
    }
    g_dmap = saved;
    return ok;
}

int ptw_discover(void) {
    if (g_state) return g_state;
    int result = -1;

    int our_pid = (int)syscall(20);
    uint64_t kproc = kern_get_proc(our_pid);
    if (kproc) {
        uint64_t td      = kr64(kproc + PROC_THREADS);
        uint64_t vmspace = kr64(kproc + PROC_VMSPACE);
        uint64_t pcb     = is_kptr(td) ? kr64(td + THREAD_PCB) : 0;
        uint64_t cr3     = is_kptr(pcb) ? kr64(pcb + PCB_CR3) : 0;
        uint64_t gt_va   = (uint64_t)(uintptr_t)&g_gt;
        uint64_t gt_val  = g_gt;

        if (is_kptr(vmspace) && cr3 && !(cr3 & 0xFFF) && cr3 < PHYS_BOUND) {

            uint64_t pml4 = kr64(vmspace + VMSPACE_PML4);
            if (is_kptr(pml4) && !(pml4 & 0xFFF)) {
                uint64_t dmap = (pml4 - cr3) & 0xFFFFFFFFFFFFFFFFULL;
                if (dmap_validates(dmap, cr3, gt_va, gt_val)) { g_dmap = dmap; result = 1; }
            }

            if (result != 1) {
                uint8_t vbuf[0x400];
                kr(vmspace, vbuf, sizeof(vbuf));
                for (int o = 0; o + 8 <= (int)sizeof(vbuf); o += 8) {
                    uint64_t P = *(uint64_t *)(vbuf + o);
                    if (!is_kptr(P) || (P & 0xFFF)) continue;
                    uint64_t dmap = (P - cr3) & 0xFFFFFFFFFFFFFFFFULL;
                    if ((dmap >> 47) != CANON_KERN || (dmap & 0xFFFFFFFFULL)) continue;
                    if (dmap_validates(dmap, cr3, gt_va, gt_val)) { g_dmap = dmap; result = 1; break; }
                }
            }
        }
    }

    g_state = result;
    return result;
}

uint64_t proc_ptwalk_dmap_base(void) { return g_dmap; }

int proc_ptwalk_leaf_addr(uint32_t pid, uint64_t va, uint64_t *out_pte_kaddr,
                          uint64_t *out_pte_val, int *out_level) {
    if ((int32_t)pid <= 0)   return 1;
    if (ptw_discover() != 1) return 1;
    uint64_t cr3 = pid_cr3(pid);
    if (!cr3) return 1;

    uint64_t table = cr3 & PHYS_MASK;
    for (int level = 0; level < 4; level++) {
        if (table >= SAFE_TABLE_BOUND) return 1;
        uint64_t idx       = (va >> (39 - level * 9)) & 0x1FF;
        uint64_t ent_kaddr = g_dmap + table + idx * 8;
        uint64_t e         = kr64(ent_kaddr);
        if (level == 3) {
            if (out_pte_kaddr) *out_pte_kaddr = ent_kaddr;
            if (out_pte_val)   *out_pte_val   = e;
            if (out_level)     *out_level     = 3;
            return 0;
        }
        if (!(e & PTE_PRESENT)) return 2;
        if ((level == 1 || level == 2) && (e & PTE_PS)) return 3;
        table = e & PHYS_MASK;
    }
    return 1;
}

int proc_ptwalk_span_resolve(uint32_t pid, uint64_t span2m, int *out_huge,
                             uint64_t *out_phys_base, uint64_t *out_leaf_pt_kaddr,
                             uint64_t *out_pte) {
    if ((int32_t)pid <= 0)   return 1;
    if (ptw_discover() != 1) return 1;
    uint64_t cr3 = pid_cr3(pid);
    if (!cr3) return 1;

    uint64_t table = cr3 & PHYS_MASK;
    for (int level = 0; level < 3; level++) {
        if (table >= SAFE_TABLE_BOUND) return 1;
        uint64_t idx = (span2m >> (39 - level * 9)) & 0x1FF;
        uint64_t e   = kr64(g_dmap + table + idx * 8);
        if (!(e & PTE_PRESENT)) return 1;
        if ((level == 1 || level == 2) && (e & PTE_PS)) {
            uint64_t pgsz = (level == 1) ? (1ULL << 30) : (1ULL << 21);
            uint64_t base = (e & PHYS_MASK) & ~(pgsz - 1);
            if (out_huge)          *out_huge          = 1;
            if (out_phys_base)     *out_phys_base     = base + (span2m & (pgsz - 1));
            if (out_pte)           *out_pte           = e;
            if (out_leaf_pt_kaddr) *out_leaf_pt_kaddr = 0;
            return 0;
        }
        table = e & PHYS_MASK;
    }
    if (table >= SAFE_TABLE_BOUND) return 1;
    if (out_huge)          *out_huge          = 0;
    if (out_leaf_pt_kaddr) *out_leaf_pt_kaddr = g_dmap + table;
    if (out_phys_base)     *out_phys_base     = 0;
    if (out_pte)           *out_pte           = 0;
    return 0;
}

int proc_ptwalk_probe(uint32_t pid, uint64_t va, uint64_t *out_phys,
                      int *out_level, uint64_t *out_pagesize, uint64_t *out_pte) {
    if ((int32_t)pid <= 0)   return 1;
    if (ptw_discover() != 1) return 1;
    uint64_t cr3 = pid_cr3(pid);
    if (!cr3) return 1;

    uint64_t e = 0; int lvl = -1;
    if (walk_leaf(cr3, va, &e, &lvl) != 0) return 1;
    if (!(e & PTE_PRESENT)) return 1;

    uint64_t page_size = (lvl == 3) ? 0x1000ULL
                       : (lvl == 2) ? 0x200000ULL
                       : (lvl == 1) ? 0x40000000ULL : 0;
    if (!page_size) return 1;

    uint64_t page_mask = page_size - 1;
    if (out_phys)     *out_phys     = ((e & PHYS_MASK) & ~page_mask) + (va & page_mask);
    if (out_level)    *out_level    = lvl;
    if (out_pagesize) *out_pagesize = page_size;
    if (out_pte)      *out_pte      = e;
    return 0;
}
