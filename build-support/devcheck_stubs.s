# SPDX-License-Identifier: GPL-3.0-only
#
# Dev-check-only stub for the Windows build (build.ps1).
#
# libPS4's syscall.s routes failed syscalls through __error(), which the
# userspace debugger gets from libPS4's kernel.c (initialized against
# libkernel). The installer is kernel-side and has no such provider; this
# fallback keeps the syscall error path well-defined in the dev-check build.
# The release build (build.sh) does not use this file.

.intel_syntax noprefix
.text
.globl __error
__error:
    lea rax, [rip + devcheck_errno_slot]
    ret

.data
.align 4
devcheck_errno_slot:
    .int 0
