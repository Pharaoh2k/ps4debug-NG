// SPDX-License-Identifier: GPL-3.0-only

#include "ksdk.h"

uint64_t cachedKernelBase = 0;

void(*Xfast_syscall)(void) = 0;
int(*printf)(const char *fmt, ... ) = 0;
void *(*malloc)(uint64_t size, void *type, int flags) = 0;
void(*free)(void *addr, void *type) = 0;
void *(*memcpy)(void *dest, const void *src, uint64_t num) = 0;
void *(*memset)(void *ptr, int value, uint64_t num) = 0;
int(*memcmp)(const void *ptr1, const void *ptr2, uint64_t num) = 0;
void *(*kmem_alloc)(struct vm_map *map, uint64_t size) = 0;
uint64_t(*strlen)(const char *str) = 0;
int(*create_thread)(struct thread *td, uint64_t ctx, void (*start_func)(void *), void *arg, char *stack_base, uint64_t stack_size, char *tls_base, long *child_tid, long *parent_tid, uint64_t flags, uint64_t rtp) = 0;
int(*kern_reboot)(int magic) = 0;
void(*vm_map_lock_read)(struct vm_map *map) = 0;
int(*vm_map_lookup_entry)(struct vm_map *map, uint64_t address, struct vm_map_entry **entries) = 0;
void(*vm_map_unlock_read)(struct vm_map *map) = 0;
int(*vm_map_delete)(struct vm_map *map, uint64_t start, uint64_t end) = 0;
int(*vm_map_protect)(struct vm_map *map, uint64_t start, uint64_t end, int new_prot, uint64_t set_max) = 0;
int(*vm_map_findspace)(struct vm_map *map, uint64_t start, uint64_t length, uint64_t *addr) = 0;
int(*vm_map_insert)(struct vm_map *map, uint64_t object, uint64_t offset, uint64_t start, uint64_t end, int prot, int max, int cow) = 0;
void(*vm_map_lock)(struct vm_map *map) = 0;
void(*vm_map_unlock)(struct vm_map *map) = 0;
int(*proc_rwmem)(struct proc *p, struct uio *uio) = 0;

uint8_t *disable_console_output = 0;
void *M_TEMP = 0;
void **kernel_map = 0;
void **prison0 = 0;
void **rootvnode = 0;
void **allproc = 0;
struct sysent *sysents = 0;

uint64_t get_kbase() {
    uint32_t edx, eax;
    __asm__ ("rdmsr" : "=d"(edx), "=a"(eax) : "c"(0xC0000082));
    return ((((uint64_t)edx) << 32) | (uint64_t)eax) - __Xfast_syscall;
}

void init_505sdk(uint8_t *kbase) {
    Xfast_syscall = (void *)(kbase + 0x1C0);
    printf = (void *)(kbase + 0x436040);
    malloc = (void *)(kbase + 0x10E250);
    free = (void *)(kbase + 0x10E460);
    memcpy = (void *)(kbase + 0x1EA530);
    memset = (void *)(kbase + 0x3205C0);
    memcmp = (void *)(kbase + 0x50AC0);
    kmem_alloc = (void *)(kbase + 0xFCC80);
    strlen = (void *)(kbase + 0x3B71A0);
    create_thread = (void *)(kbase + 0x1BE1F0);
    kern_reboot = (void *)(kbase + 0x10D390);
    vm_map_lock_read = (void *)(kbase + 0x19F140);
    vm_map_lookup_entry = (void *)(kbase + 0x19F760);
    vm_map_unlock_read = (void *)(kbase + 0x19F190);
    vm_map_delete = (void *)(kbase + 0x1A19D0);
    vm_map_protect = (void *)(kbase + 0x1A3A50);
    vm_map_findspace = (void *)(kbase + 0x1A1F60);
    vm_map_insert = (void *)(kbase + 0x1A0280);
    vm_map_lock = (void *)(kbase + 0x19EFF0);
    vm_map_unlock = (void *)(kbase + 0x19F060);
    proc_rwmem = (void *)(kbase + 0x30D150);
    disable_console_output = (void *)(kbase + 0x19ECEB0);
    M_TEMP = (void *)(kbase + 0x14B4110);
    kernel_map = (void *)(kbase + 0x1AC60E0);
    prison0 = (void *)(kbase + 0x10986A0);
    rootvnode = (void *)(kbase + 0x22C1A70);
    allproc = (void *)(kbase + 0x2382FF8);
    sysents = (void *)(kbase + 0x107C610);
}

void init_1102sdk(uint8_t *kbase) {
    Xfast_syscall = (void *)(kbase + 0x1C0);
    printf = (void *)(kbase + 0x2FCBF0);
    malloc = (void *)(kbase + 0x1A4240);
    free = (void *)(kbase + 0x1A4400);
    memcpy = (void *)(kbase + 0x2DDE10);
    memset = (void *)(kbase + 0x482D0);
    memcmp = (void *)(kbase + 0x948B0);
    kmem_alloc = (void *)(kbase + 0x245E30);
    strlen = (void *)(kbase + 0x21DC60);
    create_thread = (void *)(kbase + 0x295190);
    kern_reboot = (void *)(kbase + 0x198080);
    vm_map_lock_read = (void *)(kbase + 0x3578D0);
    vm_map_lookup_entry = (void *)(kbase + 0x357F10);
    vm_map_unlock_read = (void *)(kbase + 0x357920);
    vm_map_delete = (void *)(kbase + 0x35A3D0);
    vm_map_protect = (void *)(kbase + 0x35C730);
    vm_map_findspace = (void *)(kbase + 0x35A990);
    vm_map_insert = (void *)(kbase + 0x358AD0);
    vm_map_lock = (void *)(kbase + 0x357780);
    vm_map_unlock = (void *)(kbase + 0x3577F0);
    proc_rwmem = (void *)(kbase + 0x3838C0);
    disable_console_output = (void *)(kbase + 0x152CFF8);
    M_TEMP = (void *)(kbase + 0x15415B0);
    kernel_map = (void *)(kbase + 0x21FF130);
    prison0 = (void *)(kbase + 0x111F830);
    rootvnode = (void *)(kbase + 0x2116640);
    allproc = (void *)(kbase + 0x22D0A98);
    sysents = (void *)(kbase + 0x1101760);
}

void init_1252sdk(uint8_t *kbase) {
    Xfast_syscall = (void *)(kbase + 0x1C0);
    printf = (void *)(kbase + 0x2E0420);
    malloc = (void *)(kbase + 0x9520);
    free = (void *)(kbase + 0x96E0);
    memcpy = (void *)(kbase + 0x2BD4C0);
    memset = (void *)(kbase + 0x1FA180);
    memcmp = (void *)(kbase + 0x3942E0);
    kmem_alloc = (void *)(kbase + 0x465A20);
    strlen = (void *)(kbase + 0x36AB70);
    create_thread = (void *)(kbase + 0x4C6C0);
    kern_reboot = (void *)(kbase + 0x3A1DB0);
    vm_map_lock_read = (void *)(kbase + 0x2F70F0);
    vm_map_lookup_entry = (void *)(kbase + 0x2F7730);
    vm_map_unlock_read = (void *)(kbase + 0x2F7140);
    vm_map_delete = (void *)(kbase + 0x2F9BF0);
    vm_map_protect = (void *)(kbase + 0x2FBF50);
    vm_map_findspace = (void *)(kbase + 0x2FA1B0);
    vm_map_insert = (void *)(kbase + 0x2F82F0);
    vm_map_lock = (void *)(kbase + 0x2F6FA0);
    vm_map_unlock = (void *)(kbase + 0x2F7010);
    proc_rwmem = (void *)(kbase + 0x365FE0);
    disable_console_output = (void *)(kbase + 0x1A47F40);
    M_TEMP = (void *)(kbase + 0x1520D00);
    kernel_map = (void *)(kbase + 0x22D1D50);
    prison0 = (void *)(kbase + 0x111FA18);
    rootvnode = (void *)(kbase + 0x2136E90);
    allproc = (void *)(kbase + 0x1B28538);
    sysents = (void *)(kbase + 0x1102B70);
}

void init_1152sdk(uint8_t *kbase) {
    Xfast_syscall = (void *)(kbase + 0x1C0);
    printf = (void *)(kbase + 0x2E01A0);
    malloc = (void *)(kbase + 0x9520);
    free = (void *)(kbase + 0x96E0);
    memcpy = (void *)(kbase + 0x2BD3A0);
    memset = (void *)(kbase + 0x1FA060);
    memcmp = (void *)(kbase + 0x394060);
    kmem_alloc = (void *)(kbase + 0x4657A0);
    strlen = (void *)(kbase + 0x36A8F0);
    create_thread = (void *)(kbase + 0x4C6C0);
    kern_reboot = (void *)(kbase + 0x3A1B30);
    vm_map_lock_read = (void *)(kbase + 0x2F6E70);
    vm_map_lookup_entry = (void *)(kbase + 0x2F74B0);
    vm_map_unlock_read = (void *)(kbase + 0x2F6EC0);
    vm_map_delete = (void *)(kbase + 0x2F9970);
    vm_map_protect = (void *)(kbase + 0x2FBCD0);
    vm_map_findspace = (void *)(kbase + 0x2F9F30);
    vm_map_insert = (void *)(kbase + 0x2F8070);
    vm_map_lock = (void *)(kbase + 0x2F6D20);
    vm_map_unlock = (void *)(kbase + 0x2F6D90);
    proc_rwmem = (void *)(kbase + 0x365D60);
    disable_console_output = (void *)(kbase + 0x1A47F40);
    M_TEMP = (void *)(kbase + 0x1520D00);
    kernel_map = (void *)(kbase + 0x22D1D50);
    prison0 = (void *)(kbase + 0x111FA18);
    rootvnode = (void *)(kbase + 0x2136E90);
    allproc = (void *)(kbase + 0x1B28538);
    sysents = (void *)(kbase + 0x1102B70);
}

void init_1202sdk(uint8_t *kbase) {
    Xfast_syscall = (void *)(kbase + 0x1C0);
    printf = (void *)(kbase + 0x2E03E0);
    malloc = (void *)(kbase + 0x9520);
    free = (void *)(kbase + 0x96E0);
    memcpy = (void *)(kbase + 0x2BD480);
    memset = (void *)(kbase + 0x1FA140);
    memcmp = (void *)(kbase + 0x3942A0);
    kmem_alloc = (void *)(kbase + 0x4659E0);
    strlen = (void *)(kbase + 0x36AB30);
    create_thread = (void *)(kbase + 0x4C6C0);
    kern_reboot = (void *)(kbase + 0x3A1D70);
    vm_map_lock_read = (void *)(kbase + 0x2F70B0);
    vm_map_lookup_entry = (void *)(kbase + 0x2F76F0);
    vm_map_unlock_read = (void *)(kbase + 0x2F7100);
    vm_map_delete = (void *)(kbase + 0x2F9BB0);
    vm_map_protect = (void *)(kbase + 0x2FBF10);
    vm_map_findspace = (void *)(kbase + 0x2FA170);
    vm_map_insert = (void *)(kbase + 0x2F82B0);
    vm_map_lock = (void *)(kbase + 0x2F6F60);
    vm_map_unlock = (void *)(kbase + 0x2F6FD0);
    proc_rwmem = (void *)(kbase + 0x365FA0);
    disable_console_output = (void *)(kbase + 0x1A47F40);
    M_TEMP = (void *)(kbase + 0x1520D00);
    kernel_map = (void *)(kbase + 0x22D1D50);
    prison0 = (void *)(kbase + 0x111FA18);
    rootvnode = (void *)(kbase + 0x2136E90);
    allproc = (void *)(kbase + 0x1B28538);
    sysents = (void *)(kbase + 0x1102B70);
}

void init_1002sdk(uint8_t *kbase) {
    Xfast_syscall = (void *)(kbase + 0x1C0);
    printf = (void *)(kbase + 0xC50F0);
    malloc = (void *)(kbase + 0x109A60);
    free = (void *)(kbase + 0x109C20);
    memcpy = (void *)(kbase + 0x472D20);
    memset = (void *)(kbase + 0x3E6F0);
    memcmp = (void *)(kbase + 0x109940);
    kmem_alloc = (void *)(kbase + 0x33B040);
    strlen = (void *)(kbase + 0x2E0340);
    create_thread = (void *)(kbase + 0x182F0);
    kern_reboot = (void *)(kbase + 0x480CE0);
    vm_map_lock_read = (void *)(kbase + 0x38D070);
    vm_map_lookup_entry = (void *)(kbase + 0x38D6B0);
    vm_map_unlock_read = (void *)(kbase + 0x38D0C0);
    vm_map_delete = (void *)(kbase + 0x38FB70);
    vm_map_protect = (void *)(kbase + 0x391EB0);
    vm_map_findspace = (void *)(kbase + 0x390130);
    vm_map_insert = (void *)(kbase + 0x38E270);
    vm_map_lock = (void *)(kbase + 0x38CF20);
    vm_map_unlock = (void *)(kbase + 0x38CF90);
    proc_rwmem = (void *)(kbase + 0x44DC40);
    disable_console_output = (void *)(kbase + 0x1A78A78);
    M_TEMP = (void *)(kbase + 0x1532C00);
    kernel_map = (void *)(kbase + 0x227BEF8);
    prison0 = (void *)(kbase + 0x111B8B0);
    rootvnode = (void *)(kbase + 0x1B25BD0);
    allproc = (void *)(kbase + 0x22D9B40);
    sysents = (void *)(kbase + 0x1102D90);
}

void init_960sdk(uint8_t *kbase) {
    Xfast_syscall = (void *)(kbase + 0x1C0);
    printf = (void *)(kbase + 0x205470);
    malloc = (void *)(kbase + 0x29D330);
    free = (void *)(kbase + 0x29D4F0);
    memcpy = (void *)(kbase + 0x201CC0);
    memset = (void *)(kbase + 0xC1720);
    memcmp = (void *)(kbase + 0x47CB80);
    kmem_alloc = (void *)(kbase + 0x1889D0);
    strlen = (void *)(kbase + 0x3F1980);
    create_thread = (void *)(kbase + 0x1EC430);
    kern_reboot = (void *)(kbase + 0x331DF0);
    vm_map_lock_read = (void *)(kbase + 0x191D30);
    vm_map_lookup_entry = (void *)(kbase + 0x192370);
    vm_map_unlock_read = (void *)(kbase + 0x191D80);
    vm_map_delete = (void *)(kbase + 0x194830);
    vm_map_protect = (void *)(kbase + 0x196B70);
    vm_map_findspace = (void *)(kbase + 0x194DF0);
    vm_map_insert = (void *)(kbase + 0x192F30);
    vm_map_lock = (void *)(kbase + 0x191BE0);
    vm_map_unlock = (void *)(kbase + 0x191C50);
    proc_rwmem = (void *)(kbase + 0x479620);
    disable_console_output = (void *)(kbase + 0x1A50BE0);
    M_TEMP = (void *)(kbase + 0x1A4ECB0);
    kernel_map = (void *)(kbase + 0x2147830);
    prison0 = (void *)(kbase + 0x11137D0);
    rootvnode = (void *)(kbase + 0x21A6C30);
    allproc = (void *)(kbase + 0x221D2A0);
    sysents = (void *)(kbase + 0x10F92F0);
}

void init_1071sdk(uint8_t *kbase) {
    Xfast_syscall = (void *)(kbase + 0x1C0);
    printf = (void *)(kbase + 0x450E80);
    malloc = (void *)(kbase + 0x36E120);
    free = (void *)(kbase + 0x36E2E0);
    memcpy = (void *)(kbase + 0xD7370);
    memset = (void *)(kbase + 0xD090);
    memcmp = (void *)(kbase + 0x2A020);
    kmem_alloc = (void *)(kbase + 0x428960);
    strlen = (void *)(kbase + 0x160DA0);
    create_thread = (void *)(kbase + 0x3384E0);
    kern_reboot = (void *)(kbase + 0x45D6D0);
    vm_map_lock_read = (void *)(kbase + 0x4762D0);
    vm_map_lookup_entry = (void *)(kbase + 0x476910);
    vm_map_unlock_read = (void *)(kbase + 0x476320);
    vm_map_delete = (void *)(kbase + 0x478DD0);
    vm_map_protect = (void *)(kbase + 0x47B110);
    vm_map_findspace = (void *)(kbase + 0x479390);
    vm_map_insert = (void *)(kbase + 0x4774D0);
    vm_map_lock = (void *)(kbase + 0x476180);
    vm_map_unlock = (void *)(kbase + 0x4761F0);
    proc_rwmem = (void *)(kbase + 0x4244A0);
    disable_console_output = (void *)(kbase + 0x1A3BCA0);
    M_TEMP = (void *)(kbase + 0x1A5FE30);
    kernel_map = (void *)(kbase + 0x22A9250);
    prison0 = (void *)(kbase + 0x111B910);
    rootvnode = (void *)(kbase + 0x1BF81F0);
    allproc = (void *)(kbase + 0x2269F30);
    sysents = (void *)(kbase + 0x11029C0);
}

void init_1100sdk(uint8_t *kbase) {
    Xfast_syscall = (void *)(kbase + 0x1C0);
    printf = (void *)(kbase + 0x2FCBD0);
    malloc = (void *)(kbase + 0x1A4220);
    free = (void *)(kbase + 0x1A43E0);
    memcpy = (void *)(kbase + 0x2DDDF0);
    memset = (void *)(kbase + 0x482D0);
    memcmp = (void *)(kbase + 0x948B0);
    kmem_alloc = (void *)(kbase + 0x245E10);
    strlen = (void *)(kbase + 0x21DC40);
    create_thread = (void *)(kbase + 0x295170);
    kern_reboot = (void *)(kbase + 0x198060);
    vm_map_lock_read = (void *)(kbase + 0x3578B0);
    vm_map_lookup_entry = (void *)(kbase + 0x357EF0);
    vm_map_unlock_read = (void *)(kbase + 0x357900);
    vm_map_delete = (void *)(kbase + 0x35A3B0);
    vm_map_protect = (void *)(kbase + 0x35C710);
    vm_map_findspace = (void *)(kbase + 0x35A970);
    vm_map_insert = (void *)(kbase + 0x358AB0);
    vm_map_lock = (void *)(kbase + 0x357760);
    vm_map_unlock = (void *)(kbase + 0x3577D0);
    proc_rwmem = (void *)(kbase + 0x3838A0);
    disable_console_output = (void *)(kbase + 0x152CFF8);
    M_TEMP = (void *)(kbase + 0x15415B0);
    kernel_map = (void *)(kbase + 0x21FF130);
    prison0 = (void *)(kbase + 0x111F830);
    rootvnode = (void *)(kbase + 0x2116640);
    allproc = (void *)(kbase + 0x22D0A98);
    sysents = (void *)(kbase + 0x1101760);
}

void init_904sdk(uint8_t *kbase) {
    Xfast_syscall = (void *)(kbase + 0x1C0);
    printf = (void *)(kbase + 0xB79E0);
    malloc = (void *)(kbase + 0x3017B0);
    free = (void *)(kbase + 0x301970);
    memcpy = (void *)(kbase + 0x271130);
    memset = (void *)(kbase + 0x149670);
    memcmp = (void *)(kbase + 0x271AA0);
    kmem_alloc = (void *)(kbase + 0x37A070);
    strlen = (void *)(kbase + 0x30F0F0);
    create_thread = (void *)(kbase + 0x1ED620);
    kern_reboot = (void *)(kbase + 0x29A000);
    vm_map_lock_read = (void *)(kbase + 0x7BB80);
    vm_map_lookup_entry = (void *)(kbase + 0x7C1C0);
    vm_map_unlock_read = (void *)(kbase + 0x7BBD0);
    vm_map_delete = (void *)(kbase + 0x7E680);
    vm_map_protect = (void *)(kbase + 0x809C0);
    vm_map_findspace = (void *)(kbase + 0x7EC40);
    vm_map_insert = (void *)(kbase + 0x7CD80);
    vm_map_lock = (void *)(kbase + 0x7BA30);
    vm_map_unlock = (void *)(kbase + 0x7BAA0);
    proc_rwmem = (void *)(kbase + 0x41CA70);
    disable_console_output = (void *)(kbase + 0x1527F60);
    M_TEMP = (void *)(kbase + 0x155E1E0);
    kernel_map = (void *)(kbase + 0x2264D48);
    prison0 = (void *)(kbase + 0x111B840);
    rootvnode = (void *)(kbase + 0x21EBF20);
    allproc = (void *)(kbase + 0x1B906E0);
    sysents = (void *)(kbase + 0x10FC310);
}

void init_900sdk(uint8_t *kbase) {
    Xfast_syscall = (void *)(kbase + 0x1C0);
    printf = (void *)(kbase + 0xB7A30);
    malloc = (void *)(kbase + 0x301B20);
    free = (void *)(kbase + 0x301CE0);
    memcpy = (void *)(kbase + 0x2714B0);
    memset = (void *)(kbase + 0x1496C0);
    memcmp = (void *)(kbase + 0x271E20);
    kmem_alloc = (void *)(kbase + 0x37BE70);
    strlen = (void *)(kbase + 0x30F450);
    create_thread = (void *)(kbase + 0x1ED670);
    kern_reboot = (void *)(kbase + 0x29A380);
    vm_map_lock_read = (void *)(kbase + 0x7BB80);
    vm_map_lookup_entry = (void *)(kbase + 0x7C1C0);
    vm_map_unlock_read = (void *)(kbase + 0x7BBD0);
    vm_map_delete = (void *)(kbase + 0x7E680);
    vm_map_protect = (void *)(kbase + 0x809C0);
    vm_map_findspace = (void *)(kbase + 0x7EC40);
    vm_map_insert = (void *)(kbase + 0x7CD80);
    vm_map_lock = (void *)(kbase + 0x7BA30);
    vm_map_unlock = (void *)(kbase + 0x7BAA0);
    proc_rwmem = (void *)(kbase + 0x41EB00);
    disable_console_output = (void *)(kbase + 0x152BF60);
    M_TEMP = (void *)(kbase + 0x15621E0);
    kernel_map = (void *)(kbase + 0x2268D48);
    prison0 = (void *)(kbase + 0x111F870);
    rootvnode = (void *)(kbase + 0x21EFF20);
    allproc = (void *)(kbase + 0x1B946E0);
    sysents = (void *)(kbase + 0x1100310);
}

void init_803sdk(uint8_t *kbase) {
    Xfast_syscall = (void *)(kbase + 0x1C0);
    printf = (void *)(kbase + 0x430AE0);
    malloc = (void *)(kbase + 0x46F7F0);
    free = (void *)(kbase + 0x46F9B0);
    memcpy = (void *)(kbase + 0x25E1C0);
    memset = (void *)(kbase + 0xF6C60);
    memcmp = (void *)(kbase + 0x195A90);
    kmem_alloc = (void *)(kbase + 0x1B3F0);
    strlen = (void *)(kbase + 0x2F6090);
    create_thread = (void *)(kbase + 0x26FA50);
    kern_reboot = (void *)(kbase + 0x155560);
    vm_map_lock_read = (void *)(kbase + 0x3E7680);
    vm_map_lookup_entry = (void *)(kbase + 0x3E7CC0);
    vm_map_unlock_read = (void *)(kbase + 0x3E76D0);
    vm_map_delete = (void *)(kbase + 0x3EA180);
    vm_map_protect = (void *)(kbase + 0x3EC4C0);
    vm_map_findspace = (void *)(kbase + 0x3EA740);
    vm_map_insert = (void *)(kbase + 0x3E8880);
    vm_map_lock = (void *)(kbase + 0x3E7530);
    vm_map_unlock = (void *)(kbase + 0x3E75A0);
    proc_rwmem = (void *)(kbase + 0x173770);
    disable_console_output = (void *)(kbase + 0x155D190);
    M_TEMP = (void *)(kbase + 0x1A77E10);
    kernel_map = (void *)(kbase + 0x1B243E0);
    prison0 = (void *)(kbase + 0x111A7D0);
    rootvnode = (void *)(kbase + 0x1B8C730);
    allproc = (void *)(kbase + 0x1B244E0);
    sysents = (void *)(kbase + 0x10FC4D0);
}

void init_852sdk(uint8_t *kbase) {
    Xfast_syscall = (void *)(kbase + 0x1C0);
    printf = (void *)(kbase + 0x15D570);
    malloc = (void *)(kbase + 0xB5A40);
    free = (void *)(kbase + 0xB5C00);
    memcpy = (void *)(kbase + 0x3A40F0);
    memset = (void *)(kbase + 0x3D6710);
    memcmp = (void *)(kbase + 0x20F280);
    kmem_alloc = (void *)(kbase + 0x2199A0);
    strlen = (void *)(kbase + 0x270C40);
    create_thread = (void *)(kbase + 0x392440);
    kern_reboot = (void *)(kbase + 0x40B420);
    vm_map_lock_read = (void *)(kbase + 0x1486D0);
    vm_map_lookup_entry = (void *)(kbase + 0x148D10);
    vm_map_unlock_read = (void *)(kbase + 0x148720);
    vm_map_delete = (void *)(kbase + 0x14B1D0);
    vm_map_protect = (void *)(kbase + 0x14D510);
    vm_map_findspace = (void *)(kbase + 0x14B790);
    vm_map_insert = (void *)(kbase + 0x1498D0);
    vm_map_lock = (void *)(kbase + 0x148580);
    vm_map_unlock = (void *)(kbase + 0x1485F0);
    proc_rwmem = (void *)(kbase + 0x131B50);
    disable_console_output = (void *)(kbase + 0x153AE88);
    M_TEMP = (void *)(kbase + 0x1528FF0);
    kernel_map = (void *)(kbase + 0x1C64228);
    prison0 = (void *)(kbase + 0x111A8F0);
    rootvnode = (void *)(kbase + 0x1C66150);
    allproc = (void *)(kbase + 0x1BD72D8);
    sysents = (void *)(kbase + 0x10FC5C0);
}

void init_755sdk(uint8_t *kbase) {
    Xfast_syscall = (void *)(kbase + 0x1C0);
    printf = (void *)(kbase + 0x26F740);
    malloc = (void *)(kbase + 0x1D6680);
    free = (void *)(kbase + 0x1D6870);
    memcpy = (void *)(kbase + 0x28F800);
    memset = (void *)(kbase + 0x8D6F0);
    memcmp = (void *)(kbase + 0x31D250);
    kmem_alloc = (void *)(kbase + 0x1753E0);
    strlen = (void *)(kbase + 0x2E8BC0);
    create_thread = (void *)(kbase + 0x47AB60);
    kern_reboot = (void *)(kbase + 0xD28E0);
    vm_map_lock_read = (void *)(kbase + 0x2FC430);
    vm_map_lookup_entry = (void *)(kbase + 0x2FCA70);
    vm_map_unlock_read = (void *)(kbase + 0x2FC480);
    vm_map_delete = (void *)(kbase + 0x2FEFA0);
    vm_map_protect = (void *)(kbase + 0x3012F0);
    vm_map_findspace = (void *)(kbase + 0x2FF560);
    vm_map_insert = (void *)(kbase + 0x2FD640);
    vm_map_lock = (void *)(kbase + 0x2FC2E0);
    vm_map_unlock = (void *)(kbase + 0x2FC350);
    proc_rwmem = (void *)(kbase + 0x361310);
    disable_console_output = (void *)(kbase + 0x1564910);
    M_TEMP = (void *)(kbase + 0x1556DA0);
    kernel_map = (void *)(kbase + 0x21405B8);
    prison0 = (void *)(kbase + 0x113B728);
    rootvnode = (void *)(kbase + 0x1B463E0);
    allproc = (void *)(kbase + 0x213C828);
    sysents = (void *)(kbase + 0x1122340);
}

void init_672sdk(uint8_t *kbase) {
    Xfast_syscall = (void *)(kbase + 0x1C0);
    printf = (void *)(kbase + 0x123280);
    malloc = (void *)(kbase + 0xD7A0);
    free = (void *)(kbase + 0xD9A0);
    memcpy = (void *)(kbase + 0x3C15B0);
    memset = (void *)(kbase + 0x1687D0);
    memcmp = (void *)(kbase + 0x207E40);
    kmem_alloc = (void *)(kbase + 0x250730);
    strlen = (void *)(kbase + 0x2433E0);
    create_thread = (void *)(kbase + 0x4A6FB0);
    kern_reboot = (void *)(kbase + 0x206D50);
    vm_map_lock_read = (void *)(kbase + 0x44CD40);
    vm_map_lookup_entry = (void *)(kbase + 0x44D330);
    vm_map_unlock_read = (void *)(kbase + 0x44CD90);
    vm_map_delete = (void *)(kbase + 0x44F8A0);
    vm_map_protect = (void *)(kbase + 0x451BF0);
    vm_map_findspace = (void *)(kbase + 0x44FE60);
    vm_map_insert = (void *)(kbase + 0x44DEF0);
    vm_map_lock = (void *)(kbase + 0x44CBF0);
    vm_map_unlock = (void *)(kbase + 0x44CC60);
    proc_rwmem = (void *)(kbase + 0x10EE10);
    disable_console_output = (void *)(kbase + 0x1A6EB18);
    M_TEMP = (void *)(kbase + 0x1540EB0);
    kernel_map = (void *)(kbase + 0x220DFC0);
    prison0 = (void *)(kbase + 0x113E518);
    rootvnode = (void *)(kbase + 0x2300320);
    allproc = (void *)(kbase + 0x22BBE80);
    sysents = (void *)(kbase + 0x111E000);
}

void init_702sdk(uint8_t *kbase) {
    Xfast_syscall = (void *)(kbase + 0x1C0);
    printf = (void *)(kbase + 0xBC730);
    malloc = (void *)(kbase + 0x301840);
    free = (void *)(kbase + 0x301A40);
    memcpy = (void *)(kbase + 0x2F040);
    memset = (void *)(kbase + 0x2DFC20);
    memcmp = (void *)(kbase + 0x207500);
    kmem_alloc = (void *)(kbase + 0x1170F0);
    strlen = (void *)(kbase + 0x93FF0);
    create_thread = (void *)(kbase + 0x842E0);
    kern_reboot = (void *)(kbase + 0x2CD780);
    vm_map_lock_read = (void *)(kbase + 0x25FB90);
    vm_map_lookup_entry = (void *)(kbase + 0x260190);
    vm_map_unlock_read = (void *)(kbase + 0x25FBE0);
    vm_map_delete = (void *)(kbase + 0x262700);
    vm_map_protect = (void *)(kbase + 0x264A50);
    vm_map_findspace = (void *)(kbase + 0x262CC0);
    vm_map_insert = (void *)(kbase + 0x260D60);
    vm_map_lock = (void *)(kbase + 0x25FA50);
    vm_map_unlock = (void *)(kbase + 0x25FAB0);
    proc_rwmem = (void *)(kbase + 0x43E80);
    disable_console_output = (void *)(kbase + 0x1A6EAA0);
    M_TEMP = (void *)(kbase + 0x1A7AE50);
    kernel_map = (void *)(kbase + 0x21C8EE0);
    prison0 = (void *)(kbase + 0x113E398);
    rootvnode = (void *)(kbase + 0x22C5750);
    allproc = (void *)(kbase + 0x1B48318);
    sysents = (void *)(kbase + 0x1125660);
}

void init_1300sdk(uint8_t *kbase) {
    Xfast_syscall = (void *)(kbase + 0x1C0);
    printf = (void *)(kbase + 0x2E0440);
    malloc = (void *)(kbase + 0x9520);
    free = (void *)(kbase + 0x96E0);
    memcpy = (void *)(kbase + 0x2BD4E0);
    memset = (void *)(kbase + 0x1FA1A0);
    memcmp = (void *)(kbase + 0x394300);
    kmem_alloc = (void *)(kbase + 0x465A40);
    strlen = (void *)(kbase + 0x36AB90);
    create_thread = (void *)(kbase + 0x4C6C0);
    kern_reboot = (void *)(kbase + 0x3A1DD0);
    vm_map_lock_read = (void *)(kbase + 0x2F7110);
    vm_map_lookup_entry = (void *)(kbase + 0x2F7750);
    vm_map_unlock_read = (void *)(kbase + 0x2F7160);
    vm_map_delete = (void *)(kbase + 0x2F9C10);
    vm_map_protect = (void *)(kbase + 0x2FBF70);
    vm_map_findspace = (void *)(kbase + 0x2FA1D0);
    vm_map_insert = (void *)(kbase + 0x2F8310);
    vm_map_lock = (void *)(kbase + 0x2F6FC0);
    vm_map_unlock = (void *)(kbase + 0x2F7030);
    proc_rwmem = (void *)(kbase + 0x366000);
    disable_console_output = (void *)(kbase + 0x1A47F40);
    M_TEMP = (void *)(kbase + 0x1520D00);
    kernel_map = (void *)(kbase + 0x22D1D50);
    prison0 = (void *)(kbase + 0x111FA18);
    rootvnode = (void *)(kbase + 0x2136E90);
    allproc = (void *)(kbase + 0x1B28538);
    sysents = (void *)(kbase + 0x1102B70);
}

// 13.02 and 13.04 share an identical kernel layout (verified: every offset matches).
void init_1304sdk(uint8_t *kbase) {
    Xfast_syscall = (void *)(kbase + 0x1C0);
    printf = (void *)(kbase + 0x2E0450);
    malloc = (void *)(kbase + 0x9520);
    free = (void *)(kbase + 0x96E0);
    memcpy = (void *)(kbase + 0x2BD4F0);
    memset = (void *)(kbase + 0x1FA1B0);
    memcmp = (void *)(kbase + 0x394310);
    kmem_alloc = (void *)(kbase + 0x465A50);
    strlen = (void *)(kbase + 0x36ABA0);
    create_thread = (void *)(kbase + 0x4C6C0);
    kern_reboot = (void *)(kbase + 0x3A1DE0);
    vm_map_lock_read = (void *)(kbase + 0x2F7120);
    vm_map_lookup_entry = (void *)(kbase + 0x2F7760);
    vm_map_unlock_read = (void *)(kbase + 0x2F7170);
    vm_map_delete = (void *)(kbase + 0x2F9C20);
    vm_map_protect = (void *)(kbase + 0x2FBF80);
    vm_map_findspace = (void *)(kbase + 0x2FA1E0);
    vm_map_insert = (void *)(kbase + 0x2F8320);
    vm_map_lock = (void *)(kbase + 0x2F6FD0);
    vm_map_unlock = (void *)(kbase + 0x2F7040);
    proc_rwmem = (void *)(kbase + 0x366010);
    disable_console_output = (void *)(kbase + 0x1A47F40);
    M_TEMP = (void *)(kbase + 0x1520D00);
    kernel_map = (void *)(kbase + 0x22D1D50);
    prison0 = (void *)(kbase + 0x111FA18);
    rootvnode = (void *)(kbase + 0x2136E90);
    allproc = (void *)(kbase + 0x1B28538);
    sysents = (void *)(kbase + 0x1102B70);
}

void init_1350sdk(uint8_t *kbase) {
    Xfast_syscall = (void *)(kbase + 0x1C0);
    printf = (void *)(kbase + 0x2E0460);
    malloc = (void *)(kbase + 0x9520);
    free = (void *)(kbase + 0x96E0);
    memcpy = (void *)(kbase + 0x2BD500);
    memset = (void *)(kbase + 0x1FA1C0);
    memcmp = (void *)(kbase + 0x3946D0);
    kmem_alloc = (void *)(kbase + 0x465E90);
    strlen = (void *)(kbase + 0x36AEF0);
    create_thread = (void *)(kbase + 0x4C6C0);
    kern_reboot = (void *)(kbase + 0x3A21A0);
    vm_map_lock_read = (void *)(kbase + 0x2F7470);
    vm_map_lookup_entry = (void *)(kbase + 0x2F7AB0);
    vm_map_unlock_read = (void *)(kbase + 0x2F74C0);
    vm_map_delete = (void *)(kbase + 0x2F9F70);
    vm_map_protect = (void *)(kbase + 0x2FC2D0);
    vm_map_findspace = (void *)(kbase + 0x2FA530);
    vm_map_insert = (void *)(kbase + 0x2F8670);
    vm_map_lock = (void *)(kbase + 0x2F7320);
    vm_map_unlock = (void *)(kbase + 0x2F7390);
    proc_rwmem = (void *)(kbase + 0x366360);
    disable_console_output = (void *)(kbase + 0x1A47F40);
    M_TEMP = (void *)(kbase + 0x1520D00);
    kernel_map = (void *)(kbase + 0x22D1D50);
    prison0 = (void *)(kbase + 0x111FA18);
    rootvnode = (void *)(kbase + 0x2136E90);
    allproc = (void *)(kbase + 0x1B28538);
    sysents = (void *)(kbase + 0x1102B70);
}

void init_1352sdk(uint8_t *kbase) {
    Xfast_syscall = (void *)(kbase + 0x1C0);
    printf = (void *)(kbase + 0x2E0510);
    malloc = (void *)(kbase + 0x9520);
    free = (void *)(kbase + 0x96E0);
    memcpy = (void *)(kbase + 0x2BD5A0);
    memset = (void *)(kbase + 0x1FA260);
    memcmp = (void *)(kbase + 0x394AD0);
    kmem_alloc = (void *)(kbase + 0x466290);
    strlen = (void *)(kbase + 0x36B2F0);
    create_thread = (void *)(kbase + 0x4C6C0);
    kern_reboot = (void *)(kbase + 0x3A25A0);
    vm_map_lock_read = (void *)(kbase + 0x2F7870);
    vm_map_lookup_entry = (void *)(kbase + 0x2F7EB0);
    vm_map_unlock_read = (void *)(kbase + 0x2F78C0);
    vm_map_delete = (void *)(kbase + 0x2FA370);
    vm_map_protect = (void *)(kbase + 0x2FC6D0);
    vm_map_findspace = (void *)(kbase + 0x2FA930);
    vm_map_insert = (void *)(kbase + 0x2F8A70);
    vm_map_lock = (void *)(kbase + 0x2F7720);
    vm_map_unlock = (void *)(kbase + 0x2F7790);
    proc_rwmem = (void *)(kbase + 0x366760);
    disable_console_output = (void *)(kbase + 0x1A47F40);
    M_TEMP = (void *)(kbase + 0x1520D00);
    kernel_map = (void *)(kbase + 0x22D1D50);
    prison0 = (void *)(kbase + 0x111FA18);
    rootvnode = (void *)(kbase + 0x2136E90);
    allproc = (void *)(kbase + 0x1B28538);
    sysents = (void *)(kbase + 0x1102B70);
}

void init_ksdk() {
    uint64_t kbase = get_kbase();
    cachedKernelBase = kbase;
    unsigned short firmwareVersion = kget_firmware_from_base(kbase);
    switch(firmwareVersion) {
        case 505: case 507:
            init_505sdk((uint8_t *)kbase);
            break;
        case 671: case 672:
            init_672sdk((uint8_t *)kbase);
            break;
        case 700: case 701: case 702:
            init_702sdk((uint8_t *)kbase);
            break;
        case 750: case 751: case 755:
            init_755sdk((uint8_t *)kbase);
            break;
        case 800: case 801: case 803:
            init_803sdk((uint8_t *)kbase);
            break;
        case 850: case 852:
            init_852sdk((uint8_t *)kbase);
            break;
        case 900:
            init_900sdk((uint8_t *)kbase);
            break;
        case 903: case 904:
            init_904sdk((uint8_t *)kbase);
            break;
        case 950: case 951: case 960:
            init_960sdk((uint8_t *)kbase);
            break;
        case 1000: case 1001: case 1002:
            init_1002sdk((uint8_t *)kbase);
            break;
        case 1050: case 1051: case 1070: case 1071:
            init_1071sdk((uint8_t *)kbase);
            break;
        case 1100:
            init_1100sdk((uint8_t *)kbase);
            break;
        case 1102:
            init_1102sdk((uint8_t *)kbase);
            break;
        case 1150: case 1152:
            init_1152sdk((uint8_t *)kbase);
            break;
        case 1200: case 1202:
            init_1202sdk((uint8_t *)kbase);
            break;
        case 1250: case 1252:
            init_1252sdk((uint8_t *)kbase);
            break;
        case 1300:
            init_1300sdk((uint8_t *)kbase);
            break;
        case 1302: case 1304:
            init_1304sdk((uint8_t *)kbase);
            break;
        case 1350:
            init_1350sdk((uint8_t *)kbase);
            break;
        case 1352:
            init_1352sdk((uint8_t *)kbase);
            break;
    }
}
