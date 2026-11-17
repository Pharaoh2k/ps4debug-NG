// SPDX-License-Identifier: GPL-3.0-only
//
// Behavioral tests for the CMD_DEBUG_ATTACH / CMD_DEBUG_DETACH / disconnect
// paths. This file includes the real debugger/source/debug.c and runs its
// handlers on the host against a small fake kernel that follows FreeBSD
// ptrace(2)/wait4(2) rules:
//
//   - PT_ATTACH queues a SIGSTOP; wait4(WNOHANG) reports the stop once;
//   - PT_CONTINUE / PT_DETACH / PT_GETNUMLWPS fail with EBUSY unless the
//     target is stopped and the stop was collected by wait4;
//   - PT_DETACH on an untraced process fails with EPERM;
//   - wait4 on a process we do not trace fails with ECHILD.
//
// g_debug_mutex is faked too: every ptrace/wait4/kill must run with it held,
// and a "lock hook" lets a test run another client's commands at the moment
// a handler asks for the lock, to replay races deterministically.
//
// Build and run with: make -C tests/unit (or make -C debugger test)

#include "../../debugger/source/debug.c"

// ---- reporting -------------------------------------------------------------

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

// ---- fake processes ----------------------------------------------------------

#define NEVER     0x7fffffffffffLL
#define NPROC     4
#define NLWP      3
#define SIGUSR1_  30              // FreeBSD SIGUSR1: an outside signal

struct fproc {
    int  pid;
    int  exists;
    int  traced;
    int  stopped;
    int  collected;     // stop reported by wait4 (FreeBSD P_WAITED)
    int  stop_sig;      // signal reported for the current stop
    int  stop_pending;  // a SIGSTOP is queued (ours and/or an outside one)
    long long stop_due; // when the queued SIGSTOP takes effect
    long long stop_delay_us; // delay for SIGSTOP from kill() / PT_ATTACH
    int  ext_sig;       // an outside signal arriving at ext_due
    long long ext_due;
    long long trap_due; // a thread runs the breakpoint's int3 at trap_due
    int  trap_lwp;
    int  stop_at_probe; // stop with probe_sig right at the next PT_GETNUMLWPS
    int  probe_sig;     // (i.e. after wait4 saw nothing, before the probe)
    int  nlwp;
    uint64_t rip[NLWP];
    int  dbregs[NLWP];      // watchpoint armed
    int  queued_trap[NLWP]; // trap raised but not reported yet
    uint64_t bp_addr;       // a breakpoint the debugger set (0: none)
    int  bp_restored;       // its original byte written back
    int  detaches;
    int  sigstops;          // SIGSTOPs sent by the debugger (kill)
    int  other_signals;
    int  delivered[8];      // signals passed on by PT_CONTINUE / PT_DETACH
    int  ndelivered;
    int  detach_sig;
    // Conditions that would hurt the released game; all must stay 0.
    int  detached_with_stop_pending; // queued SIGSTOP stops it untraced
    int  detached_with_watchpoints;  // armed watchpoint traps it untraced
    int  detached_with_queued_trap;  // queued trap kills it untraced
    int  detached_with_bp_bytes;     // int3 left in its code
    int  ran_mid_instruction;        // resumed a thread at breakpoint + 1
    int  legit_at_bp1[NLWP];         // got to breakpoint + 1 by running the
                                     // restored instruction (not via int3)
    int  ran_twice;                  // such a thread was moved back: the
                                     // instruction would run twice
    int  lwp1_runs_restored_insn;    // on the next resume, lwp 1 runs the
                                     // restored instruction at the breakpoint
};

static struct fproc procs[NPROC];
static long long now_us;
static int fake_errno;
static int wait4_fail_errno;   // next wait4 fails with this errno (once)
static int detach_busy_left;   // the next N PT_DETACH calls fail with EBUSY
                               // even on a stopped target (fault injection)
static int rw_fail_left;       // the next N breakpoint byte restores fail
static int setregs_fail_left;  // the next N PT_SETREGS fail
static int setdbregs_fail_left;// the next N PT_SETDBREGS fail
static int continue_fail_left; // the next N PT_CONTINUE fail
static int lenient_continue;   // PT_CONTINUE also works on a running target
                               // (not FreeBSD; models an unverified kernel)
static int lock_held;
static int unlocked_kernel_calls;
static void (*lock_hook)(void);
static uint32_t replies[64];   // last reply per fd

static struct fproc *proc(int pid) {
    for (int i = 0; i < NPROC; i++) {
        if (procs[i].pid == pid) return &procs[i];
    }
    return NULL;
}

static struct fproc *add_proc(int pid, long long stop_delay_us) {
    for (int i = 0; i < NPROC; i++) {
        if (procs[i].pid == 0) {
            memset(&procs[i], 0, sizeof(procs[i]));
            procs[i].pid = pid;
            procs[i].exists = 1;
            procs[i].stop_due = NEVER;
            procs[i].ext_due = NEVER;
            procs[i].trap_due = NEVER;
            procs[i].stop_delay_us = stop_delay_us;
            procs[i].nlwp = 1;
            procs[i].rip[0] = 0x400000;
            procs[i].rip[1] = 0x400100;
            procs[i].rip[2] = 0x400200;
            return &procs[i];
        }
    }
    return NULL;
}

// Lwp ids are pid * 10 + index.
static struct fproc *lwp(int id, int *idx) {
    struct fproc *p = proc(id / 10);
    *idx = id % 10;
    return p && p->exists && *idx < p->nlwp ? p : NULL;
}

static void stop_now(struct fproc *p, int sig) {
    p->stopped = 1;
    p->collected = 0;
    p->stop_sig = sig;
}

// Signals and traps only take effect on a running target, earliest first.
// FreeBSD leaves a SIGSTOP sent to an already stopped traced process queued
// (kern_sig.c, tdsendsignal: "traced process is already stopped ... goto
// out"), so it fires once the process runs again; after PT_DETACH that is an
// untraced stop.
static void settle(struct fproc *p) {
    if (p->stopped) return;
    long long due = NEVER;
    int which = 0;
    if (p->stop_pending && p->stop_due < due) { due = p->stop_due; which = 1; }
    if (p->ext_sig && p->ext_due < due)       { due = p->ext_due;  which = 2; }
    if (p->bp_addr && !p->bp_restored && p->trap_due < due) { due = p->trap_due; which = 3; }
    if (due == NEVER || now_us < due) return;
    if (which == 1) {
        p->stop_pending = 0;
        p->stop_due = NEVER;
        stop_now(p, SIGSTOP);
    } else if (which == 2) {
        stop_now(p, p->ext_sig);
        p->ext_sig = 0;
        p->ext_due = NEVER;
    } else {
        p->rip[p->trap_lwp] = p->bp_addr + 1;   // ran the int3
        p->trap_due = NEVER;
        stop_now(p, SIGTRAP);
    }
}

static void queue_stop(struct fproc *p) {
    long long due = p->stop_delay_us == NEVER ? NEVER : now_us + p->stop_delay_us;
    if (!p->stop_pending || due < p->stop_due) p->stop_due = due;
    p->stop_pending = 1;
}

static int fail(int e) { fake_errno = e; return -1; }

static void kernel_call(void) {
    if (!lock_held) unlocked_kernel_calls++;
}

// The checks FreeBSD's kern_ptrace applies to every request but PT_ATTACH.
static int need_stopped(struct fproc *p) {
    if (!p->traced) return fail(EPERM);
    if (!p->stopped || !p->collected) return fail(EBUSY);
    return 0;
}

static int resume(struct fproc *p, int data, int detach) {
    for (int i = 0; i < p->nlwp; i++) {
        if (p->bp_addr && p->rip[i] == p->bp_addr + 1 && !p->legit_at_bp1[i]) p->ran_mid_instruction++;
    }
    if (!detach && p->lwp1_runs_restored_insn && p->bp_restored) {
        p->lwp1_runs_restored_insn = 0;
        p->rip[1] = p->bp_addr + 1;           // a one-byte instruction ran
        p->legit_at_bp1[1] = 1;
    }
    if (data) {
        if (p->ndelivered < 8) p->delivered[p->ndelivered] = data;
        p->ndelivered++;
    }
    p->stopped = 0;
    p->collected = 0;
    if (detach) {
        p->traced = 0;
        p->detaches++;
        p->detach_sig = data;
        if (p->stop_pending) p->detached_with_stop_pending++;
        if (p->bp_addr && !p->bp_restored) p->detached_with_bp_bytes++;
        for (int i = 0; i < p->nlwp; i++) {
            if (p->dbregs[i]) p->detached_with_watchpoints++;
            if (p->queued_trap[i]) p->detached_with_queued_trap++;
        }
        return 0;
    }
    // A thread whose trap was queued behind another stop reports it now.
    for (int i = 0; i < p->nlwp; i++) {
        if (p->queued_trap[i]) {
            p->queued_trap[i] = 0;
            stop_now(p, SIGTRAP);
            break;
        }
    }
    return 0;
}

// ---- fake libPS4 / kernel / debugger externals ------------------------------

int *__error(void) { return &fake_errno; }

static int fake_ptrace(int op, int pid, void *addr, int data) {
    kernel_call();
    int i;
    struct fproc *p;
    switch (op) {
    case PT_GETREGS:
    case PT_SETREGS:
    case PT_SETDBREGS:
        if (!(p = lwp(pid, &i))) return fail(ESRCH);
        settle(p);
        if (need_stopped(p)) return -1;
        if (op == PT_GETREGS) {
            memset(addr, 0, sizeof(struct __reg64));
            ((struct __reg64 *)addr)->r_rip = p->rip[i];
        } else if (op == PT_SETREGS) {
            if (setregs_fail_left > 0) { setregs_fail_left--; return fail(EIO); }
            if (p->legit_at_bp1[i] && p->rip[i] != ((struct __reg64 *)addr)->r_rip) p->ran_twice++;
            p->rip[i] = ((struct __reg64 *)addr)->r_rip;
        } else {
            if (setdbregs_fail_left > 0) { setdbregs_fail_left--; return fail(EIO); }
            p->dbregs[i] = 0;
        }
        return 0;
    }
    p = proc(pid);
    if (!p || !p->exists) return fail(ESRCH);
    settle(p);
    switch (op) {
    case PT_ATTACH:
        if (p->traced) return fail(EBUSY);
        p->traced = 1;
        queue_stop(p);
        return 0;
    case PT_GETNUMLWPS:
        if (p->stop_at_probe && !p->stopped) {   // stops right now
            p->stop_at_probe = 0;
            if (p->probe_sig == SIGTRAP && p->bp_addr) p->rip[p->trap_lwp] = p->bp_addr + 1;
            stop_now(p, p->probe_sig);
        }
        if (need_stopped(p)) return -1;
        return p->nlwp;
    case PT_GETLWPLIST:
        if (need_stopped(p)) return -1;
        for (int k = 0; k < p->nlwp && k < data; k++) ((uint32_t *)addr)[k] = (uint32_t)(pid * 10 + k);
        return p->nlwp;
    case PT_CONTINUE:
        if (lenient_continue && p->traced && !p->stopped) return 0;
        if (need_stopped(p)) return -1;
        if (continue_fail_left > 0) { continue_fail_left--; return fail(EIO); }
        return resume(p, data, 0);
    case PT_DETACH:
        if (!p->traced) return fail(EPERM);
        if (detach_busy_left > 0) { detach_busy_left--; return fail(EBUSY); }
        if (need_stopped(p)) return -1;
        return resume(p, data, 1);
    default:
        return 0;
    }
}

unsigned long syscall(unsigned long n, ...) {
    va_list ap;
    va_start(ap, n);
    int op = va_arg(ap, int);
    int pid = va_arg(ap, int);
    void *addr = va_arg(ap, void *);
    int data = va_arg(ap, int);
    va_end(ap);
    if (n != 26) return (unsigned long)fail(ENOSYS);
    return (unsigned long)(long)fake_ptrace(op, pid, addr, data);
}

int wait4(int pid, int *status, int options, void *rusage) {
    (void)options; (void)rusage;
    kernel_call();
    if (wait4_fail_errno) {
        int e = wait4_fail_errno;
        wait4_fail_errno = 0;
        return fail(e);
    }
    struct fproc *p = proc(pid);
    if (!p || !p->exists || !p->traced) return fail(ECHILD);
    settle(p);
    if (p->stopped && !p->collected) {
        p->collected = 1;
        *status = (p->stop_sig << 8) | 0x7f;
        return pid;
    }
    return 0;
}

int kill(int pid, int sig) {
    kernel_call();
    struct fproc *p = proc(pid);
    if (!p || !p->exists) return fail(ESRCH);
    if (sig == SIGSTOP) {
        p->sigstops++;
        queue_stop(p);
    } else if (sig == 19) {          // SIGCONT discards a pending stop
        p->stop_pending = 0;
        p->stop_due = NEVER;
        p->other_signals++;
    } else {
        p->other_signals++;
    }
    return 0;
}

// Someone else sends SIGSTOP (merges with one already queued).
static void outside_sigstop(struct fproc *p) { queue_stop(p); }

static int fake_usleep(unsigned int us) {
    now_us += us;
    if (now_us > 60000000LL) {        // no path may wait this long
        say("FAIL: unbounded wait (over 60 s of sleeps)\n");
        failures++;
        for (int i = 0; i < NPROC; i++) {   // end the loop: let stops land
            procs[i].stop_delay_us = 0;
            if (procs[i].stop_pending) procs[i].stop_due = 0;
        }
        now_us = 0;
    }
    return 0;
}
static int fake_gettimeofday(SceKernelTimeval *tp) {
    tp->tv_sec = now_us / 1000000;
    tp->tv_usec = now_us % 1000000;
    return 0;
}
int (*sceKernelUsleep)(unsigned int) = fake_usleep;
int (*sceKernelGettimeofday)(SceKernelTimeval *) = fake_gettimeofday;

static int open_sockets;
static int fake_socket(const char *n, int d, int t, int p) { (void)n; (void)d; (void)t; (void)p; open_sockets++; return 40 + open_sockets; }
static int fake_setsockopt(int s, int l, int o, const void *v, socklen_t n) { (void)s; (void)l; (void)o; (void)v; (void)n; return 0; }
static int fake_connect(int s, struct sockaddr *a, int n) { (void)s; (void)a; (void)n; return 0; }
static uint16_t fake_htons(uint16_t v) { return (uint16_t)((v << 8) | (v >> 8)); }
static int closed_fds[64];
static int fake_close(int s) { if (s > 0 && s < 64) closed_fds[s]++; return 0; }
int (*sceNetSocket)(const char *, int, int, int) = fake_socket;
int (*sceNetSetsockopt)(int, int, int, const void *, socklen_t) = fake_setsockopt;
int (*sceNetConnect)(int, struct sockaddr *, int) = fake_connect;
uint16_t (*sceNetHtons)(uint16_t) = fake_htons;
int (*sceNetSocketClose)(int) = fake_close;

static int fake_lock(ScePthreadMutex *m) {
    (void)m;
    if (lock_hook) {                 // another client takes the lock first
        void (*h)(void) = lock_hook;
        lock_hook = NULL;
        h();
    }
    if (lock_held) { say("FAIL: g_debug_mutex locked twice (deadlock)\n"); failures++; }
    lock_held = 1;
    return 0;
}
static int fake_unlock(ScePthreadMutex *m) {
    (void)m;
    if (!lock_held) { say("FAIL: g_debug_mutex unlocked while not held\n"); failures++; }
    lock_held = 0;
    return 0;
}
int (*scePthreadMutexLock)(ScePthreadMutex *) = fake_lock;
int (*scePthreadMutexUnlock)(ScePthreadMutex *) = fake_unlock;

int net_send_int32(int fd, uint32_t v) { if (fd >= 0 && fd < 64) replies[fd] = v; return 4; }
int net_send_all(int fd, const void *d, int l) { (void)fd; (void)d; return l; }
int net_recv_all(int fd, void *d, int l, int f) { (void)fd; (void)d; (void)f; return l; }
void *net_alloc_buffer(size_t n) { return malloc(n); }
int sys_proc_cmd(uint64_t pid, uint64_t cmd, void *data) {
    (void)cmd; (void)data;
    struct fproc *p = proc((int)pid);
    return p && p->exists ? 0 : 1;
}
int sys_proc_rw(uint64_t pid, uint64_t a, void *d, uint64_t l, uint64_t w) {
    (void)d; (void)l;
    struct fproc *p = proc((int)pid);
    if (!p || !p->exists) return -1;
    if (w && p->bp_addr && a == p->bp_addr) {
        if (rw_fail_left > 0) { rw_fail_left--; return -1; }
        p->bp_restored = 1;
    }
    return 0;
}
int sys_console_cmd(uint64_t cmd, void *data) { (void)cmd; (void)data; return 0; }
int kern_get_fsgsbase(int pid, int lwpid, void *o) { (void)pid; (void)lwpid; (void)o; return 0; }
int kern_set_fsgsbase(int pid, int lwpid, const void *i) { (void)pid; (void)lwpid; (void)i; return 0; }
int kern_get_fpregs(int pid, int lwpid, void *b) { (void)pid; (void)lwpid; (void)b; return 0; }
int kern_set_fpregs(int pid, int lwpid, const void *b) { (void)pid; (void)lwpid; (void)b; return 0; }

// ---- clients -----------------------------------------------------------------

static struct server_client cli_a, cli_b;

static void reset(void) {
    memset(procs, 0, sizeof(procs));
    memset(replies, 0, sizeof(replies));
    memset(closed_fds, 0, sizeof(closed_fds));
    memset(&cli_a, 0, sizeof(cli_a));
    memset(&cli_b, 0, sizeof(cli_b));
    cli_a.fd = 10;
    cli_b.fd = 11;
    now_us = 0;
    fake_errno = 0;
    wait4_fail_errno = 0;
    lenient_continue = 0;
    detach_busy_left = 0;
    rw_fail_left = 0;
    setregs_fail_left = 0;
    setdbregs_fail_left = 0;
    continue_fail_left = 0;
    lock_held = 0;
    unlocked_kernel_calls = 0;
    lock_hook = NULL;
    open_sockets = 0;
    memset(&g_debug_ctx, 0, sizeof(g_debug_ctx));
    g_orphan_pid = 0;
    g_orphan_stop_req = 0;
    g_orphan_sig = 0;
    memset(g_orphan_bp, 0, sizeof(g_orphan_bp));
    g_debugging = 0;
    curdbgcli = NULL;
    curdbgctx = NULL;
}

static uint32_t attach(struct server_client *c, int pid) {
    uint32_t body[2] = { (uint32_t)pid, 0x0100007f };
    struct cmd_packet pk = { 0, CMD_DEBUG_ATTACH, sizeof(body), body };
    debug_attach_handle_svc(c, &pk);
    return replies[c->fd];
}

static uint32_t detach(struct server_client *c) {
    struct cmd_packet pk = { 0, CMD_DEBUG_DETACH, 0, NULL };
    debug_detach_handle(c->fd, &pk);
    return replies[c->fd];
}

static uint32_t getregs(struct server_client *c) {
    uint32_t lwp = 1;
    struct cmd_packet pk = { 0, CMD_DEBUG_GETREGS, sizeof(lwp), &lwp };
    replies[c->fd] = 0;
    debug_getregs_handle(c->fd, &pk);
    return replies[c->fd];
}

// Make an attached target stop at a breakpoint (as dispatch_debug_events
// would see it): stopped, and the stop already collected.
static void hit_breakpoint(int pid) {
    struct fproc *p = proc(pid);
    p->stopped = 1;
    p->collected = 1;
    p->stop_sig = SIGTRAP;
}

// ---- tests -------------------------------------------------------------------

// 1. A disconnect racing a detach and re-attach by another client.
static void race_b_detaches_and_reattaches(void) {
    CHECK(detach(&cli_b) == CMD_SUCCESS);
    CHECK(attach(&cli_b, 200) == CMD_SUCCESS);
}

static void test_disconnect_race(void) {
    reset();
    add_proc(100, 0);
    add_proc(200, 0);
    CHECK(attach(&cli_a, 100) == CMD_SUCCESS);
    CHECK(cli_a.debugging && curdbgcli == &cli_a);
    int a_sock = cli_a.dbgctx.dbgfd;

    // A disconnects; before A gets the lock, B detaches A's session and
    // attaches a new target.
    lock_hook = race_b_detaches_and_reattaches;
    debug_client_disconnect(&cli_a);

    CHECK(!proc(100)->traced);                 // A's target released by B
    CHECK(closed_fds[a_sock] == 1);            // A's socket closed once
    CHECK(g_debug_ctx.attached && g_debug_ctx.pid == 200);
    CHECK(curdbgcli == &cli_b && cli_b.debugging);
    CHECK(proc(200)->traced && proc(200)->detaches == 0); // B untouched
    CHECK(closed_fds[cli_b.dbgctx.dbgfd] == 0);

    // B's own disconnect does tear its session down.
    debug_client_disconnect(&cli_b);
    CHECK(!g_debug_ctx.attached && curdbgcli == NULL);
    CHECK(!proc(200)->traced && g_orphan_pid == 0);
    CHECK(unlocked_kernel_calls == 0 && !lock_held);
}

// 2. DETACH of a stopped target, of a running one, and one that will not stop.
static void test_detach_paths(void) {
    // Stopped at a breakpoint: plain PT_DETACH, no SIGSTOP.
    reset();
    add_proc(100, 0);
    CHECK(attach(&cli_a, 100) == CMD_SUCCESS);
    hit_breakpoint(100);
    CHECK(detach(&cli_a) == CMD_SUCCESS);
    CHECK(!proc(100)->traced && proc(100)->sigstops == 0);

    // Running: PT_DETACH gets EBUSY, the target is stopped and detached.
    reset();
    add_proc(100, 0);
    CHECK(attach(&cli_a, 100) == CMD_SUCCESS);
    proc(100)->stop_delay_us = 50000;
    CHECK(detach(&cli_a) == CMD_SUCCESS);
    CHECK(!proc(100)->traced && proc(100)->sigstops == 1);
    CHECK(proc(100)->detached_with_stop_pending == 0);
    CHECK(proc(100)->other_signals == 0);       // no SIGCONT or others
    CHECK(g_orphan_pid == 0);

    // Stopped but the stop not collected yet: collect it, no extra SIGSTOP
    // (an extra one would freeze the detached target).
    reset();
    add_proc(100, 0);
    CHECK(attach(&cli_a, 100) == CMD_SUCCESS);
    proc(100)->stopped = 1;
    proc(100)->collected = 0;
    proc(100)->stop_sig = SIGTRAP;
    CHECK(detach(&cli_a) == CMD_SUCCESS);
    // That stop was a trap, so the target is resumed and stopped once more
    // (another thread may have the same trap queued): one SIGSTOP of ours,
    // reported before release.
    CHECK(!proc(100)->traced && proc(100)->sigstops == 1);
    CHECK(proc(100)->detached_with_stop_pending == 0);

    // The detach fails: the stop never comes. The pid is kept, DETACH says
    // so, and it is not a session.
    reset();
    add_proc(100, 0);
    add_proc(300, 0);
    CHECK(attach(&cli_a, 100) == CMD_SUCCESS);
    proc(100)->stop_delay_us = NEVER;
    long long t0 = now_us;
    CHECK(detach(&cli_a) == CMD_ERROR);
    CHECK(now_us - t0 <= 1100000);              // the wait is bounded
    CHECK(proc(100)->traced && g_orphan_pid == 100);
    CHECK(!g_debug_ctx.attached && curdbgcli == NULL);
    CHECK(getregs(&cli_b) == CMD_ERROR);        // not a debug session
    CHECK(attach(&cli_b, 300) == CMD_ALREADY_DEBUG);
    CHECK(!proc(300)->traced);

    // 3. Retry after a later stop: the next attach collects it, detaches the
    // kept pid and attaches the new target.
    proc(100)->stop_due = now_us;
    CHECK(attach(&cli_b, 300) == CMD_SUCCESS);
    CHECK(!proc(100)->traced && g_orphan_pid == 0);
    CHECK(proc(100)->detached_with_stop_pending == 0);
    CHECK(g_debug_ctx.pid == 300 && curdbgcli == &cli_b);
    CHECK(unlocked_kernel_calls == 0 && !lock_held);
}

// 3b. The same retry through DETACH, and through a disconnect-free attach
// timeout.
static void test_retry_paths(void) {
    // Attach where the target never reports its stop: CMD_ERROR, pid kept.
    reset();
    add_proc(400, NEVER);
    CHECK(attach(&cli_a, 400) == CMD_ERROR);
    CHECK(g_orphan_pid == 400 && proc(400)->traced);
    CHECK(!g_debug_ctx.attached && !cli_a.debugging);
    CHECK(closed_fds[41] == 1);                 // interrupt socket closed
    // Later the stop arrives; DETACH (no session) releases it.
    proc(400)->stop_due = now_us;
    CHECK(detach(&cli_b) == CMD_SUCCESS);
    CHECK(!proc(400)->traced && g_orphan_pid == 0);

    // Attach whose stop is just late: released right away (stopped during
    // the detach), so no pid is kept.
    reset();
    add_proc(500, 1500000);
    CHECK(attach(&cli_a, 500) == CMD_ERROR);
    CHECK(g_orphan_pid == 0 && !proc(500)->traced);
    CHECK(proc(500)->detached_with_stop_pending == 0);

    // A wait4 error other than ECHILD keeps the pid.
    reset();
    add_proc(600, NEVER);
    CHECK(attach(&cli_a, 600) == CMD_ERROR);
    CHECK(g_orphan_pid == 600);
    wait4_fail_errno = EINTR;
    CHECK(detach(&cli_b) == CMD_ERROR);
    CHECK(g_orphan_pid == 600 && proc(600)->traced);

    // ECHILD (the pid now belongs to a process we do not trace): dropped,
    // and that process is neither detached nor signalled.
    proc(600)->traced = 0;
    proc(600)->stop_pending = 0;
    int stops = proc(600)->sigstops;
    CHECK(detach(&cli_b) == CMD_SUCCESS);
    CHECK(g_orphan_pid == 0);
    CHECK(proc(600)->sigstops == stops && proc(600)->other_signals == 0);
    CHECK(unlocked_kernel_calls == 0 && !lock_held);
}

// The owner check in debug_client_disconnect: a client whose debugging flag
// is still set but no longer owns the session must not tear it down.
static void test_disconnect_not_owner(void) {
    reset();
    add_proc(100, 0);
    CHECK(attach(&cli_b, 100) == CMD_SUCCESS);
    cli_a.debugging = 1;                        // stale flag, not the owner
    debug_client_disconnect(&cli_a);
    CHECK(g_debug_ctx.attached && curdbgcli == &cli_b && proc(100)->traced);
}

// The attach must not complete before a stop was seen, even on a kernel that
// would let PT_CONTINUE through on a running target.
static void test_attach_needs_stop(void) {
    reset();
    lenient_continue = 1;
    add_proc(700, NEVER);
    CHECK(attach(&cli_a, 700) == CMD_ERROR);
    CHECK(!g_debug_ctx.attached && !cli_a.debugging);
}

// A kept target whose pending stop was cancelled (CMD_DEBUG_PROCESS_STOP
// resume sends SIGCONT): the retry has to stop it itself before PT_DETACH.
static void test_retry_after_sigcont(void) {
    reset();
    add_proc(800, NEVER);
    CHECK(attach(&cli_a, 800) == CMD_ERROR);
    CHECK(g_orphan_pid == 800 && proc(800)->stop_pending);
    uint8_t body[5] = { 0x20, 0x03, 0, 0, 0 };   // pid 800, state 0 = resume
    struct cmd_packet pk = { 0, CMD_DEBUG_PROCESS_STOP, sizeof(body), body };
    debug_process_stop_handle(cli_b.fd, &pk);  // runs without the lock, as on master
    unlocked_kernel_calls = 0;
    CHECK(!proc(800)->stop_pending && proc(800)->other_signals == 1);
    proc(800)->stop_delay_us = 20000;           // a new SIGSTOP now lands
    CHECK(detach(&cli_b) == CMD_SUCCESS);
    CHECK(!proc(800)->traced && g_orphan_pid == 0);
    CHECK(proc(800)->detached_with_stop_pending == 0);
    CHECK(unlocked_kernel_calls == 0 && !lock_held);
}

// SIGSTOP lands but the PT_DETACH after it fails: the target stays stopped
// (stop collected) and kept. Retries must not send it another SIGSTOP, must
// not wait for a stop that will never be reported again, and must release it
// once PT_DETACH works.
static void test_detach_fails_after_stop(void) {
    reset();
    add_proc(100, 0);
    CHECK(attach(&cli_a, 100) == CMD_SUCCESS);
    proc(100)->stop_delay_us = 10000;
    detach_busy_left = 2;          // first try (running) + the one after the stop
    CHECK(detach(&cli_a) == CMD_ERROR);
    CHECK(g_orphan_pid == 100 && proc(100)->traced);
    CHECK(proc(100)->stopped && proc(100)->collected && proc(100)->sigstops == 1);

    // Retry while PT_DETACH still fails: no new SIGSTOP, no long wait.
    detach_busy_left = 1;
    long long t0 = now_us;
    CHECK(detach(&cli_b) == CMD_ERROR);
    CHECK(g_orphan_pid == 100 && proc(100)->sigstops == 1);
    CHECK(!proc(100)->stop_pending && now_us - t0 < 10000);

    // Retry once PT_DETACH works: released, nothing left queued.
    CHECK(detach(&cli_b) == CMD_SUCCESS);
    CHECK(!proc(100)->traced && g_orphan_pid == 0 && proc(100)->sigstops == 1);
    CHECK(proc(100)->detached_with_stop_pending == 0);
    CHECK(unlocked_kernel_calls == 0 && !lock_held);
}

// Teardown of a target already stopped at a breakpoint whose PT_DETACH fails:
// it is already stopped, so no SIGSTOP; the pid is kept and released later.
static void test_teardown_stopped_detach_fails(void) {
    reset();
    add_proc(100, 0);
    CHECK(attach(&cli_a, 100) == CMD_SUCCESS);
    hit_breakpoint(100);
    detach_busy_left = 1;
    long long t0 = now_us;
    CHECK(detach(&cli_a) == CMD_ERROR);
    CHECK(g_orphan_pid == 100 && proc(100)->sigstops == 0);
    CHECK(!proc(100)->stop_pending && now_us - t0 < 10000);
    CHECK(detach(&cli_b) == CMD_SUCCESS);
    CHECK(!proc(100)->traced && proc(100)->sigstops == 0);
    CHECK(proc(100)->detached_with_stop_pending == 0);
}

// ---- release: cleanup on a stopped, traced target (review of dbd84b6) ------

#define BP 0x401000ULL

// A session on pid with one breakpoint (original byte 0x55) and two threads.
static struct fproc *session_with_bp(struct server_client *c, int pid) {
    struct fproc *p = add_proc(pid, 0);
    CHECK(attach(c, pid) == CMD_SUCCESS);
    p->nlwp = 2;
    p->bp_addr = BP;
    g_debug_ctx.bp[0].enabled = 1;
    g_debug_ctx.bp[0].address = BP;
    g_debug_ctx.bp[0].saved_byte = 0x55;
    return p;
}

static int released_cleanly(struct fproc *p) {
    return !p->traced && p->detaches == 1 &&
           p->detached_with_stop_pending == 0 && p->detached_with_watchpoints == 0 &&
           p->detached_with_queued_trap == 0 && p->detached_with_bp_bytes == 0 &&
           p->ran_mid_instruction == 0;
}

static int delivered_count(struct fproc *p, int sig) {
    int n = 0;
    for (int i = 0; i < p->ndelivered && i < 8; i++) n += p->delivered[i] == sig;
    return n;
}

// Watchpoints on a running target: it must be stopped before they are
// cleared (FreeBSD refuses PT_SETDBREGS on a running target).
static void test_release_running_with_watchpoints(void) {
    reset();
    struct fproc *p = session_with_bp(&cli_a, 100);
    p->dbregs[0] = p->dbregs[1] = 1;
    CHECK(detach(&cli_a) == CMD_SUCCESS);
    CHECK(released_cleanly(p) && p->bp_restored);
    CHECK(p->sigstops == 1 && p->ndelivered == 0 && p->detach_sig == 0);
    CHECK(g_orphan_pid == 0 && unlocked_kernel_calls == 0);
}

// Stopped at a breakpoint while another thread's trap is still queued: both
// threads are rewound, the target is resumed so the queued trap is reported
// to us, and it is stopped again before the watchpoints are cleared and it
// is detached (the sweep's PT_CONTINUE leaves it running).
static void test_release_queued_trap(void) {
    reset();
    struct fproc *p = session_with_bp(&cli_a, 100);
    hit_breakpoint(100);
    p->rip[0] = BP + 1;
    p->rip[1] = BP + 1;
    p->queued_trap[1] = 1;
    p->dbregs[0] = p->dbregs[1] = 1;
    CHECK(detach(&cli_a) == CMD_SUCCESS);
    CHECK(released_cleanly(p));
    CHECK(p->rip[0] == BP && p->rip[1] == BP);
    CHECK(delivered_count(p, SIGTRAP) == 0);     // our own trap, not passed on
}

// Review case 1: wait4 saw nothing, then the target stopped (here: a
// watchpoint trap, then an outside signal) right before the probe. Our
// SIGSTOP is queued behind that stop and must be reported before release.
static void test_stop_between_wait_and_probe(void) {
    reset();
    struct fproc *p = add_proc(100, 0);
    CHECK(attach(&cli_a, 100) == CMD_SUCCESS);
    p->stop_delay_us = 30000;
    p->stop_at_probe = 1;
    p->probe_sig = SIGTRAP;
    CHECK(detach(&cli_a) == CMD_SUCCESS);
    CHECK(released_cleanly(p) && p->sigstops == 2);   // + one flush round
    CHECK(delivered_count(p, SIGTRAP) == 0);

    reset();
    p = add_proc(100, 0);
    CHECK(attach(&cli_a, 100) == CMD_SUCCESS);
    p->stop_delay_us = 30000;
    p->stop_at_probe = 1;
    p->probe_sig = SIGUSR1_;
    CHECK(detach(&cli_a) == CMD_SUCCESS);
    CHECK(released_cleanly(p));
    CHECK(delivered_count(p, SIGUSR1_) == 1);    // passed on, not swallowed
}

// Review case 2: another signal stops the target before our SIGSTOP lands.
static void test_other_stop_before_sigstop(void) {
    // An outside signal: passed on, and our SIGSTOP still resolved.
    reset();
    struct fproc *p = add_proc(100, 0);
    CHECK(attach(&cli_a, 100) == CMD_SUCCESS);
    p->stop_delay_us = 50000;
    p->ext_sig = SIGUSR1_;
    p->ext_due = now_us + 10000;
    CHECK(detach(&cli_a) == CMD_SUCCESS);
    CHECK(released_cleanly(p) && p->sigstops == 1);
    CHECK(delivered_count(p, SIGUSR1_) == 1 && p->detach_sig == 0);

    // A breakpoint trap from a thread that ran the int3 before the byte was
    // restored: rewound before the target is resumed, trap not passed on.
    reset();
    p = session_with_bp(&cli_a, 100);
    p->stop_delay_us = 50000;
    p->rip[1] = BP + 1;
    p->ext_sig = SIGTRAP;
    p->ext_due = now_us + 10000;
    CHECK(detach(&cli_a) == CMD_SUCCESS);
    CHECK(released_cleanly(p) && p->rip[1] == BP);
    CHECK(delivered_count(p, SIGTRAP) == 0);
}

// Outside SIGSTOPs are not swallowed into a lost stop, and ours never stays
// queued behind a release.
static void test_outside_sigstop(void) {
    // Stopped by someone else's SIGSTOP (not collected yet), no request of
    // ours: no SIGSTOP from us, and the outside one is passed on at detach.
    reset();
    struct fproc *p = add_proc(100, 0);
    CHECK(attach(&cli_a, 100) == CMD_SUCCESS);
    outside_sigstop(p);
    CHECK(detach(&cli_a) == CMD_SUCCESS);
    CHECK(!p->traced && p->sigstops == 0 && p->detach_sig == SIGSTOP);
    CHECK(p->detached_with_stop_pending == 0);

    // An outside SIGSTOP stops it first while ours is still queued. The two
    // cannot be told apart; ours is treated as reported, so the queued one
    // stops the target after release, which is what the outside sender asked
    // for. (Either way the target ends up stopped, never resumed by us.)
    reset();
    p = add_proc(100, 0);
    CHECK(attach(&cli_a, 100) == CMD_SUCCESS);
    p->stop_delay_us = 50000;
    p->ext_sig = SIGSTOP;
    p->ext_due = now_us + 10000;
    CHECK(detach(&cli_a) == CMD_SUCCESS);
    CHECK(!p->traced && p->sigstops == 1);
    CHECK(p->detached_with_stop_pending == 1 || p->detach_sig == SIGSTOP);
    CHECK(p->other_signals == 0);                // no SIGCONT
}

// Cleanup failures: nothing is detached with a breakpoint byte, a thread on
// a breakpoint or an armed watchpoint left; the pid, the breakpoints and the
// stop state are kept, DETACH says CMD_ERROR, and a retry finishes the job.
static void test_cleanup_failures(void) {
    // Watchpoint clear fails.
    reset();
    struct fproc *p = session_with_bp(&cli_a, 100);
    p->dbregs[0] = p->dbregs[1] = 1;
    setdbregs_fail_left = 1;
    CHECK(detach(&cli_a) == CMD_ERROR);
    CHECK(p->traced && p->detaches == 0 && g_orphan_pid == 100);
    CHECK(getregs(&cli_b) == CMD_ERROR);         // not a session
    CHECK(detach(&cli_b) == CMD_SUCCESS);        // retry
    CHECK(released_cleanly(p) && g_orphan_pid == 0);

    // Breakpoint byte restore fails: kept with its breakpoint, then retried.
    reset();
    p = session_with_bp(&cli_a, 100);
    rw_fail_left = 1;
    CHECK(detach(&cli_a) == CMD_ERROR);
    CHECK(p->traced && p->detaches == 0 && !p->bp_restored);
    CHECK(g_orphan_pid == 100 && g_orphan_bp[0].enabled && g_orphan_bp[0].address == BP);
    CHECK(!g_debug_ctx.attached);
    add_proc(300, 0);
    CHECK(attach(&cli_b, 300) == CMD_SUCCESS);   // retry first, then attach
    CHECK(released_cleanly(p) && p->bp_restored && g_orphan_pid == 0);

    // Rewinding a thread off the breakpoint fails: kept, never resumed
    // mid-instruction, then retried.
    reset();
    p = session_with_bp(&cli_a, 100);
    hit_breakpoint(100);
    p->rip[0] = BP + 1;
    setregs_fail_left = 1;
    CHECK(detach(&cli_a) == CMD_ERROR);
    CHECK(p->traced && p->detaches == 0 && p->ran_mid_instruction == 0);
    CHECK(detach(&cli_b) == CMD_SUCCESS);
    CHECK(released_cleanly(p) && p->rip[0] == BP);

    // An outside signal stops it before our SIGSTOP lands and resuming it
    // fails: kept stopped with our SIGSTOP queued behind that stop and the
    // signal still owed. The retry must resume it (passing the signal on)
    // so our SIGSTOP is reported before release, not detach it as it is.
    reset();
    p = add_proc(100, 0);
    CHECK(attach(&cli_a, 100) == CMD_SUCCESS);
    p->stop_delay_us = 50000;
    p->ext_sig = SIGUSR1_;
    p->ext_due = now_us + 10000;
    continue_fail_left = 1;
    CHECK(detach(&cli_a) == CMD_ERROR);
    CHECK(p->traced && p->stopped && p->stop_pending && g_orphan_pid == 100);
    CHECK(g_orphan_stop_req == 1 && g_orphan_sig == SIGUSR1_);
    CHECK(detach(&cli_b) == CMD_SUCCESS);
    CHECK(released_cleanly(p) && delivered_count(p, SIGUSR1_) == 1);

    // Watchpoint clear fails after the breakpoint was removed and the target
    // resumed once; meanwhile another thread ran the restored instruction and
    // is at breakpoint + 1 for real. The retry must not move it back.
    reset();
    p = session_with_bp(&cli_a, 100);
    p->nlwp = 3;
    hit_breakpoint(100);
    p->rip[0] = BP + 1;
    p->lwp1_runs_restored_insn = 1;
    p->dbregs[2] = 1;
    setdbregs_fail_left = 1;
    CHECK(detach(&cli_a) == CMD_ERROR);
    CHECK(p->traced && g_orphan_pid == 100 && !g_orphan_bp[0].enabled);
    CHECK(p->rip[1] == BP + 1);
    CHECK(detach(&cli_b) == CMD_SUCCESS);
    CHECK(released_cleanly(p) && p->ran_twice == 0 && p->rip[1] == BP + 1);

    // Disconnect of the owner with a failing cleanup keeps it the same way.
    reset();
    p = session_with_bp(&cli_a, 100);
    p->dbregs[0] = 1;
    setdbregs_fail_left = 1;
    debug_client_disconnect(&cli_a);
    CHECK(p->traced && g_orphan_pid == 100 && !g_debug_ctx.attached);
    CHECK(detach(&cli_b) == CMD_SUCCESS && released_cleanly(p));
    CHECK(unlocked_kernel_calls == 0 && !lock_held);
}

// ---- SIGTRAP ownership and breakpoint removal (review of 3eb79e1) ---------

// A failed attach: no breakpoint, watchpoint or step exists yet, so a SIGTRAP
// is the game's own and must reach it, not be dropped as the debugger's.
static void test_failed_attach_passes_game_sigtrap(void) {
    reset();
    struct fproc *p = add_proc(100, 50000);  // PT_ATTACH's SIGSTOP lands late
    p->ext_sig = SIGTRAP;                     // the game traps first
    p->ext_due = 10000;
    continue_fail_left = 1;                   // attach fails after that stop
    CHECK(attach(&cli_a, 100) == CMD_ERROR);
    CHECK(!g_debug_ctx.attached && g_orphan_pid == 0);
    CHECK(!p->traced && delivered_count(p, SIGTRAP) == 1);
    CHECK(p->detached_with_stop_pending == 0);

    // Same when the release is only finished by a later retry.
    reset();
    p = add_proc(100, NEVER);
    CHECK(attach(&cli_a, 100) == CMD_ERROR);  // never stops: kept
    CHECK(g_orphan_pid == 100 && g_orphan_own_traps == 0);
    p->ext_sig = SIGTRAP;
    p->ext_due = now_us;
    p->stop_due = now_us + 20000;             // our SIGSTOP lands after it
    CHECK(detach(&cli_b) == CMD_SUCCESS);
    CHECK(!p->traced && delivered_count(p, SIGTRAP) == 1);
    CHECK(p->detached_with_stop_pending == 0);
}

// Stopped at a breakpoint hit by three threads: one reported, two with the
// trap still queued. Every queued trap must be reported to us before release.
static void test_release_two_queued_traps(void) {
    reset();
    struct fproc *p = session_with_bp(&cli_a, 100);
    p->nlwp = 3;
    hit_breakpoint(100);
    p->rip[0] = p->rip[1] = p->rip[2] = BP + 1;
    p->queued_trap[1] = p->queued_trap[2] = 1;
    CHECK(detach(&cli_a) == CMD_SUCCESS);
    CHECK(released_cleanly(p));
    CHECK(delivered_count(p, SIGTRAP) == 0);
}

// After the breakpoint byte is back and the target ran, a thread can be at
// breakpoint + 1 for real (one-byte instruction, e.g. push rbp). It must not
// be moved back, or that instruction runs twice.
static void test_no_rewind_after_running(void) {
    reset();
    struct fproc *p = session_with_bp(&cli_a, 100);
    hit_breakpoint(100);
    p->rip[0] = BP + 1;                       // ran our int3
    p->lwp1_runs_restored_insn = 1;           // lwp 1 runs the real one later
    CHECK(detach(&cli_a) == CMD_SUCCESS);
    CHECK(released_cleanly(p) && p->rip[0] == BP);
    CHECK(p->ran_twice == 0 && p->rip[1] == BP + 1);
}

int main(void) {
    test_failed_attach_passes_game_sigtrap();
    test_release_two_queued_traps();
    test_no_rewind_after_running();
    test_release_running_with_watchpoints();
    test_release_queued_trap();
    test_stop_between_wait_and_probe();
    test_other_stop_before_sigstop();
    test_outside_sigstop();
    test_cleanup_failures();
    test_detach_fails_after_stop();
    test_teardown_stopped_detach_fails();
    test_disconnect_race();
    test_disconnect_not_owner();
    test_attach_needs_stop();
    test_retry_after_sigcont();
    test_detach_paths();
    test_retry_paths();

    if (failures) {
        char m[64];
        snprintf(m, sizeof(m), "%d test(s) failed\n", failures);
        say(m);
        return 1;
    }
    say("test_attach_session: all tests passed\n");
    return 0;
}
