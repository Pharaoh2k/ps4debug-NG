// SPDX-License-Identifier: GPL-3.0-only

#ifndef _PROC_H
#define _PROC_H

#include <ps4.h>
#include <stdbool.h>
#include "protocol.h"
#include "net.h"

struct proc_vm_map_entry {
    char name[32];
    uint64_t start;
    uint64_t end;
    uint64_t offset;
    uint16_t prot;
} __attribute__((packed));

int proc_list_handle(int fd, struct cmd_packet *packet);
int proc_read_handle(int fd, struct cmd_packet *packet);
int proc_read_stack_handle(int fd, struct cmd_packet *packet);
int proc_disasm_region_handle(int fd, struct cmd_packet *packet);
int proc_extract_code_xrefs_handle(int fd, struct cmd_packet *packet);
int proc_find_xrefs_to_handle(int fd, struct cmd_packet *packet);
int proc_write_handle(int fd, struct cmd_packet *packet);
int proc_write_multi_handle(int fd, struct cmd_packet *packet);
int proc_maps_handle(int fd, struct cmd_packet *packet);
int proc_install_handle(int fd, struct cmd_packet *packet);
int proc_call_handle(int fd, struct cmd_packet *packet);
int proc_elf_handle(int fd, struct cmd_packet *packet);
int proc_elf_rpc_handle(int fd, struct cmd_packet *packet);
int proc_protect_handle(int fd, struct cmd_packet *packet);
int proc_scan_handle(int fd, struct cmd_packet *packet);
int proc_info_handle(int fd, struct cmd_packet *packet);
int proc_alloc_handle(int fd, struct cmd_packet *packet);
int proc_free_handle(int fd, struct cmd_packet *packet);

int proc_alloc_hinted_handle(int fd, struct cmd_packet *packet);
int proc_scan_aob_handle(int fd, struct cmd_packet *packet);
int proc_scan_aob_multi_handle(int fd, struct cmd_packet *packet);
int proc_auth_handle(int fd, struct cmd_packet *packet);
int proc_scan_start_handle(int fd, struct cmd_packet *packet);
int proc_scan_count_handle(int fd, struct cmd_packet *packet);
int proc_scan_get_handle(int fd, struct cmd_packet *packet);

int proc_assemble_handle(int fd, struct cmd_packet *packet);

int proc_turboscan_caps_handle(int fd, struct cmd_packet *packet);
int proc_turboscan_start_handle(int fd, struct cmd_packet *packet, unsigned char client_idx);
int proc_turboscan_count_handle(int fd, struct cmd_packet *packet, unsigned char client_idx);
int proc_turboscan_get_handle(int fd, struct cmd_packet *packet, unsigned char client_idx);
int proc_turboscan_end_handle(int fd, struct cmd_packet *packet, unsigned char client_idx);
int proc_turboscan_config_handle(int fd, struct cmd_packet *packet);
int proc_turboscan_regions_handle(int fd, struct cmd_packet *packet);

int proc_turboscan_fileprobe_handle(int fd, struct cmd_packet *packet);

void turboscan_session_free_idx(unsigned char idx);
void turboscan_alias_free_idx(unsigned char idx);

void turboscan_startup_cleanup(void);

int proc_handle(int fd, struct cmd_packet *packet, unsigned char client_idx);

extern uint32_t g_proc_auth_bits;

#endif
