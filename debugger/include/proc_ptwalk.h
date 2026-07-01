// SPDX-License-Identifier: GPL-3.0-only


#pragma once
#include <stdint.h>

int ptw_discover(void);

uint64_t proc_ptwalk_dmap_base(void);

int proc_ptwalk_leaf_addr(uint32_t pid, uint64_t va, uint64_t *out_pte_kaddr,
                          uint64_t *out_pte_val, int *out_level);

int proc_ptwalk_span_resolve(uint32_t pid, uint64_t span2m, int *out_huge,
                             uint64_t *out_phys_base, uint64_t *out_leaf_pt_kaddr,
                             uint64_t *out_pte);

int proc_ptwalk_probe(uint32_t pid, uint64_t va, uint64_t *out_phys,
                      int *out_level, uint64_t *out_pagesize, uint64_t *out_pte);
