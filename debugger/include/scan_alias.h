// SPDX-License-Identifier: GPL-3.0-only


#pragma once
#include <stdint.h>

typedef struct scan_alias_ctx scan_alias_ctx;

scan_alias_ctx *scan_alias_begin(uint32_t pid, uint64_t arena_cap);

const void *scan_alias_map(scan_alias_ctx *c, uint64_t tgt, uint64_t len,
                           uint64_t *out_mapped_len);

void scan_alias_release(scan_alias_ctx *c);

void scan_alias_end(scan_alias_ctx *c);

void scan_alias_rebind(scan_alias_ctx *c, uint32_t pid);
