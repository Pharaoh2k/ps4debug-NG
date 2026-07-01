// SPDX-License-Identifier: GPL-3.0-only


#pragma once
#include <stdint.h>

#ifndef SCAN_TURBO_SIZET_FROM_PS4
#include <stddef.h>
#endif

size_t scan_simd_find_exact(unsigned char valtype, const uint8_t *buf, size_t len,
                            const void *value, uint32_t step,
                            uint32_t *out_off, size_t max_out);

int scan_point_turbo_supported(unsigned char cmpType, unsigned char valType);

int scan_point_compare(unsigned char cmpType, unsigned char valType,
                       const void *mem, const void *scan, const void *prev);

struct scan_range { uint64_t start; uint64_t end; };

#define SCAN_EXCLUDE_NONE  0
#define SCAN_EXCLUDE_PCD   1
#define SCAN_EXCLUDE_SPEED 2

int scan_turbo_regions(uint32_t pid, int mode, uint32_t min_mbps,
                      struct scan_range *out, int max, uint64_t *out_total_bytes);
