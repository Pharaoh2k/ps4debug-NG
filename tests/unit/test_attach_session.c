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
#define STOPPED17 ((17 << 8) | 0x7f)

struct fproc {
    int  pid;
    int  exists;
    int  traced;
    int  stopped;
    int  collected;     // stop reported by wait4 (FreeBSD P_WAITED)
    int  stop_pending;  // a SIGSTOP is queued
    long long stop_due; // when the queued SIGSTOP takes effect
    long long stop_delay_us; // delay for SIGSTOP from kill() / PT_ATTACH
    int  detaches;
    int  sigstops;
    int  other_signals;
    int  detached_with_stop_pending; // would freeze untraced: must stay 0
};

static struct fproc procs[NPROC];
static long long now_us;
static int fake_errno;
static int wait4_fail_errno;   // next wait4 fails with this errno (once)
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
            procs[i].stop_delay_us = stop_delay_us;
            return &procs[i];
        }
    }
    return NULL;
}

static void settle(struct fproc *p) {
    if (p->stop_pending && now_us >= p->stop_due) {
        p->stop_pending = 0;
        p->stop_due = NEVER;
        p->stopped = 1;
        p->collected = 0;
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

// ---- fake libPS4 / kernel / debugger externals ------------------------------

int *__error(void) { return &fake_errno; }

static int fake_ptrace(int op, int pid, void *addr, int data) {
    (void)data;
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
    case PT_CONTINUE:
        if (lenient_continue && p->traced) return 0;
        // fall through
    case PT_DETACH:
    case PT_GETNUMLWPS:
    case PT_GETLWPLIST:
        if (!p->traced) return fail(EPERM);
        if (!p->stopped || !p->collected) return fail(EBUSY);
        if (op == PT_GETNUMLWPS) return 1;
        if (op == PT_GETLWPLIST) { ((uint32_t *)addr)[0] = (uint32_t)pid * 10; return 1; }
        if (op == PT_DETACH) {
            p->traced = 0;
            p->detaches++;
            if (p->stop_pending) p->detached_with_stop_pending++;
        }
        p->stopped = 0;
        p->collected = 0;
        return 0;
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
        *status = STOPPED17;
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
    lenient_continue = 0;
    lock_held = 0;
    unlocked_kernel_calls = 0;
    lock_hook = NULL;
    open_sockets = 0;
    memset(&g_debug_ctx, 0, sizeof(g_debug_ctx));
    g_orphan_pid = 0;
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
    CHECK(detach(&cli_a) == CMD_SUCCESS);
    CHECK(!proc(100)->traced && proc(100)->sigstops == 0);
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

int main(void) {
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
