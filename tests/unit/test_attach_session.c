// SPDX-License-Identifier: GPL-3.0-only
//
// Behavioral tests for CMD_DEBUG_ATTACH (stop wait, failed-attach release,
// kept pid and its retry from ATTACH and DETACH) and for the session
// ownership check on client disconnect. This file includes the real
// debugger/source/debug.c and runs its handlers on the host against a small
// fake kernel that follows FreeBSD 9 ptrace(2)/wait4(2)/kill(2) rules:
//
//   - PT_ATTACH queues a SIGSTOP; wait4(WNOHANG) reports a stop once;
//   - PT_CONTINUE / PT_DETACH / PT_GETNUMLWPS fail with EBUSY unless the
//     target is stopped and the stop was collected by wait4; PT_DETACH on an
//     untraced process fails with EPERM; wait4 on a process we do not trace
//     fails with ECHILD;
//   - a SIGSTOP merges into one still queued, but once a thread has taken
//     one (before its stop is reported) a new one is queued apart; SIGCONT
//     removes a queued SIGSTOP, not a taken one.
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
#define SIGUSR1_  30              // FreeBSD SIGUSR1: an outside signal

struct fproc {
    int  pid;
    int  exists;
    int  traced;
    int  stopped;
    int  collected;     // stop reported by wait4 (FreeBSD P_WAITED)
    int  stop_sig;      // signal reported for the current stop
    int  stop_pending;  // a SIGSTOP is queued
    long long stop_due; // when the queued SIGSTOP takes effect
    long long stop_delay_us; // delay for SIGSTOP from kill() / PT_ATTACH
    int  stop_taken;    // a SIGSTOP a thread already took; its stop is
    long long taken_due;//   only reported at taken_due
    int  take_sigstop_early; // the next SIGSTOP is taken at once
    int  ext_sig;       // an outside signal arriving at ext_due
    long long ext_due;
    int  detaches;
    int  sigstops;      // SIGSTOPs sent by the debugger (kill)
    int  other_signals;
    int  delivered[8];  // signals passed on by PT_CONTINUE / PT_DETACH
    int  ndelivered;
    int  detach_sig;
    int  detached_with_stop_pending; // a SIGSTOP stops it untraced: must stay 0
};

static struct fproc procs[NPROC];
static long long now_us;
static int fake_errno;
static int wait4_fail_errno;   // next wait4 fails with this errno (once)
static int detach_busy_left;   // the next N PT_DETACH fail with EBUSY
static int continue_fail_left; // the next N PT_CONTINUE fail
static int lenient_continue;   // PT_CONTINUE also works on a running target,
                               // clearing a SIGSTOP a thread has taken
static int lenient_numlwps;    // PT_GETNUMLWPS also works on a running target
                               // (both not FreeBSD; model an unverified kernel)
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
            procs[i].taken_due = NEVER;
            procs[i].ext_due = NEVER;
            procs[i].stop_delay_us = stop_delay_us;
            return &procs[i];
        }
    }
    return NULL;
}

static void stop_now(struct fproc *p, int sig) {
    p->stopped = 1;
    p->collected = 0;
    p->stop_sig = sig;
}

// Signals only take effect on a running target, earliest first. A SIGSTOP
// sent to a traced process that is already stopped stays queued (FreeBSD
// kern_sig.c, tdsendsignal) and fires once it runs again.
static void settle(struct fproc *p) {
    if (p->stopped) return;
    long long due = NEVER;
    int which = 0;
    if (p->stop_pending && p->stop_due < due) { due = p->stop_due;  which = 1; }
    if (p->stop_taken && p->taken_due < due)  { due = p->taken_due; which = 2; }
    if (p->ext_sig && p->ext_due < due)       { due = p->ext_due;   which = 3; }
    if (due == NEVER || now_us < due) return;
    if (which == 1) {
        p->stop_pending = 0;
        p->stop_due = NEVER;
        stop_now(p, SIGSTOP);
    } else if (which == 2) {
        p->stop_taken = 0;
        p->taken_due = NEVER;
        stop_now(p, SIGSTOP);
    } else {
        stop_now(p, p->ext_sig);
        p->ext_sig = 0;
        p->ext_due = NEVER;
    }
}

static void queue_stop(struct fproc *p) {
    long long due = p->stop_delay_us == NEVER ? NEVER : now_us + p->stop_delay_us;
    if (p->take_sigstop_early && !p->stop_taken && !p->stop_pending) {
        p->take_sigstop_early = 0;
        p->stop_taken = 1;
        p->taken_due = due;
        return;
    }
    if (!p->stop_pending || due < p->stop_due) p->stop_due = due;
    p->stop_pending = 1;
}

static int fail(int e) { fake_errno = e; return -1; }

static void kernel_call(void) {
    if (!lock_held) unlocked_kernel_calls++;
}

static int need_stopped(struct fproc *p) {
    if (!p->traced) return fail(EPERM);
    if (!p->stopped || !p->collected) return fail(EBUSY);
    return 0;
}

static int resume(struct fproc *p, int data, int detach) {
    if (data) {
        if (p->ndelivered < 8) p->delivered[p->ndelivered] = data;
        p->ndelivered++;
    }
    p->stopped = 0;
    p->collected = 0;
    p->stop_sig = 0;
    if (detach) {
        p->traced = 0;
        p->detaches++;
        p->detach_sig = data;
        if (p->stop_pending || p->stop_taken) p->detached_with_stop_pending++;
    }
    return 0;
}

// ---- fake libPS4 / kernel / debugger externals ------------------------------

int *__error(void) { return &fake_errno; }

static int fake_ptrace(int op, int pid, void *addr, int data) {
    kernel_call();
    struct fproc *p = proc(pid);
    if (!p || !p->exists) return fail(ESRCH);
    settle(p);
    switch (op) {
    case PT_ATTACH:
        if (p->traced) return fail(EBUSY);
        p->traced = 1;
        queue_stop(p);
        return 0;
    case PT_GETNUMLWPS:
        if (lenient_numlwps && p->traced) return 1;
        if (need_stopped(p)) return -1;
        return 1;
    case PT_GETLWPLIST:
        if (need_stopped(p)) return -1;
        ((uint32_t *)addr)[0] = (uint32_t)pid * 10;
        return 1;
    case PT_CONTINUE:
        if (lenient_continue && p->traced && !p->stopped) {
            p->stop_taken = 0;
            p->taken_due = NEVER;
            return 0;
        }
        if (need_stopped(p)) return -1;
        if (continue_fail_left > 0) { continue_fail_left--; return fail(EIO); }
        return resume(p, data, 0);
    case PT_DETACH:
        if (!p->traced) return fail(EPERM);
        if (detach_busy_left > 0) { detach_busy_left--; return fail(EBUSY); }
        if (need_stopped(p)) return -1;
        return resume(p, data, 1);
    default:
        return 0; // PT_SETDBREGS on an lwp, etc.
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
    } else if (sig == 19) {          // SIGCONT removes a queued SIGSTOP only
        p->stop_pending = 0;
        p->stop_due = NEVER;
        p->other_signals++;
    } else {
        p->other_signals++;
    }
    return 0;
}

static int fake_usleep(unsigned int us) {
    now_us += us;
    if (now_us > 60000000LL) {        // no path may wait this long
        say("FAIL: unbounded wait (over 60 s of sleeps)\n");
        failures++;
        for (int i = 0; i < NPROC; i++) procs[i].exists = 0;   // end it
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
int sys_proc_rw(uint64_t pid, uint64_t a, void *d, uint64_t l, uint64_t w) { (void)pid; (void)a; (void)d; (void)l; (void)w; return 0; }
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
    detach_busy_left = 0;
    continue_fail_left = 0;
    lenient_continue = 0;
    lenient_numlwps = 0;
    lock_held = 0;
    unlocked_kernel_calls = 0;
    lock_hook = NULL;
    open_sockets = 0;
    memset(&g_debug_ctx, 0, sizeof(g_debug_ctx));
    g_orphan_pid = 0;
    g_orphan_stop_req = 0;
    g_orphan_sig = 0;
    g_orphan_stopped = 0;
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

// Released, and no stop the server asked for is left to stop it later.
static int released(struct fproc *p) {
    return !p->traced && p->detaches == 1 && !p->stop_pending && !p->stop_taken &&
           p->detached_with_stop_pending == 0;
}

static int delivered_count(struct fproc *p, int sig) {
    int n = 0;
    for (int i = 0; i < p->ndelivered && i < 8; i++) n += p->delivered[i] == sig;
    return n;
}

// A session target stopped (and collected), e.g. at a breakpoint.
static void hold_stopped(int pid) {
    struct fproc *p = proc(pid);
    p->stopped = 1;
    p->collected = 1;
    p->stop_sig = SIGTRAP;
}

// ---- disconnect ownership ------------------------------------------------------

// A disconnect racing a detach and re-attach by another client.
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
    hold_stopped(100);
    int a_sock = cli_a.dbgctx.dbgfd;

    // A disconnects; before A gets the lock, B detaches A's session and
    // attaches a new target.
    lock_hook = race_b_detaches_and_reattaches;
    debug_client_disconnect(&cli_a);

    CHECK(!proc(100)->traced);                 // A's session released by B
    CHECK(closed_fds[a_sock] == 1);            // A's socket closed once
    CHECK(g_debug_ctx.attached && g_debug_ctx.pid == 200);
    CHECK(curdbgcli == &cli_b && cli_b.debugging);
    CHECK(proc(200)->traced && proc(200)->detaches == 0); // B untouched
    CHECK(closed_fds[cli_b.dbgctx.dbgfd] == 0);

    // B's own disconnect does tear its session down.
    hold_stopped(200);
    debug_client_disconnect(&cli_b);
    CHECK(!g_debug_ctx.attached && curdbgcli == NULL);
    CHECK(!proc(200)->traced);
    CHECK(unlocked_kernel_calls == 0 && !lock_held);
}

// A client whose debugging flag is still set but no longer owns the session.
static void test_disconnect_not_owner(void) {
    reset();
    add_proc(100, 0);
    CHECK(attach(&cli_b, 100) == CMD_SUCCESS);
    cli_a.debugging = 1;
    debug_client_disconnect(&cli_a);
    CHECK(g_debug_ctx.attached && curdbgcli == &cli_b && proc(100)->traced);
}

// ---- attach --------------------------------------------------------------------

// The attach does not complete before a stop was seen, even on a kernel that
// would let PT_CONTINUE through on a running target.
static void test_attach_needs_stop(void) {
    reset();
    lenient_continue = 1;
    add_proc(700, NEVER);
    CHECK(attach(&cli_a, 700) == CMD_ERROR);
    CHECK(!g_debug_ctx.attached && !cli_a.debugging);
}

// A normal attach and detach.
static void test_attach_detach(void) {
    reset();
    struct fproc *p = add_proc(100, 0);
    CHECK(attach(&cli_a, 100) == CMD_SUCCESS);
    CHECK(g_debug_ctx.attached && p->traced && !p->stopped && p->sigstops == 0);
    hold_stopped(100);
    CHECK(detach(&cli_a) == CMD_SUCCESS);
    CHECK(!p->traced && !g_debug_ctx.attached);
    CHECK(unlocked_kernel_calls == 0 && !lock_held);
}

// ---- failed attach: release, kept pid, retry ------------------------------------

// The stop arrives late (after the 1 s attach wait): the attach fails and the
// target is released once PT_ATTACH's SIGSTOP is reported, no pid kept.
static void test_failed_attach_late_stop(void) {
    reset();
    struct fproc *p = add_proc(100, 1500000);
    CHECK(attach(&cli_a, 100) == CMD_ERROR);
    CHECK(released(p) && g_orphan_pid == 0 && p->sigstops == 0);
    CHECK(closed_fds[41] == 1);                 // interrupt socket closed
    CHECK(!g_debug_ctx.attached && !cli_a.debugging);
}

// The stop was seen but PT_CONTINUE failed: released at once.
static void test_failed_attach_continue_fails(void) {
    reset();
    struct fproc *p = add_proc(100, 0);
    continue_fail_left = 1;
    CHECK(attach(&cli_a, 100) == CMD_ERROR);
    CHECK(released(p) && g_orphan_pid == 0 && p->sigstops == 0);
}

// The stop never comes: the pid is kept (not a session), attaches get
// CMD_ALREADY_DEBUG, and the retry from DETACH or ATTACH releases it once
// PT_ATTACH's SIGSTOP is reported.
static void test_kept_pid_retry(void) {
    reset();
    struct fproc *p = add_proc(100, NEVER);
    add_proc(300, 0);
    CHECK(attach(&cli_a, 100) == CMD_ERROR);
    CHECK(p->traced && g_orphan_pid == 100 && g_orphan_stop_req == 1);
    CHECK(!g_debug_ctx.attached && getregs(&cli_b) == CMD_ERROR);
    CHECK(attach(&cli_b, 300) == CMD_ALREADY_DEBUG && !proc(300)->traced);
    CHECK(detach(&cli_b) == CMD_ERROR);         // still no stop: kept
    CHECK(g_orphan_pid == 100 && p->sigstops == 0);

    p->stop_due = now_us + 20000;               // the stop now comes
    CHECK(attach(&cli_b, 300) == CMD_SUCCESS);  // retry first, then attach
    CHECK(released(p) && g_orphan_pid == 0 && p->sigstops == 0);
    CHECK(proc(300)->traced && curdbgcli == &cli_b);
    CHECK(unlocked_kernel_calls == 0 && !lock_held);

    // Same through DETACH.
    reset();
    p = add_proc(100, NEVER);
    CHECK(attach(&cli_a, 100) == CMD_ERROR);
    p->stop_due = now_us;
    CHECK(detach(&cli_b) == CMD_SUCCESS);
    CHECK(released(p) && g_orphan_pid == 0);
}

// PT_DETACH fails: kept, and retried.
static void test_kept_pid_detach_fails(void) {
    reset();
    struct fproc *p = add_proc(100, 0);
    continue_fail_left = 1;
    detach_busy_left = 1;
    CHECK(attach(&cli_a, 100) == CMD_ERROR);
    CHECK(p->traced && g_orphan_pid == 100 && g_orphan_stop_req == 0);
    CHECK(detach(&cli_b) == CMD_SUCCESS);
    CHECK(released(p) && g_orphan_pid == 0 && p->sigstops == 0);
}

// Other stops before PT_ATTACH's SIGSTOP: their signals are passed on (a
// SIGTRAP too, it can only be the game's), and the release still waits for
// the SIGSTOP.
static void test_failed_attach_passes_signals(void) {
    int sigs[2] = { SIGUSR1_, SIGTRAP };
    for (int k = 0; k < 2; k++) {
        reset();
        struct fproc *p = add_proc(100, 50000); // PT_ATTACH's SIGSTOP lands late
        p->ext_sig = sigs[k];                    // the game stops first
        p->ext_due = 10000;
        continue_fail_left = 1;                  // the attach fails then
        CHECK(attach(&cli_a, 100) == CMD_ERROR);
        CHECK(released(p) && g_orphan_pid == 0 && p->sigstops == 0);
        CHECK(delivered_count(p, sigs[k]) == 1);
    }
}

// Another stop reported while the release waits for PT_ATTACH's SIGSTOP (the
// attach wait already timed out): it does not count as that SIGSTOP, and its
// signal is passed on.
static void test_release_sees_other_stop(void) {
    int sigs[2] = { SIGUSR1_, SIGTRAP };
    for (int k = 0; k < 2; k++) {
        reset();
        struct fproc *p = add_proc(100, 1500000);   // SIGSTOP after the 1 s wait
        p->ext_sig = sigs[k];
        p->ext_due = 1200000;                         // lands during the release
        CHECK(attach(&cli_a, 100) == CMD_ERROR);
        CHECK(released(p) && g_orphan_pid == 0 && p->sigstops == 0);
        CHECK(delivered_count(p, sigs[k]) == 1);
    }
}

// No SIGSTOP of the server's: a thread may already have taken PT_ATTACH's
// (its stop not reported yet), and a second one would be queued apart and
// stop the released target.
static void test_no_second_sigstop(void) {
    // On the release right after the 1 s attach wait.
    reset();
    struct fproc *p = add_proc(100, 1500000);
    p->take_sigstop_early = 1;
    CHECK(attach(&cli_a, 100) == CMD_ERROR);
    CHECK(released(p) && p->sigstops == 0);

    // On the retry of a kept pid.
    reset();
    p = add_proc(100, NEVER);
    p->take_sigstop_early = 1;
    CHECK(attach(&cli_a, 100) == CMD_ERROR);
    CHECK(g_orphan_pid == 100 && p->stop_taken);
    CHECK(detach(&cli_b) == CMD_ERROR);         // retried, not reported yet
    p->taken_due = now_us + 20000;
    CHECK(detach(&cli_b) == CMD_SUCCESS);
    CHECK(released(p) && p->sigstops == 0);
}

// The accepted exception: a thread has taken PT_ATTACH's SIGSTOP, the attach
// wait times out, and an outside SIGSTOP is reported first. wait4 gives only
// the signal number, so it is counted as the queued one: the release sends
// no SIGSTOP, drops the pid, and leaves exactly the original stop
// outstanding, which then stops the target after detach.
static void test_outside_sigstop_first(void) {
    reset();
    struct fproc *p = add_proc(100, 1500000);   // taken now, completes at 1.5 s
    p->take_sigstop_early = 1;
    p->ext_sig = SIGSTOP;                         // outside SIGSTOP at 1.2 s
    p->ext_due = 1200000;
    CHECK(attach(&cli_a, 100) == CMD_ERROR);
    CHECK(p->sigstops == 0);
    CHECK(g_orphan_pid == 0 && !p->traced && p->detaches == 1);
    CHECK(p->stop_taken && !p->stop_pending);    // the original, only
    CHECK(p->detached_with_stop_pending == 1);
}

// On a kernel that lets ptrace requests through on a running target, a
// probe cannot tell that the target is stopped: PT_GETNUMLWPS succeeds while
// it runs, and a PT_CONTINUE then clears the SIGSTOP a thread has taken. The
// release relies on the stop it has seen instead.
static void test_lenient_kernel_tracks_stop(void) {
    reset();
    lenient_numlwps = 1;
    lenient_continue = 1;
    struct fproc *p = add_proc(100, 1500000);   // taken now, completes at 1.5 s
    p->take_sigstop_early = 1;
    CHECK(attach(&cli_a, 100) == CMD_ERROR);
    CHECK(released(p) && g_orphan_pid == 0 && p->sigstops == 0);
}

// A SIGCONT removes PT_ATTACH's queued SIGSTOP (CMD_DEBUG_PROCESS_STOP resume
// sends one). The server cannot tell that from a SIGSTOP still on its way,
// so it sends none and keeps the pid until a SIGSTOP is reported or the
// target exits.
static void test_sigcont_keeps_pid(void) {
    reset();
    struct fproc *p = add_proc(800, NEVER);
    CHECK(attach(&cli_a, 800) == CMD_ERROR);
    CHECK(g_orphan_pid == 800 && p->stop_pending);
    uint8_t body[5] = { 0x20, 0x03, 0, 0, 0 };   // pid 800, state 0 = resume
    struct cmd_packet pk = { 0, CMD_DEBUG_PROCESS_STOP, sizeof(body), body };
    debug_process_stop_handle(cli_b.fd, &pk);  // runs without the lock, as on master
    unlocked_kernel_calls = 0;
    CHECK(!p->stop_pending && p->other_signals == 1);
    p->stop_delay_us = 20000;                   // a SIGSTOP would land now
    CHECK(detach(&cli_b) == CMD_ERROR);          // but none is sent: kept
    CHECK(p->traced && g_orphan_pid == 800 && p->sigstops == 0);
    CHECK(attach(&cli_b, 800) == CMD_ALREADY_DEBUG);
    p->exists = 0;                               // the target exits
    CHECK(detach(&cli_b) == CMD_SUCCESS && g_orphan_pid == 0);
    CHECK(unlocked_kernel_calls == 0 && !lock_held);
}

// wait4 errors: only ECHILD drops the pid, and then without touching it.
static void test_kept_pid_wait4_errors(void) {
    reset();
    struct fproc *p = add_proc(600, NEVER);
    CHECK(attach(&cli_a, 600) == CMD_ERROR);
    wait4_fail_errno = EINTR;
    CHECK(detach(&cli_b) == CMD_ERROR);
    CHECK(g_orphan_pid == 600 && p->traced);

    p->traced = 0;                               // the pid is no longer ours
    p->stop_pending = 0;
    CHECK(detach(&cli_b) == CMD_SUCCESS && g_orphan_pid == 0);
    CHECK(p->sigstops == 0 && p->other_signals == 0 && p->detaches == 0);
}

int main(void) {
    test_disconnect_race();
    test_disconnect_not_owner();
    test_attach_needs_stop();
    test_attach_detach();
    test_failed_attach_late_stop();
    test_failed_attach_continue_fails();
    test_kept_pid_retry();
    test_kept_pid_detach_fails();
    test_failed_attach_passes_signals();
    test_release_sees_other_stop();
    test_no_second_sigstop();
    test_outside_sigstop_first();
    test_lenient_kernel_tracks_stop();
    test_sigcont_keeps_pid();
    test_kept_pid_wait4_errors();

    if (failures) {
        char m[64];
        snprintf(m, sizeof(m), "%d test(s) failed\n", failures);
        say(m);
        return 1;
    }
    say("test_attach_session: all tests passed\n");
    return 0;
}
