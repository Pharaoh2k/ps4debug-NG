// SPDX-License-Identifier: GPL-3.0-only

#include "kdbg.h"

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
    return syscall(107, procs, num);
}

int sys_proc_rw(uint64_t pid, uint64_t address, void *data, uint64_t length, uint64_t write) {
    return syscall(108, pid, address, data, length, write);
}

int sys_proc_cmd(uint64_t pid, uint64_t cmd, void *data) {
    return syscall(109, pid, cmd, data);
}

int sys_kern_base(uint64_t *kbase) {
    return syscall(110, kbase);
}

int sys_kern_rw(uint64_t address, void *data, uint64_t length, uint64_t write) {
    return syscall(111, address, data, length, write);
}

int sys_console_cmd(uint64_t cmd, void *data) {
    return syscall(112, cmd, data);
}
