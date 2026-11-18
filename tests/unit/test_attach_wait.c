// SPDX-License-Identifier: GPL-3.0-only
//
// Host-side tests for the wait4() helpers used by CMD_DEBUG_ATTACH.
// Build and run with: make -C tests/unit (or make -C debugger test)

#include <stdio.h>

#include "attach_wait.h"

static int failures;

#define CHECK(cond) do { \
    if (!(cond)) { \
        fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); \
        failures++; \
    } \
} while (0)

// FreeBSD wait status encodings (sys/wait.h).
#define STOPPED(sig) (((sig) << 8) | 0x7f)
#define EXITED(code) ((code) << 8)
#define KILLED(sig)  (sig)

int main(void) {
    // Only a reported stop lets the attach continue.
    CHECK(attach_wait_stopped(STOPPED(17)));   // SIGSTOP from PT_ATTACH
    CHECK(attach_wait_stopped(STOPPED(5)));    // any stop signal
    CHECK(!attach_wait_stopped(EXITED(0)));
    CHECK(!attach_wait_stopped(EXITED(1)));
    CHECK(!attach_wait_stopped(KILLED(9)));
    CHECK(!attach_wait_stopped(0));            // no status filled in

    // Pending detach: still ours while wait4 reports nothing or a stop.
    CHECK(!attach_wait_gone(0, 0, 0));            // running, no status yet
    CHECK(!attach_wait_gone(1234, STOPPED(17), 0)); // stop collected: detach now

    // A failed wait4 only drops the pid on ECHILD; other errors prove
    // nothing about the trace, so the pid is kept.
    CHECK(!attach_wait_gone(-1, 0, 4));           // EINTR
    CHECK(!attach_wait_gone(-1, 0, 14));          // EFAULT
    CHECK(!attach_wait_gone(-1, 0, 22));          // EINVAL
    CHECK(!attach_wait_gone(-1, 0, 0));           // no errno

    // Dropped once wait4 shows it is not ours any more.
    CHECK(attach_wait_gone(-1, 0, ATTACH_ECHILD)); // ECHILD: not traced by us
    CHECK(attach_wait_gone(1234, EXITED(0), 0));   // exited (and reaped)
    CHECK(attach_wait_gone(1234, KILLED(9), 0));   // killed (and reaped)

    if (failures) {
        fprintf(stderr, "%d test(s) failed\n", failures);
        return 1;
    }
    printf("test_attach_wait: all tests passed\n");
    return 0;
}
