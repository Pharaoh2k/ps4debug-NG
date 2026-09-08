// SPDX-License-Identifier: GPL-3.0-only

#include "kdbg.h"
#include "../../syscalls.h"

void prefault(void *address, size_t size) {
    for(uint64_t i = 0; i < size; i++) {
        volatile uint8_t c;
        (void)c;

        c = ((char *)address)[i];
    }
}

void *net_alloc_buffer(size_t size) {
    void *p = malloc(size);
    if (p) prefault(p, size);
    return p;
}

int sys_proc_list(struct proc_list_entry *procs, uint64_t *num) {
    return syscall(PS4DEBUG_SYS_PROC_LIST, procs, num);
}

int sys_proc_rw(uint64_t pid, uint64_t address, void *data, uint64_t length, uint64_t write) {
    return syscall(PS4DEBUG_SYS_PROC_RW, pid, address, data, length, write);
}

int sys_proc_cmd(uint64_t pid, uint64_t cmd, void *data) {
    return syscall(PS4DEBUG_SYS_PROC_CMD, pid, cmd, data);
}

int sys_kern_base(uint64_t *kbase) {
    return syscall(PS4DEBUG_SYS_KERN_BASE, kbase);
}

int sys_kern_rw(uint64_t address, void *data, uint64_t length, uint64_t write) {
    return syscall(PS4DEBUG_SYS_KERN_RW, address, data, length, write);
}

int sys_console_cmd(uint64_t cmd, void *data) {
    return syscall(PS4DEBUG_SYS_CONSOLE_CMD, cmd, data);
}
