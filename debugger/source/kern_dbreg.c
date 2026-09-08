// SPDX-License-Identifier: GPL-3.0-only

#include <ps4.h>
#include "kdbg.h"
#include "fw.h"
#include <stdint.h>

#define PROC_NEXT       0x00
#define PROC_PID        0xB0
#define PROC_THREADS    0x10
#define THREAD_NEXT     0x10
#define THREAD_TID      0x88
#define THREAD_PCB      0x388
#define PCB_FSBASE      0x40
#define PCB_SAVE        0xE8

#define FSGS_BLOB       0x10
#define XSAVE_BLOB      0x340

static inline int is_kptr(uint64_t v) { return (v >> 48) == 0xFFFFu; }

static inline void kr_read(uint64_t addr, void *buf, uint64_t len) {
    sys_kern_rw(addr, buf, len, 0);
}
static inline void kr_write(uint64_t addr, const void *buf, uint64_t len) {
    sys_kern_rw(addr, (void *)buf, len, 1);
}

static uint64_t allproc_offset(void) {
    switch (get_fw_version()) {
    case 505: case 507:                         return 0x2382FF8;
    case 671: case 672:                         return 0x22BBE80;
    case 700: case 701: case 702:               return 0x1B48318;
    case 750: case 751: case 755:               return 0x213C828;
    case 800: case 801: case 803:               return 0x1B244E0;
    case 850: case 852:                         return 0x1BD72D8;
    case 900:                                   return 0x1B946E0;
    case 903: case 904:                         return 0x1B906E0;
    case 950: case 951: case 960:               return 0x221D2A0;
    case 1000: case 1001: case 1002:            return 0x22D9B40;
    case 1050: case 1051: case 1070: case 1071: return 0x2269F30;
    case 1100:                                  return 0x22D0A98;
    case 1102:                                  return 0x22D0A98;
    case 1150: case 1152:                       return 0x1B28538;
    case 1200: case 1202:                       return 0x1B28538;
    case 1250: case 1252:                       return 0x1B28538;
    case 1300:                                  return 0x1B28538;
    case 1302: case 1304:                       return 0x1B28538;
    case 1350: case 1352:                       return 0x1B28538;
    default:                                    return 0;
    }
}

uint64_t kern_get_proc(int pid) {
    uint64_t off = allproc_offset();
    if (!off) return 0;

    uint64_t kbase = 0;
    sys_kern_base(&kbase);
    if (!kbase) return 0;

    uint64_t proc = 0;
    kr_read(kbase + off, &proc, 8);

    int guard = 0;
    while (is_kptr(proc) && guard++ < 4096) {
        int cpid = -1;
        kr_read(proc + PROC_PID, &cpid, 4);
        if (cpid == pid) return proc;
        uint64_t next = 0;
        kr_read(proc + PROC_NEXT, &next, 8);
        proc = next;
    }
    return 0;
}

static uint64_t kern_thread_addr(int pid, int lwpid) {
    uint64_t proc = kern_get_proc(pid);
    if (!proc) return 0;

    uint64_t td = 0;
    kr_read(proc + PROC_THREADS, &td, 8);
    int g2 = 0;
    while (is_kptr(td) && g2++ < 4096) {
        int ctid = -1;
        kr_read(td + THREAD_TID, &ctid, 4);
        if (ctid == lwpid) return td;
        uint64_t next = 0;
        kr_read(td + THREAD_NEXT, &next, 8);
        td = next;
    }
    return 0;
}

static uint64_t pcb_of(int pid, int lwpid) {
    uint64_t td = kern_thread_addr(pid, lwpid);
    if (!td) return 0;
    uint64_t pcb = 0;
    kr_read(td + THREAD_PCB, &pcb, 8);
    return is_kptr(pcb) ? pcb : 0;
}

int kern_get_fsgsbase(int pid, int lwpid, void *out16) {
    if (pid == 0 || !out16) return 1;
    uint64_t pcb = pcb_of(pid, lwpid);
    if (!pcb) return 6;
    kr_read(pcb + PCB_FSBASE, out16, FSGS_BLOB);
    return 0;
}

int kern_set_fsgsbase(int pid, int lwpid, const void *in16) {
    if (pid == 0 || !in16) return 1;
    uint64_t pcb = pcb_of(pid, lwpid);
    if (!pcb) return 6;
    kr_write(pcb + PCB_FSBASE, in16, FSGS_BLOB);
    return 0;
}

int kern_get_fpregs(int pid, int lwpid, void *fpu_buf) {
    if (pid == 0 || !fpu_buf) return 1;
    uint64_t pcb = pcb_of(pid, lwpid);
    if (!pcb) return 6;
    uint64_t save = 0;
    kr_read(pcb + PCB_SAVE, &save, 8);
    if (!is_kptr(save)) return 1;
    kr_read(save, fpu_buf, XSAVE_BLOB);
    return 0;
}

int kern_set_fpregs(int pid, int lwpid, const void *fpu_buf) {
    if (pid == 0 || !fpu_buf) return 1;
    uint64_t pcb = pcb_of(pid, lwpid);
    if (!pcb) return 6;
    uint64_t save = 0;
    kr_read(pcb + PCB_SAVE, &save, 8);
    if (!is_kptr(save)) return 1;
    kr_write(save, fpu_buf, XSAVE_BLOB);
    return 0;
}
