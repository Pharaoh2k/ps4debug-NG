// SPDX-License-Identifier: GPL-3.0-only

#ifndef _ATTACH_WAIT_H
#define _ATTACH_WAIT_H

// wait4() helpers for CMD_DEBUG_ATTACH. No SDK dependency, so they can be
// unit-tested on the host.

// FreeBSD WIFSTOPPED().
static inline int attach_wait_stopped(int status) {
    return (status & 0x7f) == 0x7f;
}

#define ATTACH_ECHILD 10 // FreeBSD ECHILD

// Whether a wait4(pid, &status, WNOHANG) result (and errno when it fails)
// shows the target is no longer ours to detach: ECHILD (a traced process is
// reported to its tracer by wait4, so it is no longer traced by us), or it
// exited or was killed. Other errors (EINTR, EFAULT, EINVAL, ...) prove
// nothing, so the target is kept.
static inline int attach_wait_gone(int ret, int status, int err) {
    if (ret < 0) {
        return err == ATTACH_ECHILD;
    }
    return ret > 0 && !attach_wait_stopped(status);
}

#endif
