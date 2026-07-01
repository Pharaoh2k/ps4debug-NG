// SPDX-License-Identifier: GPL-3.0-only

#include "debug.h"
int g_debugging;
struct server_client *curdbgcli;
struct debug_context *curdbgctx;

ScePthreadMutex g_debug_mutex;
int g_stepping_lwpid;

int connect_debugger(struct debug_context *dbgctx, struct sockaddr_in *client) {
    struct sockaddr_in server;
    int r;

    g_debugging = 1;

    server.sin_len = sizeof(server);
    server.sin_family = AF_INET;
    server.sin_addr.s_addr = client->sin_addr.s_addr;
    server.sin_port = sceNetHtons(DEBUG_PORT);
    memset(server.sin_zero, NULL, sizeof(server.sin_zero));

    dbgctx->dbgfd = sceNetSocket("interrupt", AF_INET, SOCK_STREAM, 0);
    if(dbgctx->dbgfd <= 0) {
        return 1;
    }

    int flag = 1;
    sceNetSetsockopt(dbgctx->dbgfd, IPPROTO_TCP, TCP_NODELAY, (char *)&flag, sizeof(flag));

    r = sceNetConnect(dbgctx->dbgfd, (struct sockaddr *)&server, sizeof(server));
    if(r) {
        return 1;
    }

    return 0;
}

void debug_cleanup(struct debug_context *dbgctx) {

    if (curdbgcli) {
        curdbgcli->debugging = 0;
    }
    g_debugging = 0;
    curdbgcli = NULL;
    curdbgctx = NULL;

    if (dbgctx && dbgctx->dbgfd > 0) {
        sceNetSocketClose(dbgctx->dbgfd);
        dbgctx->dbgfd = 0;
    }
}
#define PT_CONTINUE     7
#define PT_STEP         9
#define PT_ATTACH       10
#define PT_DETACH       11
#define PT_GETNUMLWPS   14
#define PT_GETLWPLIST   15
#define PT_SUSPEND      18
#define PT_RESUME       19
#define PT_GETREGS      33
#define PT_SETREGS      34
#define PT_GETFPREGS    35
#define PT_SETFPREGS    36
#define PT_GETDBREGS    37
#define PT_SETDBREGS    38

#define MAX_BPS 30
struct debug_bp {
    uint64_t saved_byte;
    uint32_t enabled;
    uint32_t _pad;
    uint64_t address;
};

static struct {
    uint32_t pid;
    uint32_t attached;
    struct debug_bp  bp[MAX_BPS];
    struct __dbreg64 dbreg;
} g_debug_ctx;

static int kern_ptrace(int op, int pid, void *addr, int data) {
    errno = 0;
    return (int)syscall(26, op, pid, addr, data);
}

static inline int ptrace_ok(int ret) {
    return ret != -1 || errno == 0;
}

extern int kern_get_fsgsbase(int pid, int lwpid, void *out16);
extern int kern_set_fsgsbase(int pid, int lwpid, const void *in16);
extern int kern_get_fpregs(int pid, int lwpid, void *fpu_buf);
extern int kern_set_fpregs(int pid, int lwpid, const void *fpu_buf);

int g_pending_sig_pid;
int g_pending_signal;
long g_last_alive_check;

#define BP_DISABLE_VERIFY_MAX 16
static uint64_t g_recently_disabled_bps[BP_DISABLE_VERIFY_MAX];
static int      g_recently_disabled_count = 0;

static int bp_count_stuck_and_rewind(const uint64_t *bp_addrs, int n_bps,
                                     const char *tag) {
    int stuck = 0;
    if (n_bps <= 0) return 0;

    int numlwps = kern_ptrace(PT_GETNUMLWPS, (int)g_debug_ctx.pid, 0, 0);
    if (numlwps <= 0) return 0;

    uint32_t *lwpids = (uint32_t *)net_alloc_buffer((size_t)numlwps * sizeof(uint32_t));
    if (lwpids && kern_ptrace(PT_GETLWPLIST, (int)g_debug_ctx.pid, lwpids, numlwps) > 0) {
        struct __reg64 regs;
        for (int i = 0; i < numlwps; i++) {
            errno = 0;
            if (kern_ptrace(PT_GETREGS, (int)lwpids[i], &regs, 0) == -1) continue;
            for (int b = 0; b < n_bps; b++) {
                uint64_t a = bp_addrs[b];
                if (regs.r_rip == a + 1) {
                    regs.r_rip = a;
                    kern_ptrace(PT_SETREGS, (int)lwpids[i], &regs, 0);
                    uprintf("[%s] LWP %u rewound 0x%lx->0x%lx",
                            tag, lwpids[i],
                            (unsigned long)(a + 1), (unsigned long)a);
                    stuck++;
                    break;
                } else if (regs.r_rip == a) {
                    stuck++;
                    break;
                }
            }
        }
    }
    if (lwpids) free(lwpids);
    return stuck;
}

static void bp_resume_n(int n, const char *tag) {
    if (n <= 0) return;
    for (int i = 0; i < n && i < 256; i++) {
        kern_ptrace(PT_CONTINUE, (int)g_debug_ctx.pid, (void *)1, 0);
    }
    uprintf("[%s] resumed %d stuck LWPs", tag, n);
}

static int bp_iterative_sweep(const uint64_t *bp_addrs, int n_bps,
                              const char *tag) {
    int total_resumed = 0;
    for (int round = 0; round < 8; round++) {
        int stuck = bp_count_stuck_and_rewind(bp_addrs, n_bps, tag);
        if (stuck == 0) break;
        bp_resume_n(stuck, tag);
        total_resumed += stuck;
    }
    return total_resumed;
}

static void bp_disable_verify_sweep(void) {
    if (g_recently_disabled_count <= 0) return;
    bp_iterative_sweep(g_recently_disabled_bps,
                       g_recently_disabled_count,
                       "bp-disable-rescue");
    g_recently_disabled_count = 0;
}

int debug_process_stop_handle(int fd, struct cmd_packet *packet) {
    if (!packet->data) {

        net_send_int32(fd, CMD_DATA_NULL);
        return 1;
    }

    uint8_t *p = (uint8_t *)packet->data;
    uint32_t pid = *(uint32_t *)p;
    uint8_t  state = p[4];

    if (state > 2 || pid == 0) {

        net_send_int32(fd, CMD_ERROR);
        return 1;
    }

    if (g_debugging) {

        g_pending_sig_pid = (int)pid;
        if      (state == 0) g_pending_signal = 0;
        else if (state == 1) g_pending_signal = 17;
        else                 g_pending_signal = 9;
    } else {

        int sig;
        if      (state == 0) sig = 19;
        else if (state == 1) sig = 17;
        else                 sig = 9;
        kill((int)pid, sig);
    }

    net_send_int32(fd, CMD_SUCCESS);
    return 0;
}

int debug_attach_handle(int fd, struct cmd_packet *packet) {
    (void)packet;
    net_send_int32(fd, CMD_ERROR);
    return 1;
}

int debug_attach_handle_svc(struct server_client *svc, struct cmd_packet *packet) {
    int fd = svc->fd;

    if (g_debug_ctx.attached) {
        net_send_int32(fd, CMD_ALREADY_DEBUG);
        return 1;
    }

    if (!packet->data) {
        net_send_int32(fd, CMD_DATA_NULL);
        return 1;
    }
    uint32_t pid = *(uint32_t *)packet->data;

    if (kern_ptrace(PT_ATTACH, (int)pid, 0, 0) < 0) {
        net_send_int32(fd, CMD_ERROR);
        return 1;
    }

    {
        int attach_status = 0;
        int spins = 0;
        while (wait4((int)pid, &attach_status, 1 , NULL) <= 0) {
            if (++spins > 200) break;
            sceKernelUsleep(1000);
        }
    }

    if (kern_ptrace(PT_CONTINUE, (int)pid, (void *)1, 0) < 0) {
        net_send_int32(fd, CMD_ERROR);
        return 1;
    }

    struct sockaddr_in client_for_events;
    memset(&client_for_events, 0, sizeof(client_for_events));
    client_for_events.sin_len    = sizeof(client_for_events);
    client_for_events.sin_family = AF_INET;
    if (packet->datalen >= CMD_DEBUG_ATTACH_PACKET_SIZE) {
        struct cmd_debug_attach_packet *ap =
            (struct cmd_debug_attach_packet *)packet->data;
        client_for_events.sin_addr.s_addr = ap->client_ip;
    } else {
        client_for_events.sin_addr.s_addr = svc->client.sin_addr.s_addr;
    }

    memset(&svc->dbgctx, 0, sizeof(svc->dbgctx));
    if (connect_debugger(&svc->dbgctx, &client_for_events) != 0) {
        net_send_int32(fd, CMD_ERROR);
        return 1;
    }

    memset(&g_debug_ctx, 0, sizeof(g_debug_ctx));
    g_debug_ctx.pid      = pid;
    g_debug_ctx.attached = 1;

    svc->dbgctx.pid  = (int)pid;
    svc->debugging   = 1;
    curdbgcli        = svc;
    curdbgctx        = &svc->dbgctx;

    net_send_int32(fd, CMD_SUCCESS);
    return 0;
}

void debug_full_teardown(struct server_client *svc) {
    if (!g_debug_ctx.attached) {
        return;
    }

    struct sys_proc_vm_map_args vm_probe;
    memset(&vm_probe, 0, sizeof(vm_probe));
    int alive = (sys_proc_cmd(g_debug_ctx.pid, SYS_PROC_VM_MAP, &vm_probe) == 0);

    if (alive) {

        uint64_t enabled_addrs[MAX_BPS];
        int n_enabled = 0;
        for (int b = 0; b < MAX_BPS; b++) {
            if (g_debug_ctx.bp[b].enabled) {
                enabled_addrs[n_enabled++] = g_debug_ctx.bp[b].address;
            }
        }

        for (int b = 0; b < MAX_BPS; b++) {
            if (g_debug_ctx.bp[b].enabled) {
                sys_proc_rw(g_debug_ctx.pid, g_debug_ctx.bp[b].address,
                            &g_debug_ctx.bp[b].saved_byte, 1, 1);
            }
        }

        bp_iterative_sweep(enabled_addrs, n_enabled, "detach-rescue");

        int count = kern_ptrace(PT_GETNUMLWPS, g_debug_ctx.pid, 0, 0);
        if (count > 0) {
            uint32_t *lwpids = (uint32_t *)net_alloc_buffer((size_t)count * sizeof(uint32_t));
            if (lwpids && kern_ptrace(PT_GETLWPLIST, g_debug_ctx.pid, lwpids, count) != -1) {
                struct __dbreg64 zero = {0};
                for (int i = 0; i < count; i++) {
                    kern_ptrace(PT_SETDBREGS, (int)lwpids[i], &zero, 0);
                }
            }
            if (lwpids) free(lwpids);
        }

        kern_ptrace(PT_DETACH, g_debug_ctx.pid, 0, 0);
    }

    if (svc && svc->dbgctx.dbgfd > 0) {
        debug_cleanup(&svc->dbgctx);
    } else if (curdbgctx) {
        debug_cleanup(curdbgctx);
    }

    memset(&g_debug_ctx, 0, sizeof(g_debug_ctx));
}

int debug_detach_handle(int fd, struct cmd_packet *packet) {
    if (!g_debug_ctx.attached) {
        net_send_int32(fd, CMD_ERROR);
        return 1;
    }
    debug_full_teardown(curdbgcli);
    net_send_int32(fd, CMD_SUCCESS);
    return 0;
}

int dispatch_debug_events(void) {
    struct server_client *svc = curdbgcli;

    if (!svc || !svc->debugging || svc->dbgctx.dbgfd <= 0) {
        return 0;
    }
    if (!g_debug_ctx.attached) {
        return 0;
    }

    if (g_pending_sig_pid != 0) {
        kern_ptrace(PT_CONTINUE, g_pending_sig_pid, (void *)1, g_pending_signal);
        g_pending_sig_pid = 0;
        g_pending_signal = 0;
        bp_disable_verify_sweep();
    }

    SceKernelTimeval now;
    sceKernelGettimeofday(&now);
    if (((uint32_t)now.tv_sec - (uint32_t)g_last_alive_check) > 4) {
        struct sys_proc_vm_map_args vm_probe;
        memset(&vm_probe, 0, sizeof(vm_probe));
        if (sys_proc_cmd(g_debug_ctx.pid, SYS_PROC_VM_MAP, &vm_probe) != 0) {
            return 1;
        }
        g_last_alive_check = (long)now.tv_sec;
    }

    int status = 0;
    int r = wait4((int)g_debug_ctx.pid, &status, 1 , NULL);
    if (r <= 0) {
        return 0;
    }

    uint8_t sig = (uint8_t)((status >> 8) & 0xFF);

    if (sig == 17) {
        return 0;
    }

    if (sig == 9) {
        debug_full_teardown(svc);
        kern_ptrace(PT_CONTINUE, (int)svc->dbgctx.pid, (void *)1, 9);
        return 0;
    }

    struct debug_interrupt_packet pkt;
    memset(&pkt, 0, sizeof(pkt));

    uint8_t lwpi[152];
    errno = 0;
    kern_ptrace(13 , (int)g_debug_ctx.pid, lwpi, (int)sizeof(lwpi));
    if (errno != 0) {
        return 0;
    }

    uint32_t lwpid = *(uint32_t *)lwpi;
    pkt.lwpid  = lwpid;
    pkt.status = (uint32_t)status;
    memcpy(pkt.tdname, lwpi + 0x80, sizeof(pkt.tdname));

    kern_ptrace(PT_GETREGS,   (int)lwpid, &pkt.reg64,   0);
    kern_ptrace(PT_GETFPREGS, (int)lwpid, &pkt.savefpu, 0);
    kern_ptrace(PT_GETDBREGS, (int)lwpid, &pkt.dbreg64, 0);

    int matched_wp = -1;
    for (int i = 0; i < 4; i++) {
        if (pkt.dbreg64.dr[i] != 0 && pkt.reg64.r_rip == pkt.dbreg64.dr[i]) {
            matched_wp = i;
            break;
        }
    }

    if (matched_wp >= 0) {
        uint64_t dr7_orig = pkt.dbreg64.dr[7];

        int rw_bits = (int)((dr7_orig >> (16 + 4 * matched_wp)) & 3);
        if (rw_bits == 0) {

            uint64_t clear_mask = ((uint64_t)3 << (2 * matched_wp))
                                | ((uint64_t)0xF << (16 + 4 * matched_wp));
            pkt.dbreg64.dr[7] = dr7_orig & ~clear_mask;
            kern_ptrace(PT_SETDBREGS, (int)lwpid, &pkt.dbreg64, 0);
            kern_ptrace(PT_STEP,      (int)lwpid, (void *)1,     0);
            int status2 = 0;
            while (wait4((int)g_debug_ctx.pid, &status2, 1, NULL) <= 0) {
                sceKernelUsleep(100);
            }
            pkt.dbreg64.dr[7] = dr7_orig;
            kern_ptrace(PT_SETDBREGS, (int)lwpid, &pkt.dbreg64, 0);
        }

    }

    uint64_t rip_minus_1 = pkt.reg64.r_rip - 1;
    int matched_bp = -1;
    for (int i = 0; i < MAX_BPS; i++) {
        if (g_debug_ctx.bp[i].enabled &&
            g_debug_ctx.bp[i].address == rip_minus_1) {
            matched_bp = i;
            break;
        }
    }

    if (matched_bp >= 0) {
        struct debug_bp *slot = &g_debug_ctx.bp[matched_bp];
        uint64_t bp_addr = slot->address;

        int numlwps = kern_ptrace(PT_GETNUMLWPS, (int)g_debug_ctx.pid, 0, 0);
        uint32_t *lwpids = NULL;
        if (numlwps > 0) {
            lwpids = (uint32_t *)net_alloc_buffer((size_t)numlwps * sizeof(uint32_t));
            if (lwpids && kern_ptrace(PT_GETLWPLIST, (int)g_debug_ctx.pid, lwpids, numlwps) <= 0) {
                free(lwpids);
                lwpids = NULL;
                numlwps = 0;
            }
        }

        #define MAX_PENDING_BP_LWPS 64
        uint32_t pending_lwps[MAX_PENDING_BP_LWPS];
        int npending = 0;
        pending_lwps[npending++] = lwpid;

        while (npending < MAX_PENDING_BP_LWPS) {
            int drain_status = 0;
            int drain_r = wait4((int)g_debug_ctx.pid, &drain_status, 1, NULL);
            if (drain_r <= 0) break;
            uint8_t lwpi_d[152];
            errno = 0;
            if (kern_ptrace(PT_LWPINFO, (int)g_debug_ctx.pid, lwpi_d, (int)sizeof(lwpi_d)) == -1) continue;
            uint32_t drained = *(uint32_t *)lwpi_d;
            int dup = 0;
            for (int i = 0; i < npending; i++) {
                if (pending_lwps[i] == drained) { dup = 1; break; }
            }
            if (!dup) pending_lwps[npending++] = drained;
        }

        for (int p = 0; p < npending; p++) {
            uint32_t step_lwp = pending_lwps[p];
            struct __reg64 regs;

            if (step_lwp == lwpid) {
                regs = pkt.reg64;
            } else {
                if (kern_ptrace(PT_GETREGS, (int)step_lwp, &regs, 0) == -1) continue;
            }

            if (regs.r_rip - 1 != bp_addr) continue;

            if (lwpids) {
                for (int i = 0; i < numlwps; i++) {
                    if (lwpids[i] != step_lwp) {
                        kern_ptrace(PT_SUSPEND, (int)lwpids[i], 0, 0);
                    }
                }
            }

            sys_proc_rw(g_debug_ctx.pid, bp_addr, &slot->saved_byte, 1, 1);

            regs.r_rip = bp_addr;
            kern_ptrace(PT_SETREGS, (int)step_lwp, &regs, 0);
            kern_ptrace(PT_STEP,    (int)step_lwp, (void *)1, 0);

            int status2 = 0;
            for (;;) {
                int wr = wait4((int)g_debug_ctx.pid, &status2, 1, NULL);
                if (wr > 0) {
                    uint8_t lwpi2[152];
                    errno = 0;
                    if (kern_ptrace(PT_LWPINFO, (int)g_debug_ctx.pid, lwpi2, (int)sizeof(lwpi2)) == -1) {
                        break;
                    }
                    if (*(uint32_t *)lwpi2 == step_lwp) {
                        break;
                    }
                } else {
                    sceKernelUsleep(100);
                }
            }

            uint8_t int3 = 0xCC;
            sys_proc_rw(g_debug_ctx.pid, bp_addr, &int3, 1, 1);

            if (lwpids) {
                for (int i = 0; i < numlwps; i++) {
                    if (lwpids[i] != step_lwp) {
                        kern_ptrace(PT_RESUME, (int)lwpids[i], 0, 0);
                    }
                }
            }

            if (step_lwp == lwpid) {
                pkt.reg64 = regs;
            }
        }

        if (lwpids) free(lwpids);
    }

    if (g_stepping_lwpid > 0) {
        if ((int)lwpid != g_stepping_lwpid) {
            kern_ptrace(PT_CONTINUE, (int)lwpid, (void *)1, 0);
            return 0;
        }
        g_stepping_lwpid = 0;
    }

    net_send_all(svc->dbgctx.dbgfd, &pkt, sizeof(pkt));
    return 0;
}

int debug_set_breakpoint_handle(int fd, struct cmd_packet *packet) {
    struct bp_pkt { uint32_t index; uint32_t enabled; uint64_t address; } __attribute__((packed));
    struct bp_pkt *bp = (struct bp_pkt *)packet->data;

    if (!g_debug_ctx.attached) {
        net_send_int32(fd, CMD_ERROR);
        return 1;
    }
    if (!bp) {
        net_send_int32(fd, CMD_DATA_NULL);
        return 1;
    }
    if (bp->index >= MAX_BPS) {
        net_send_int32(fd, CMD_INVALID_INDEX);
        return 1;
    }

    scePthreadMutexLock(&g_debug_mutex);

    struct debug_bp *slot = &g_debug_ctx.bp[bp->index];
    if (bp->enabled) {
        slot->enabled = 1;
        slot->address = bp->address;

        sys_proc_rw(g_debug_ctx.pid, bp->address, &slot->saved_byte, 1, 0);
        uint8_t int3 = 0xCC;
        sys_proc_rw(g_debug_ctx.pid, bp->address, &int3, 1, 1);
    } else if (slot->enabled) {

        uint64_t bp_addr = slot->address;
        uint32_t rewound[64];
        int n_rewound = 0;

        int numlwps = kern_ptrace(PT_GETNUMLWPS, (int)g_debug_ctx.pid, 0, 0);
        if (numlwps > 0) {
            uint32_t *lwpids = (uint32_t *)net_alloc_buffer((size_t)numlwps * sizeof(uint32_t));
            if (lwpids && kern_ptrace(PT_GETLWPLIST, (int)g_debug_ctx.pid, lwpids, numlwps) > 0) {
                struct __reg64 regs;
                for (int i = 0; i < numlwps; i++) {
                    errno = 0;
                    if (kern_ptrace(PT_GETREGS, (int)lwpids[i], &regs, 0) == -1) continue;
                    if (regs.r_rip == bp_addr + 1) {
                        regs.r_rip = bp_addr;
                        kern_ptrace(PT_SETREGS, (int)lwpids[i], &regs, 0);
                        if (n_rewound < 64) rewound[n_rewound++] = lwpids[i];
                    }
                }
            }
            if (lwpids) free(lwpids);
        }

        sys_proc_rw(g_debug_ctx.pid, slot->address, &slot->saved_byte, 1, 1);
        slot->enabled = 0;
        slot->address = 0;
        (void)rewound; (void)n_rewound;

        if (g_recently_disabled_count < BP_DISABLE_VERIFY_MAX) {
            g_recently_disabled_bps[g_recently_disabled_count++] = bp_addr;
        }
    }

    scePthreadMutexUnlock(&g_debug_mutex);

    net_send_int32(fd, CMD_SUCCESS);
    return 0;
}

int debug_set_watchpoint_handle(int fd, struct cmd_packet *packet) {
    struct wp_pkt {
        uint32_t index;
        uint32_t enabled;
        uint32_t length;
        uint32_t breaktype;
        uint64_t address;
    } __attribute__((packed));
    struct wp_pkt *wp = (struct wp_pkt *)packet->data;

    if (!g_debug_ctx.attached) {
        net_send_int32(fd, CMD_ERROR);
        return 1;
    }
    if (!wp) {
        net_send_int32(fd, CMD_DATA_NULL);
        return 1;
    }
    if (wp->index > 3) {
        net_send_int32(fd, CMD_INVALID_INDEX);
        return 1;
    }

    int count = kern_ptrace(PT_GETNUMLWPS, g_debug_ctx.pid, 0, 0);
    uint32_t *lwpids = NULL;
    if (count > 0) {
        lwpids = (uint32_t *)net_alloc_buffer((size_t)count * sizeof(uint32_t));
        if (!lwpids) {
            net_send_int32(fd, CMD_DATA_NULL);
            return 1;
        }
        if (kern_ptrace(PT_GETLWPLIST, g_debug_ctx.pid, lwpids, count) == -1) {
            free(lwpids);
            net_send_int32(fd, CMD_ERROR);
            return 1;
        }
    }

    uint64_t v10 = 3ULL << (2 * wp->index);
    g_debug_ctx.dbreg.dr[7] &= ~(v10 | (15ULL << (4 * wp->index + 16)));
    if (wp->enabled) {
        g_debug_ctx.dbreg.dr[wp->index] = wp->address;
        g_debug_ctx.dbreg.dr[7] |= v10 |
            ((uint64_t)(wp->breaktype | (4 * wp->length)) << (4 * wp->index + 16));
    } else {
        g_debug_ctx.dbreg.dr[wp->index] = 0;
    }

    for (int i = 0; i < count; i++) {
        if (kern_ptrace(PT_SETDBREGS, (int)lwpids[i], &g_debug_ctx.dbreg, 0) == -1 && errno != 0) {
            if (lwpids) free(lwpids);
            net_send_int32(fd, CMD_ERROR);
            return 1;
        }
    }

    if (lwpids) free(lwpids);

    net_send_int32(fd, CMD_SUCCESS);
    return 0;
}

int debug_get_thread_list_handle(int fd, struct cmd_packet *packet) {
    if (!g_debug_ctx.attached) {
        net_send_int32(fd, CMD_ERROR);
        return 1;
    }

    int count = kern_ptrace(PT_GETNUMLWPS, g_debug_ctx.pid, 0, 0);
    if (count < 0) {
        net_send_int32(fd, CMD_ERROR);
        return 1;
    }
    uint32_t *lwpids = NULL;
    if (count > 0) {
        lwpids = (uint32_t *)net_alloc_buffer((size_t)count * sizeof(uint32_t));
        if (!lwpids) {
            net_send_int32(fd, CMD_DATA_NULL);
            return 1;
        }
        if (kern_ptrace(PT_GETLWPLIST, g_debug_ctx.pid, lwpids, count) == -1) {
            free(lwpids);
            net_send_int32(fd, CMD_ERROR);
            return 1;
        }
    }

    net_send_int32(fd, CMD_SUCCESS);
    uint32_t ucount = (uint32_t)count;
    net_send_all(fd, &ucount, sizeof(ucount));
    if (count > 0) {
        net_send_all(fd, lwpids, (int)((size_t)count * sizeof(uint32_t)));
        free(lwpids);
    }
    return 0;
}

int debug_suspend_thread_handle(int fd, struct cmd_packet *packet) {
    uint32_t *pp = (uint32_t *)packet->data;
    if (!g_debug_ctx.attached) { net_send_int32(fd, CMD_ERROR); return 1; }
    if (!pp) { net_send_int32(fd, CMD_DATA_NULL); return 1; }
    int r = kern_ptrace(PT_SUSPEND, (int)*pp, 0, 0);
    net_send_int32(fd, r == -1 ? CMD_ERROR : CMD_SUCCESS);
    return 0;
}

int debug_resume_thread_handle(int fd, struct cmd_packet *packet) {
    uint32_t *pp = (uint32_t *)packet->data;
    if (!g_debug_ctx.attached) { net_send_int32(fd, CMD_ERROR); return 1; }
    if (!pp) { net_send_int32(fd, CMD_DATA_NULL); return 1; }
    int r = kern_ptrace(PT_RESUME, (int)*pp, 0, 0);
    net_send_int32(fd, r == -1 ? CMD_ERROR : CMD_SUCCESS);
    return 0;
}

int debug_getregs_handle(int fd, struct cmd_packet *packet) {
    uint32_t *pp = (uint32_t *)packet->data;
    if (!g_debug_ctx.attached) { net_send_int32(fd, CMD_ERROR); return 1; }
    if (!pp) { net_send_int32(fd, CMD_DATA_NULL); return 1; }

    uint8_t buf[200];
    int r = kern_ptrace(PT_GETREGS, (int)*pp, buf, 0);
    if (!ptrace_ok(r)) { net_send_int32(fd, CMD_ERROR); return 1; }

    net_send_int32(fd, CMD_SUCCESS);
    net_send_all(fd, buf, 176);
    return 0;
}

int debug_setregs_handle(int fd, struct cmd_packet *packet) {
    struct setreg_hdr { uint32_t _u; uint32_t length; } __attribute__((packed));
    struct setreg_hdr *h = (struct setreg_hdr *)packet->data;
    if (!g_debug_ctx.attached) { net_send_int32(fd, CMD_ERROR); return 1; }
    if (!h) { net_send_int32(fd, CMD_DATA_NULL); return 1; }

    net_send_int32(fd, CMD_SUCCESS);
    uint8_t buf[200];
    if (h->length > sizeof(buf)) { net_send_int32(fd, CMD_TOO_MUCH_DATA); return 1; }
    net_recv_all(fd, buf, h->length, 1);
    int r = kern_ptrace(PT_SETREGS, (int)g_debug_ctx.pid, buf, 0);
    net_send_int32(fd, ptrace_ok(r) ? CMD_SUCCESS : CMD_ERROR);
    return 0;
}

int debug_getfpregs_handle(int fd, struct cmd_packet *packet) {
    uint32_t *pp = (uint32_t *)packet->data;
    if (!g_debug_ctx.attached) { net_send_int32(fd, CMD_ERROR); return 1; }
    if (!pp) { net_send_int32(fd, CMD_DATA_NULL); return 1; }

    uint8_t buf[848];
    memset(buf, 0, sizeof(buf));

    if (kern_get_fpregs((int)g_debug_ctx.pid, (int)*pp, buf) != 0) {
        int r = kern_ptrace(PT_GETFPREGS, (int)*pp, buf, 0);
        if (!ptrace_ok(r)) { net_send_int32(fd, CMD_ERROR); return 1; }
    }

    net_send_int32(fd, CMD_SUCCESS);
    net_send_all(fd, buf, 832);
    return 0;
}

int debug_setfpregs_handle(int fd, struct cmd_packet *packet) {
    struct setreg_hdr { uint32_t lwpid; uint32_t length; } __attribute__((packed));
    struct setreg_hdr *h = (struct setreg_hdr *)packet->data;
    if (!g_debug_ctx.attached) { net_send_int32(fd, CMD_ERROR); return 1; }
    if (!h) { net_send_int32(fd, CMD_DATA_NULL); return 1; }

    uint32_t lwpid  = h->lwpid;
    uint32_t length = h->length;

    net_send_int32(fd, CMD_SUCCESS);
    uint8_t buf[848];
    if (length > sizeof(buf)) { net_send_int32(fd, CMD_TOO_MUCH_DATA); return 1; }

    memset(buf, 0, sizeof(buf));
    if (net_recv_all(fd, buf, length, 1) < (int)length) {
        net_send_int32(fd, CMD_ERROR);
        return 1;
    }

    int applied = 0;
    if (length == 832)
        applied = (kern_set_fpregs((int)g_debug_ctx.pid, (int)lwpid, buf) == 0);
    if (!applied) {
        int r = kern_ptrace(PT_SETFPREGS, (int)g_debug_ctx.pid, buf, 0);
        if (!ptrace_ok(r)) { net_send_int32(fd, CMD_ERROR); return 1; }
    }

    net_send_int32(fd, CMD_SUCCESS);
    return 0;
}

int debug_getdbregs_handle(int fd, struct cmd_packet *packet) {
    uint32_t *pp = (uint32_t *)packet->data;
    if (!g_debug_ctx.attached) { net_send_int32(fd, CMD_ERROR); return 1; }
    if (!pp) { net_send_int32(fd, CMD_DATA_NULL); return 1; }

    uint8_t buf[152];
    int r = kern_ptrace(PT_GETDBREGS, (int)*pp, buf, 0);
    if (!ptrace_ok(r)) { net_send_int32(fd, CMD_ERROR); return 1; }

    net_send_int32(fd, CMD_SUCCESS);
    net_send_all(fd, buf, 128);
    return 0;
}

int debug_setdbregs_handle(int fd, struct cmd_packet *packet) {
    struct setreg_hdr { uint32_t _u; uint32_t length; } __attribute__((packed));
    struct setreg_hdr *h = (struct setreg_hdr *)packet->data;
    if (!g_debug_ctx.attached) { net_send_int32(fd, CMD_ERROR); return 1; }
    if (!h) { net_send_int32(fd, CMD_DATA_NULL); return 1; }

    net_send_int32(fd, CMD_SUCCESS);
    uint8_t buf[152];
    if (h->length > sizeof(buf)) { net_send_int32(fd, CMD_TOO_MUCH_DATA); return 1; }
    net_recv_all(fd, buf, h->length, 1);
    int r = kern_ptrace(PT_SETDBREGS, (int)g_debug_ctx.pid, buf, 0);
    net_send_int32(fd, ptrace_ok(r) ? CMD_SUCCESS : CMD_ERROR);
    return 0;
}

int debug_continue_handle(int fd, struct cmd_packet *packet) {
    uint32_t *pp = (uint32_t *)packet->data;
    if (!g_debug_ctx.attached) { net_send_int32(fd, CMD_ERROR); return 1; }
    if (!pp) { net_send_int32(fd, CMD_DATA_NULL); return 1; }

    int signal = (*pp == 1) ? 17 : (*pp == 2) ? 9 : 0;
    int r = kern_ptrace(PT_CONTINUE, (int)g_debug_ctx.pid, (void *)1, signal);
    if (signal == 0) bp_disable_verify_sweep();
    net_send_int32(fd, ptrace_ok(r) ? CMD_SUCCESS : CMD_ERROR);
    return 0;
}

int debug_thread_info_handle(int fd, struct cmd_packet *packet) {
    uint32_t *pp = (uint32_t *)packet->data;
    if (!g_debug_ctx.attached || !pp) {
        net_send_int32(fd, CMD_DATA_NULL);
        return 1;
    }
    struct sys_proc_thrinfo_args args;
    args.lwpid = *pp;
    sys_proc_cmd(g_debug_ctx.pid, SYS_PROC_THRINFO, &args);
    net_send_int32(fd, CMD_SUCCESS);
    net_send_all(fd, &args, sizeof(args));
    return 0;
}

int debug_step_handle(int fd, struct cmd_packet *packet) {
    if (!g_debug_ctx.attached) { net_send_int32(fd, CMD_ERROR); return 1; }

    scePthreadMutexLock(&g_debug_mutex);
    int r = kern_ptrace(PT_STEP, (int)g_debug_ctx.pid, (void *)1, 0);
    scePthreadMutexUnlock(&g_debug_mutex);

    net_send_int32(fd, r == 0 ? CMD_SUCCESS : CMD_ERROR);
    return 0;
}

int debug_step_thread_handle(int fd, struct cmd_packet *packet) {
    if (!g_debug_ctx.attached) { net_send_int32(fd, CMD_ERROR); return 1; }
    uint32_t *pp = (uint32_t *)packet->data;

    int r;
    if (!pp) {

        r = kern_ptrace(PT_STEP, (int)g_debug_ctx.pid, (void *)1, 0);
    } else {
        int target = (int)*pp;
        scePthreadMutexLock(&g_debug_mutex);
        g_stepping_lwpid = target;
        r = kern_ptrace(PT_STEP, target, (void *)1, 0);
        scePthreadMutexUnlock(&g_debug_mutex);
    }

    net_send_int32(fd, r == 0 ? CMD_SUCCESS : CMD_ERROR);
    return 0;
}

int debug_getfsgsbase_handle(int fd, struct cmd_packet *packet) {
    uint32_t *pp = (uint32_t *)packet->data;
    if (!g_debug_ctx.attached) { net_send_int32(fd, CMD_ERROR); return 1; }
    if (!pp) { net_send_int32(fd, CMD_DATA_NULL); return 1; }

    uint8_t buf[16];
    memset(buf, 0, sizeof(buf));
    if (kern_get_fsgsbase((int)g_debug_ctx.pid, (int)*pp, buf) != 0) {
        net_send_int32(fd, CMD_ERROR);
        return 1;
    }

    net_send_int32(fd, CMD_SUCCESS);
    net_send_all(fd, buf, 16);
    return 0;
}

int debug_setfsgsbase_handle(int fd, struct cmd_packet *packet) {
    struct setreg_hdr { uint32_t lwpid; uint32_t length; } __attribute__((packed));
    struct setreg_hdr *h = (struct setreg_hdr *)packet->data;
    if (!g_debug_ctx.attached) { net_send_int32(fd, CMD_ERROR); return 1; }
    if (!h) { net_send_int32(fd, CMD_DATA_NULL); return 1; }
    if (h->length != 16) { net_send_int32(fd, CMD_ERROR); return 1; }

    uint8_t buf[16];
    memset(buf, 0, sizeof(buf));
    net_send_int32(fd, CMD_SUCCESS);
    if (net_recv_all(fd, buf, 16, 1) < 16) {
        net_send_int32(fd, CMD_ERROR);
        return 1;
    }

    if (kern_set_fsgsbase((int)g_debug_ctx.pid, (int)h->lwpid, buf) != 0) {
        net_send_int32(fd, CMD_ERROR);
        return 1;
    }

    net_send_int32(fd, CMD_SUCCESS);
    return 0;
}

int debug_handle(int fd, struct cmd_packet *packet) {
    switch(packet->cmd) {

        case CMD_DEBUG_PROCESS_STOP:     return debug_process_stop_handle(fd, packet);
        case CMD_DEBUG_ATTACH:           return debug_attach_handle(fd, packet);
        case CMD_DEBUG_DETACH:           return debug_detach_handle(fd, packet);
        case CMD_DEBUG_SET_BREAKPOINT:   return debug_set_breakpoint_handle(fd, packet);
        case CMD_DEBUG_SET_WATCHPOINT:   return debug_set_watchpoint_handle(fd, packet);
        case CMD_DEBUG_GET_THREAD_LIST:  return debug_get_thread_list_handle(fd, packet);
        case CMD_DEBUG_SUSPEND_THREAD:   return debug_suspend_thread_handle(fd, packet);
        case CMD_DEBUG_RESUME_THREAD:    return debug_resume_thread_handle(fd, packet);
        case CMD_DEBUG_GETREGS:          return debug_getregs_handle(fd, packet);
        case CMD_DEBUG_SETREGS:          return debug_setregs_handle(fd, packet);
        case CMD_DEBUG_GETFPREGS:        return debug_getfpregs_handle(fd, packet);
        case CMD_DEBUG_SETFPREGS:        return debug_setfpregs_handle(fd, packet);
        case CMD_DEBUG_GETDBREGS:        return debug_getdbregs_handle(fd, packet);
        case CMD_DEBUG_SETDBREGS:        return debug_setdbregs_handle(fd, packet);

        case 0xBDBB000Eu:                return debug_getfsgsbase_handle(fd, packet);
        case 0xBDBB000Fu:                return debug_setfsgsbase_handle(fd, packet);
        case CMD_DEBUG_CONTINUE:         return debug_continue_handle(fd, packet);
        case CMD_DEBUG_THREAD_INFO:      return debug_thread_info_handle(fd, packet);
        case CMD_DEBUG_STEP:             return debug_step_handle(fd, packet);
        case CMD_DEBUG_STEP_THREAD:      return debug_step_thread_handle(fd, packet);
    }
    return 1;
}
