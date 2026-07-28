// SPDX-License-Identifier: GPL-3.0-only

#include "proc.h"

#include "Zydis.h"
#include "aob_scan.h"
#include "proc_ptwalk.h"
#include "scan_alias.h"

#define DISASM_READ_CHUNK  0x10000

static void disasm_fill_entry(struct disasm_instr_entry *out,
                              uint64_t addr,
                              const ZydisDecodedInstruction *insn,
                              const ZydisDecodedOperand *operands) {
    memset(out, 0, sizeof(*out));
    out->addr = addr;
    out->length = insn->length;
    out->mnemonic_lo = (uint8_t)(insn->mnemonic & 0xFF);

    switch (insn->meta.category) {
        case ZYDIS_CATEGORY_CALL:          out->kind |= 0x01; break;
        case ZYDIS_CATEGORY_RET:           out->kind |= 0x02; break;
        case ZYDIS_CATEGORY_UNCOND_BR:     out->kind |= 0x04; break;
        case ZYDIS_CATEGORY_COND_BR:       out->kind |= 0x08; break;
        default: break;
    }

    for (ZyanU8 i = 0; i < insn->operand_count_visible; i++) {
        const ZydisDecodedOperand *op = &operands[i];
        if (op->type != ZYDIS_OPERAND_TYPE_MEMORY) continue;

        out->kind |= 0x10;
        if (op->actions & ZYDIS_OPERAND_ACTION_MASK_READ)  out->kind |= 0x40;
        if (op->actions & ZYDIS_OPERAND_ACTION_MASK_WRITE) out->kind |= 0x80;

        out->mem_base_reg  = (uint8_t)(op->mem.base  & 0xFF);
        out->mem_index_reg = (uint8_t)(op->mem.index & 0xFF);
        out->mem_scale     = op->mem.scale;
        out->mem_disp      = op->mem.disp.has_displacement ? op->mem.disp.value : 0;

        if (op->mem.base == ZYDIS_REGISTER_RIP) {
            out->kind |= 0x20;
            out->rip_rel_target = addr + insn->length + (uint64_t)op->mem.disp.value;
        }
        break;
    }
}

typedef void (*disasm_emit_fn)(uint64_t addr,
                               const ZydisDecodedInstruction *insn,
                               const ZydisDecodedOperand *operands,
                               void *ctx);

static uint64_t disasm_iterate(uint32_t pid, uint64_t start, uint64_t length,
                               uint64_t max_instrs, disasm_emit_fn emit, void *ctx) {
    void *chunk = net_alloc_buffer(DISASM_READ_CHUNK);
    if (!chunk) return 0;

    ZydisDecoder decoder;
    ZydisDecoderInit(&decoder, ZYDIS_MACHINE_MODE_LONG_64, ZYDIS_STACK_WIDTH_64);

    ZydisDecodedInstruction insn;
    ZydisDecodedOperand     operands[ZYDIS_MAX_OPERAND_COUNT];

    uint64_t emitted  = 0;
    uint64_t off      = 0;

    while (off < length && emitted < max_instrs) {
        uint64_t remaining = length - off;
        uint32_t read_len  = remaining > DISASM_READ_CHUNK ? DISASM_READ_CHUNK : (uint32_t)remaining;

        sys_proc_rw(pid, start + off, chunk, read_len, 0);

        uint32_t pos = 0;
        while (pos < read_len && emitted < max_instrs) {
            uint32_t avail = read_len - pos;
            ZyanStatus r = ZydisDecoderDecodeFull(&decoder,
                                                   (uint8_t *)chunk + pos, avail,
                                                   &insn, operands);
            if (!ZYAN_SUCCESS(r)) {

                pos++;
                continue;
            }
            emit(start + off + pos, &insn, operands, ctx);
            emitted++;
            pos += insn.length;

            if (pos + ZYDIS_MAX_INSTRUCTION_LENGTH > read_len && remaining > read_len) {
                break;
            }
        }
        off += (pos > 0 ? pos : 1);
    }

    free(chunk);
    return emitted;
}

struct disasm_ctx {
    int fd;
    uint8_t *out_buf;
    uint32_t out_cap;
    uint32_t out_used;
};

static void disasm_emit_record(uint64_t addr,
                               const ZydisDecodedInstruction *insn,
                               const ZydisDecodedOperand *operands,
                               void *ctx_) {
    struct disasm_ctx *ctx = (struct disasm_ctx *)ctx_;
    if (ctx->out_used + DISASM_INSTR_ENTRY_SIZE > ctx->out_cap) {

        net_send_all(ctx->fd, ctx->out_buf, (int)ctx->out_used);
        ctx->out_used = 0;
    }
    struct disasm_instr_entry *e = (struct disasm_instr_entry *)(ctx->out_buf + ctx->out_used);
    disasm_fill_entry(e, addr, insn, operands);
    ctx->out_used += DISASM_INSTR_ENTRY_SIZE;
}

int proc_disasm_region_handle(int fd, struct cmd_packet *packet) {
    struct cmd_proc_disasm_packet *dp = (struct cmd_proc_disasm_packet *)packet->data;
    if (!dp) {
        net_send_int32(fd, CMD_DATA_NULL);
        return 1;
    }
    if (dp->max_entries == 0 || dp->max_entries > 1000000) {
        net_send_int32(fd, CMD_ERROR);
        return 1;
    }

    struct disasm_ctx ctx;
    ctx.fd = fd;
    ctx.out_cap = 0x10000;
    ctx.out_used = 0;
    ctx.out_buf = (uint8_t *)net_alloc_buffer(ctx.out_cap);
    if (!ctx.out_buf) {
        net_send_int32(fd, CMD_DATA_NULL);
        return 1;
    }

    net_send_int32(fd, CMD_SUCCESS);

    (void)disasm_iterate(dp->pid, dp->address, dp->length,
                          dp->max_entries, disasm_emit_record, &ctx);

    if (ctx.out_used > 0) {
        net_send_all(fd, ctx.out_buf, (int)ctx.out_used);
    }

    uint8_t sentinel[DISASM_INSTR_ENTRY_SIZE];
    memset(sentinel, 0xFF, sizeof(sentinel));
    net_send_all(fd, sentinel, sizeof(sentinel));

    free(ctx.out_buf);
    return 0;
}

struct xrefs_ctx {
    int fd;
    uint64_t *buf;
    uint32_t cap;
    uint32_t used;
};

static void xrefs_emit(uint64_t addr,
                       const ZydisDecodedInstruction *insn,
                       const ZydisDecodedOperand *operands,
                       void *ctx_) {
    (void)addr;
    struct xrefs_ctx *ctx = (struct xrefs_ctx *)ctx_;
    for (ZyanU8 i = 0; i < insn->operand_count_visible; i++) {
        const ZydisDecodedOperand *op = &operands[i];
        if (op->type != ZYDIS_OPERAND_TYPE_MEMORY) continue;
        if (op->mem.base != ZYDIS_REGISTER_RIP) break;

        uint64_t target = addr + insn->length + (uint64_t)op->mem.disp.value;
        if (ctx->used + 1 > ctx->cap) {
            net_send_all(ctx->fd, ctx->buf, (int)(ctx->used * sizeof(uint64_t)));
            ctx->used = 0;
        }
        ctx->buf[ctx->used++] = target;
        break;
    }
}

int proc_extract_code_xrefs_handle(int fd, struct cmd_packet *packet) {
    struct cmd_proc_disasm_packet *dp = (struct cmd_proc_disasm_packet *)packet->data;
    if (!dp) {
        net_send_int32(fd, CMD_DATA_NULL);
        return 1;
    }

    struct xrefs_ctx ctx;
    ctx.fd = fd;
    ctx.cap = 0x2000;
    ctx.used = 0;
    ctx.buf = (uint64_t *)net_alloc_buffer(ctx.cap * sizeof(uint64_t));
    if (!ctx.buf) {
        net_send_int32(fd, CMD_DATA_NULL);
        return 1;
    }

    net_send_int32(fd, CMD_SUCCESS);

    uint64_t max_instrs = dp->max_entries ? dp->max_entries : 10000000;
    uint64_t emitted = disasm_iterate(dp->pid, dp->address, dp->length,
                                       max_instrs, xrefs_emit, &ctx);

    if (ctx.used > 0) {
        net_send_all(fd, ctx.buf, (int)(ctx.used * sizeof(uint64_t)));
    }

    uint32_t total_instrs = (uint32_t)emitted;
    uint32_t total_xrefs  = 0;
    (void)total_instrs; (void)total_xrefs;
    uint64_t sentinel = 0;

    sentinel = 0xFFFFFFFFFFFFFFFFULL;
    net_send_all(fd, &sentinel, sizeof(uint64_t));

    free(ctx.buf);
    return 0;
}

struct xrefs_to_ctx {
    int fd;
    uint64_t target;
    uint64_t *buf;
    uint32_t cap;
    uint32_t used;
};

static void xrefs_to_emit(uint64_t addr,
                          const ZydisDecodedInstruction *insn,
                          const ZydisDecodedOperand *operands,
                          void *ctx_) {
    struct xrefs_to_ctx *ctx = (struct xrefs_to_ctx *)ctx_;
    for (ZyanU8 i = 0; i < insn->operand_count_visible; i++) {
        const ZydisDecodedOperand *op = &operands[i];
        if (op->type != ZYDIS_OPERAND_TYPE_MEMORY) continue;
        if (op->mem.base != ZYDIS_REGISTER_RIP) break;
        uint64_t resolved = addr + insn->length + (uint64_t)op->mem.disp.value;
        if (resolved == ctx->target) {
            if (ctx->used + 1 > ctx->cap) {
                net_send_all(ctx->fd, ctx->buf, (int)(ctx->used * sizeof(uint64_t)));
                ctx->used = 0;
            }
            ctx->buf[ctx->used++] = addr;
        }
        break;
    }
}

int proc_find_xrefs_to_handle(int fd, struct cmd_packet *packet) {
    struct cmd_proc_xrefs_to_packet *xp = (struct cmd_proc_xrefs_to_packet *)packet->data;
    if (!xp) {
        net_send_int32(fd, CMD_DATA_NULL);
        return 1;
    }

    struct xrefs_to_ctx ctx;
    ctx.fd = fd;
    ctx.target = xp->target_address;
    ctx.cap = 0x2000;
    ctx.used = 0;
    ctx.buf = (uint64_t *)net_alloc_buffer(ctx.cap * sizeof(uint64_t));
    if (!ctx.buf) {
        net_send_int32(fd, CMD_DATA_NULL);
        return 1;
    }

    net_send_int32(fd, CMD_SUCCESS);

    (void)disasm_iterate(xp->pid, xp->scan_address, xp->scan_length,
                         10000000, xrefs_to_emit, &ctx);

    if (ctx.used > 0) {
        net_send_all(fd, ctx.buf, (int)(ctx.used * sizeof(uint64_t)));
    }
    uint64_t sentinel = 0xFFFFFFFFFFFFFFFFULL;
    net_send_all(fd, &sentinel, sizeof(uint64_t));

    free(ctx.buf);
    return 0;
}

int proc_list_handle(int fd, struct cmd_packet *packet) {
    void *data;
    uint64_t num;
    uint32_t length;

    sys_proc_list(NULL, &num);

    if(num > 0) {
        length = sizeof(struct proc_list_entry) * num;
        data = net_alloc_buffer(length);
        if(!data) {
            net_send_int32(fd, CMD_DATA_NULL);
            return 1;
        }

        sys_proc_list(data, &num);

        net_send_int32(fd, CMD_SUCCESS);
        net_send_all(fd, &num, sizeof(uint32_t));
        net_send_all(fd, data, length);

        free(data);

        return 0;
    }

    net_send_int32(fd, CMD_DATA_NULL);
    return 1;
}

int proc_read_handle(int fd, struct cmd_packet *packet) {
    struct cmd_proc_read_packet *rp = (struct cmd_proc_read_packet *)packet->data;

    if (!rp) {
        net_send_int32(fd, CMD_DATA_NULL);
        return 1;
    }

    void *data = net_alloc_buffer(0x10000);
    if (!data) {
        net_send_int32(fd, CMD_DATA_NULL);
        return 1;
    }

    net_send_int32(fd, CMD_SUCCESS);

    uint64_t length = rp->length;
    uint64_t address = rp->address;

    if (length > 0) {
        uint64_t last_aligned = address + ((length - 1) & ~(uint64_t)0xFFFF);

        for (;;) {
            memset(data, 0, 0x10000);
            if (address == last_aligned) break;
            sys_proc_rw(rp->pid, address, data, 0x10000, 0);
            net_send_all(fd, data, 0x10000);
            address += 0x10000;
        }

        uint64_t tail_size = length - ((length - 1) & ~(uint64_t)0xFFFF);
        sys_proc_rw(rp->pid, address, data, tail_size, 0);
        net_send_all(fd, data, (int)tail_size);
    }

    free(data);
    return 0;
}

#define PROC_USR_MIN  0x10000ULL
#define PROC_USR_MAX  0x0000800000000000ULL
static inline int proc_addr_range_ok(uint64_t a, uint64_t len) {
    if (len == 0) return 0;
    if (a < PROC_USR_MIN || a >= PROC_USR_MAX) return 0;
    if (a + len < a) return 0;
    if (a + len > PROC_USR_MAX) return 0;
    return 1;
}

int proc_read_stack_handle(int fd, struct cmd_packet *packet) {
    struct cmd_proc_read_stack_packet *sp =
        (struct cmd_proc_read_stack_packet *)packet->data;
    if (!sp) { net_send_int32(fd, CMD_DATA_NULL); return 1; }

    uint32_t pid   = sp->pid;
    uint64_t rbp   = sp->rbp;
    uint64_t rsp   = sp->rsp;
    uint32_t depth = sp->depth;
    if (depth == 0) depth = 1;
    if (depth > CMD_PROC_READ_STACK_MAX_DEPTH) depth = CMD_PROC_READ_STACK_MAX_DEPTH;

    uint64_t cap = 4 + (uint64_t)depth *
                   (32 + 12 + CMD_PROC_READ_STACK_LOCALS_CAP + CMD_PROC_READ_STACK_CODE_LEN);
    uint8_t *buf = (uint8_t *)net_alloc_buffer(cap);
    if (!buf) { net_send_int32(fd, CMD_DATA_NULL); return 1; }

    uint8_t  code_scratch[CMD_PROC_READ_STACK_CODE_LEN];
    uint32_t off = 4;
    uint32_t n_frames = 0;

    uint64_t cur_rbp = rbp, cur_rsp = rsp;
    for (uint32_t i = 0; i < depth; i++) {

        if (!proc_addr_range_ok(cur_rbp, 16)) break;

        uint64_t pair[2] = { 0, 0 };
        sys_proc_rw((uint64_t)pid, cur_rbp, pair, 16, 0);
        uint64_t saved_rbp = pair[0];
        uint64_t ret_addr  = pair[1];

        int64_t  fs = (int64_t)cur_rbp - (int64_t)cur_rsp + 8;
        uint32_t flags = 0;
        uint32_t locals_len = 0;
        if (fs <= 0 || (uint64_t)fs > CMD_PROC_READ_STACK_LOCALS_CAP
            || !proc_addr_range_ok(cur_rsp, (uint64_t)fs)) {
            flags |= 1u;
        } else {
            locals_len = (uint32_t)fs;
        }
        uint64_t code_addr = ret_addr - CMD_PROC_READ_STACK_CODE_OFF;
        uint32_t code_len  = proc_addr_range_ok(code_addr, CMD_PROC_READ_STACK_CODE_LEN)
                             ? CMD_PROC_READ_STACK_CODE_LEN : 0u;

        if (off + 32 + 12 + locals_len + code_len > cap) break;

        memcpy(buf + off, &cur_rbp,   8); off += 8;
        memcpy(buf + off, &cur_rsp,   8); off += 8;
        memcpy(buf + off, &saved_rbp, 8); off += 8;
        memcpy(buf + off, &ret_addr,  8); off += 8;
        memcpy(buf + off, &flags,      4); off += 4;
        memcpy(buf + off, &locals_len, 4); off += 4;
        memcpy(buf + off, &code_len,   4); off += 4;
        if (locals_len) {
            memset(buf + off, 0, locals_len);
            sys_proc_rw((uint64_t)pid, cur_rsp, buf + off, locals_len, 0);
            off += locals_len;
        }
        if (code_len) {
            memset(code_scratch, 0, sizeof(code_scratch));
            sys_proc_rw((uint64_t)pid, code_addr, code_scratch, code_len, 0);
            memcpy(buf + off, code_scratch, code_len);
            off += code_len;
        }
        n_frames++;

        if (saved_rbp == 0) break;
        cur_rsp = cur_rbp + 8;
        cur_rbp = saved_rbp;
    }

    memcpy(buf, &n_frames, 4);
    net_send_int32(fd, CMD_SUCCESS);
    net_send_all(fd, &off, 4);
    net_send_all(fd, buf, (int)off);
    free(buf);
    return 0;
}

static uint8_t proc_write_mem_status(uint32_t pid, uint64_t address,
                                     uint32_t length, const void *expected,
                                     void *readback) {
    if ((int32_t)pid <= 0 || !expected || !readback || length == 0
        || address + length < address) {
        return PROC_WRITE_STATUS_INVALID;
    }

    if (sys_proc_rw(pid, address, (void *)expected, length, 1) != 0) {
        return PROC_WRITE_STATUS_RW_FAILED;
    }

    const uint8_t *wanted = (const uint8_t *)expected;
    uint8_t *actual = (uint8_t *)readback;
    for (uint32_t i = 0; i < length; i++) {
        actual[i] = wanted[i] ^ 0xFFu;
    }

    if (sys_proc_rw(pid, address, actual, length, 0) != 0) {
        return PROC_WRITE_STATUS_RW_FAILED;
    }

    return memcmp(actual, wanted, length) == 0
        ? PROC_WRITE_STATUS_OK
        : PROC_WRITE_STATUS_VERIFY_FAILED;
}

int proc_write_handle(int fd, struct cmd_packet *packet) {
    struct cmd_proc_write_packet *wp = (struct cmd_proc_write_packet *)packet->data;

    if (!wp) {
        net_send_int32(fd, CMD_DATA_NULL);
        return 1;
    }

    void *data = net_alloc_buffer(0x10000);
    if (!data) {
        net_send_int32(fd, CMD_DATA_NULL);
        return 1;
    }

    void *readback = net_alloc_buffer(0x10000);
    if (!readback) {
        free(data);
        net_send_int32(fd, CMD_DATA_NULL);
        return 1;
    }

    net_send_int32(fd, CMD_SUCCESS);

    uint64_t length = wp->length;
    uint64_t address = wp->address;
    uint8_t status = PROC_WRITE_STATUS_OK;
    int request_valid = length == 0
        || ((int32_t)wp->pid > 0 && address + length >= address);
    if (!request_valid) {
        status = PROC_WRITE_STATUS_INVALID;
    }

    if (length > 0) {
        while (length > 0x10000) {
            if (net_recv_all(fd, data, 0x10000, 1) != 0x10000) {
                free(readback);
                free(data);
                return 1;
            }
            if (request_valid) {
                uint8_t chunk_status = proc_write_mem_status(
                    wp->pid, address, 0x10000, data, readback);
                if (status == PROC_WRITE_STATUS_OK) {
                    status = chunk_status;
                }
            }
            address += 0x10000;
            length -= 0x10000;
        }
        if (net_recv_all(fd, data, (int)length, 1) != (int)length) {
            free(readback);
            free(data);
            return 1;
        }
        if (request_valid) {
            uint8_t chunk_status = proc_write_mem_status(
                wp->pid, address, (uint32_t)length, data, readback);
            if (status == PROC_WRITE_STATUS_OK) {
                status = chunk_status;
            }
        }
    }

    net_send_int32(fd, status == PROC_WRITE_STATUS_OK
                       ? CMD_SUCCESS : CMD_ERROR);
    free(readback);
    free(data);
    return 0;
}

int proc_write_multi_handle(int fd, struct cmd_packet *packet) {
    struct cmd_proc_write_multi_packet *mp =
        (struct cmd_proc_write_multi_packet *)packet->data;
    if (!mp) {
        net_send_int32(fd, CMD_DATA_NULL);
        return 1;
    }

    uint32_t pid         = mp->pid;
    uint32_t count       = mp->count;
    int      want_status = (mp->flags & PROC_WRITE_MULTI_F_STATUS) ? 1 : 0;

    if (count > PROC_WRITE_MULTI_MAX_COUNT) {
        net_send_int32(fd, CMD_ERROR);
        return 1;
    }

    unsigned char *buf = (unsigned char *)net_alloc_buffer(0x10000);
    if (!buf) {
        net_send_int32(fd, CMD_DATA_NULL);
        return 1;
    }

    unsigned char *readback =
        (unsigned char *)net_alloc_buffer(0x10000);
    if (!readback) {
        free(buf);
        net_send_int32(fd, CMD_DATA_NULL);
        return 1;
    }

    unsigned char *status = NULL;
    if (want_status && count > 0) {
        status = (unsigned char *)net_alloc_buffer(count);
        if (!status) {
            free(readback);
            free(buf);
            net_send_int32(fd, CMD_DATA_NULL);
            return 1;
        }
    }

    net_send_int32(fd, CMD_SUCCESS);

    int any_failed = 0;
    for (uint32_t i = 0; i < count; i++) {
        unsigned char hdr[12];
        if (net_recv_all(fd, hdr, 12, 1) != 12) {
            if (status) free(status);
            free(readback);
            free(buf);
            return 1;
        }
        uint64_t addr;
        uint32_t len32;
        memcpy(&addr,  hdr,     8);
        memcpy(&len32, hdr + 8, 4);

        if (len32 > PROC_WRITE_MULTI_MAX_ENTRY) {
            if (status) free(status);
            free(readback);
            free(buf);
            net_send_int32(fd, CMD_ERROR);
            return 1;
        }

        uint64_t length = len32;
        uint64_t a      = addr;
        int entry_valid = length == 0
            || ((int32_t)pid > 0 && a + length >= a);
        uint8_t failed = entry_valid
            ? PROC_WRITE_STATUS_OK : PROC_WRITE_STATUS_INVALID;
        while (length > 0) {
            uint32_t toRecv = length > 0x10000u ? 0x10000u : (uint32_t)length;
            if (net_recv_all(fd, buf, (int)toRecv, 1) != (int)toRecv) {
                if (status) free(status);
                free(readback);
                free(buf);
                return 1;
            }
            if (entry_valid) {
                uint8_t chunk_status =
                    proc_write_mem_status(pid, a, toRecv, buf, readback);
                if (failed == PROC_WRITE_STATUS_OK) {
                    failed = chunk_status;
                }
            }
            a      += toRecv;
            length -= toRecv;
        }
        if (failed != PROC_WRITE_STATUS_OK) {
            any_failed = 1;
        }
        if (status) status[i] = failed;
    }

    if (status) {
        net_send_all(fd, status, (int)count);
        free(status);
    }
    net_send_int32(fd, !want_status && any_failed
                       ? CMD_ERROR : CMD_SUCCESS);
    free(readback);
    free(buf);
    return 0;
}

int proc_maps_handle(int fd, struct cmd_packet *packet) {
    struct cmd_proc_maps_packet *mp;
    struct sys_proc_vm_map_args args;
    uint32_t size;
    uint32_t num;

    mp = (struct cmd_proc_maps_packet *)packet->data;

    if(mp) {
        memset(&args, NULL, sizeof(args));

        if(sys_proc_cmd(mp->pid, SYS_PROC_VM_MAP, &args)) {
            net_send_int32(fd, CMD_ERROR);
            return 1;
        }

        size = args.num * sizeof(struct proc_vm_map_entry);

        args.maps = (struct proc_vm_map_entry *)net_alloc_buffer(size);
        if(!args.maps) {
            net_send_int32(fd, CMD_DATA_NULL);
            return 1;
        }

        if(sys_proc_cmd(mp->pid, SYS_PROC_VM_MAP, &args)) {
            free(args.maps);
            net_send_int32(fd, CMD_ERROR);
            return 1;
        }

        net_send_int32(fd, CMD_SUCCESS);
        num = (uint32_t)args.num;
        net_send_all(fd, &num, sizeof(uint32_t));
        net_send_all(fd, args.maps, size);

        free(args.maps);

        return 0;
    }

    net_send_int32(fd, CMD_ERROR);

    return 1;
}

int proc_install_handle(int fd, struct cmd_packet *packet) {
    struct cmd_proc_install_packet *ip;
    struct sys_proc_install_args args;
    struct cmd_proc_install_response resp;

    ip = (struct cmd_proc_install_packet *)packet->data;

    if(!ip) {
        net_send_int32(fd, CMD_DATA_NULL);
        return 1;
    }

    args.stubentryaddr = NULL;
    sys_proc_cmd(ip->pid, SYS_PROC_INSTALL, &args);

    if(!args.stubentryaddr) {
        net_send_int32(fd, CMD_DATA_NULL);
        return 1;
    }

    resp.rpcstub = args.stubentryaddr;

    net_send_int32(fd, CMD_SUCCESS);
    net_send_all(fd, &resp, CMD_PROC_INSTALL_RESPONSE_SIZE);

    return 0;
}

int proc_call_handle(int fd, struct cmd_packet *packet) {
    struct cmd_proc_call_packet *cp;
    struct sys_proc_call_args args;
    struct cmd_proc_call_response resp;

    cp = (struct cmd_proc_call_packet *)packet->data;

    if(!cp) {
        net_send_int32(fd, CMD_DATA_NULL);
        return 1;
    }

    args.pid = cp->pid;
    args.rpcstub = cp->rpcstub;
    args.rax = NULL;
    args.rip = cp->rpc_rip;
    args.rdi = cp->rpc_rdi;
    args.rsi = cp->rpc_rsi;
    args.rdx = cp->rpc_rdx;
    args.rcx = cp->rpc_rcx;
    args.r8 = cp->rpc_r8;
    args.r9 = cp->rpc_r9;

    sys_proc_cmd(cp->pid, SYS_PROC_CALL, &args);

    resp.pid = cp->pid;
    resp.rpc_rax = args.rax;

    net_send_int32(fd, CMD_SUCCESS);
    net_send_all(fd, &resp, CMD_PROC_CALL_RESPONSE_SIZE);

    return 0;
}

int proc_elf_handle(int fd, struct cmd_packet *packet) {
    struct cmd_proc_elf_packet *ep;
    struct sys_proc_elf_args args;
    void *elf;

    ep = (struct cmd_proc_elf_packet *)packet->data;

    if(ep) {
        elf = net_alloc_buffer(ep->length);
        if(!elf) {
            net_send_int32(fd, CMD_DATA_NULL);
            return 1;
        }

        net_send_int32(fd, CMD_SUCCESS);

        net_recv_all(fd, elf, ep->length, 1);

        args.elf = elf;

        if(sys_proc_cmd(ep->pid, SYS_PROC_ELF, &args)) {
            free(elf);
            net_send_int32(fd, CMD_ERROR);
            return 1;
        }

        free(elf);

        net_send_int32(fd, CMD_SUCCESS);

        return 0;
    }

    net_send_int32(fd, CMD_ERROR);

    return 1;
}

int proc_elf_rpc_handle(int fd, struct cmd_packet *packet) {
    struct cmd_proc_elf_rpc_packet *ep;
    struct sys_proc_elf_rpc_args args;
    struct cmd_proc_elf_rpc_response resp;
    void *elf;

    ep = (struct cmd_proc_elf_rpc_packet *)packet->data;
    if (!ep) {
        net_send_int32(fd, CMD_ERROR);
        return 1;
    }

    elf = net_alloc_buffer(ep->length);
    if (!elf) {
        net_send_int32(fd, CMD_DATA_NULL);
        return 1;
    }

    net_send_int32(fd, CMD_SUCCESS);
    net_recv_all(fd, elf, ep->length, 1);

    args.elf = elf;
    args.entry = 0;
    if (sys_proc_cmd(ep->pid, SYS_PROC_ELF_RPC, &args)) {
        free(elf);
        net_send_int32(fd, CMD_ERROR);
        return 1;
    }

    free(elf);

    resp.entry = args.entry;
    net_send_int32(fd, CMD_SUCCESS);
    net_send_all(fd, &resp, CMD_PROC_ELF_RPC_RESPONSE_SIZE);
    return 0;
}

int proc_protect_handle(int fd, struct cmd_packet *packet) {
    struct cmd_proc_protect_packet *pp;
    struct sys_proc_protect_args args;

    pp = (struct cmd_proc_protect_packet *)packet->data;

    if(pp) {
        args.address = pp->address;
        args.length = pp->length;
        args.prot = pp->newprot;
        sys_proc_cmd(pp->pid, SYS_PROC_PROTECT, &args);

        net_send_int32(fd, CMD_SUCCESS);
    }

    net_send_int32(fd, CMD_DATA_NULL);

    return 0;
}

size_t proc_scan_getSizeOfValueType(cmd_proc_scan_valuetype valType) {
    switch (valType) {
       case valTypeUInt8:
       case valTypeInt8:
          return 1;
       case valTypeUInt16:
       case valTypeInt16:
          return 2;
       case valTypeUInt32:
       case valTypeInt32:
       case valTypeFloat:
          return 4;
       case valTypeUInt64:
       case valTypeInt64:
       case valTypeDouble:
          return 8;
       case valTypeArrBytes:
       case valTypeString:
       default:
          return NULL;
    }
}

static bool fuzzy_float_compare(float scanV, float memV) {
    if (scanV == memV) return true;
    float diff = scanV - memV;
    float abs_diff = diff < 0.0f ? -diff : diff;

    uint32_t eps_bits = 0x00000008u;
    float epsilon;
    memcpy(&epsilon, &eps_bits, 4);

    if (scanV == 0.0f || memV == 0.0f) {

        return epsilon > abs_diff;
    }
    float abs_scan = scanV < 0.0f ? -scanV : scanV;
    float abs_mem  = memV  < 0.0f ? -memV  : memV;
    float sum = abs_scan + abs_mem;

    uint32_t min_sum_bits = 0x007FFFFDu;
    float min_sum;
    memcpy(&min_sum, &min_sum_bits, 4);

    if (min_sum > sum) {
        return epsilon > abs_diff;
    }

    float ratio = abs_diff / sum;
    uint32_t tol_bits = 0x358637BDu;
    float tolerance;
    memcpy(&tolerance, &tol_bits, 4);

    return tolerance > ratio;
}

static bool fuzzy_double_compare(double scanV, double memV) {
    if (scanV == memV) return true;
    double diff = scanV - memV;
    double abs_diff = diff < 0.0 ? -diff : diff;

    uint64_t eps_bits = 0x000000010C6F7A0Bull;
    double epsilon;
    memcpy(&epsilon, &eps_bits, 8);

    if (scanV == 0.0 || memV == 0.0) {
        return epsilon > abs_diff;
    }
    double abs_scan = scanV < 0.0 ? -scanV : scanV;
    double abs_mem  = memV  < 0.0 ? -memV  : memV;
    double sum = abs_scan + abs_mem;

    uint64_t min_sum_bits = 0x0010000000000000ull;
    double min_sum;
    memcpy(&min_sum, &min_sum_bits, 8);
    if (min_sum > sum) {
        return epsilon > abs_diff;
    }

    double ratio = abs_diff / sum;
    uint64_t tol_bits = 0x3EB0C6F7A0B5ED8Dull;
    double tolerance;
    memcpy(&tolerance, &tol_bits, 8);
    return tolerance > ratio;
}

bool proc_scan_legacy_compareValues(cmd_proc_scan_comparetype cmpType,
                                    cmd_proc_scan_valuetype valType,
                                    size_t valTypeLength,
                                    unsigned char *pScanValue,
                                    unsigned char *pMemoryValue,
                                    unsigned char *pExtraValue) {
    switch (cmpType) {
       case cmpTypeExactValue:
       {

          if (valTypeLength <= 1) return false;
          for (size_t j = 0; j < valTypeLength - 1; j++) {
             if (pScanValue[j] != pMemoryValue[j]) return false;
          }
          return true;
       }
       case cmpTypeFuzzyValue:
       {

          if (valType == valTypeFloat) {
             float diff = *(float *)pScanValue - *(float *)pMemoryValue;
             return diff < 1.0f && diff > -1.0f;
          }
          if (valType == valTypeDouble) {
             double diff = *(double *)pScanValue - *(double *)pMemoryValue;
             return diff < 1.0 && diff > -1.0;
          }
          return false;
       }
       case cmpTypeBiggerThan:
       {

          switch (valType) {
             case valTypeUInt8:  return *pMemoryValue > *pScanValue;
             case valTypeInt8:   return *(int8_t *)pMemoryValue > *(int8_t *)pScanValue;
             case valTypeUInt16: return *(uint16_t *)pMemoryValue > *(uint16_t *)pScanValue;
             case valTypeInt16:  return *(int16_t *)pMemoryValue > *(int16_t *)pScanValue;
             case valTypeUInt32: return *(uint32_t *)pMemoryValue > *(uint32_t *)pScanValue;
             case valTypeInt32:  return *(int32_t *)pMemoryValue > *(int32_t *)pScanValue;
             case valTypeUInt64: return *(uint64_t *)pMemoryValue > *(uint64_t *)pScanValue;
             case valTypeInt64:  return *(int64_t *)pMemoryValue > *(int64_t *)pScanValue;
             case valTypeFloat:  return *(float *)pMemoryValue > *(float *)pScanValue;
             case valTypeDouble: return *(double *)pMemoryValue > *(double *)pScanValue;
             case valTypeArrBytes:
             case valTypeString: return false;
          }
          return false;
       }
       case cmpTypeSmallerThan:
       {

          switch (valType) {
             case valTypeUInt8:  return *pMemoryValue < *pScanValue;
             case valTypeInt8:   return *(int8_t *)pMemoryValue < *(int8_t *)pScanValue;
             case valTypeUInt16: return *(uint16_t *)pMemoryValue < *(uint16_t *)pScanValue;
             case valTypeInt16:  return *(int16_t *)pMemoryValue < *(int16_t *)pScanValue;
             case valTypeUInt32: return *(uint32_t *)pMemoryValue < *(uint32_t *)pScanValue;
             case valTypeInt32:  return *(int32_t *)pMemoryValue < *(int32_t *)pScanValue;
             case valTypeUInt64: return *(uint64_t *)pMemoryValue < *(uint64_t *)pScanValue;
             case valTypeInt64:  return *(int64_t *)pMemoryValue < *(int64_t *)pScanValue;
             case valTypeFloat:  return *(float *)pMemoryValue < *(float *)pScanValue;
             case valTypeDouble: return *(double *)pMemoryValue < *(double *)pScanValue;
             case valTypeArrBytes:
             case valTypeString: return false;
          }
          return false;
       }
       case cmpTypeValueBetween:
       {

          #define _BETWEEN(TY) do { \
             TY mv = *(TY *)pMemoryValue; \
             TY sv = *(TY *)pScanValue; \
             TY ev = *(TY *)pExtraValue; \
             if (ev > sv) return mv > sv && mv < ev; \
             return mv < sv && mv > ev; \
          } while (0)
          switch (valType) {
             case valTypeUInt8:  _BETWEEN(uint8_t);
             case valTypeInt8:   _BETWEEN(int8_t);
             case valTypeUInt16: _BETWEEN(uint16_t);
             case valTypeInt16:  _BETWEEN(int16_t);
             case valTypeUInt32: _BETWEEN(uint32_t);
             case valTypeInt32:  _BETWEEN(int32_t);
             case valTypeUInt64: _BETWEEN(uint64_t);
             case valTypeInt64:  _BETWEEN(int64_t);
             case valTypeFloat:  _BETWEEN(float);
             case valTypeDouble: _BETWEEN(double);
             case valTypeArrBytes:
             case valTypeString: return false;
          }
          #undef _BETWEEN
          return false;
       }
       case cmpTypeIncreasedValue:
       {

          switch (valType) {
             case valTypeUInt8:  return *pMemoryValue > *pExtraValue;
             case valTypeInt8:   return *(int8_t *)pMemoryValue > *(int8_t *)pExtraValue;
             case valTypeUInt16: return *(uint16_t *)pMemoryValue > *(uint16_t *)pExtraValue;
             case valTypeInt16:  return *(int16_t *)pMemoryValue > *(int16_t *)pExtraValue;
             case valTypeUInt32: return *(uint32_t *)pMemoryValue > *(uint32_t *)pExtraValue;
             case valTypeInt32:  return *(int32_t *)pMemoryValue > *(int32_t *)pExtraValue;
             case valTypeUInt64: return *(uint64_t *)pMemoryValue > *(uint64_t *)pExtraValue;
             case valTypeInt64:  return *(int64_t *)pMemoryValue > *(int64_t *)pExtraValue;
             case valTypeFloat:  return *(float *)pMemoryValue > *(float *)pExtraValue;
             case valTypeDouble: return *(double *)pMemoryValue > *(double *)pExtraValue;
             case valTypeArrBytes:
             case valTypeString: return false;
          }
          return false;
       }
       case cmpTypeIncreasedValueBy:
       {

          switch (valType) {
             case valTypeUInt8:  return *pMemoryValue == (uint8_t)(*pExtraValue + *pScanValue);
             case valTypeInt8:   return *(int8_t *)pMemoryValue == (int8_t)(*(int8_t *)pExtraValue + *(int8_t *)pScanValue);
             case valTypeUInt16: return *(uint16_t *)pMemoryValue == (uint16_t)(*(uint16_t *)pExtraValue + *(uint16_t *)pScanValue);
             case valTypeInt16:  return *(int16_t *)pMemoryValue == (int16_t)(*(int16_t *)pExtraValue + *(int16_t *)pScanValue);
             case valTypeUInt32: return *(uint32_t *)pMemoryValue == (*(uint32_t *)pExtraValue + *(uint32_t *)pScanValue);
             case valTypeInt32:  return *(int32_t *)pMemoryValue == (*(int32_t *)pExtraValue + *(int32_t *)pScanValue);
             case valTypeUInt64: return *(uint64_t *)pMemoryValue == (*(uint64_t *)pExtraValue + *(uint64_t *)pScanValue);
             case valTypeInt64:  return *(int64_t *)pMemoryValue == (*(int64_t *)pExtraValue + *(int64_t *)pScanValue);
             case valTypeFloat:  return *(float *)pMemoryValue == (*(float *)pExtraValue + *(float *)pScanValue);
             case valTypeDouble: {

                double delta_d = *(double *)pScanValue;
                return *(double *)pMemoryValue == (*(double *)pExtraValue + delta_d);
             }
             case valTypeArrBytes:
             case valTypeString: return false;
          }
          return false;
       }
       case cmpTypeDecreasedValue:
       {

          switch (valType) {
             case valTypeUInt8:  return *pMemoryValue < *pExtraValue;
             case valTypeInt8:   return *(int8_t *)pMemoryValue < *(int8_t *)pExtraValue;
             case valTypeUInt16: return *(uint16_t *)pMemoryValue < *(uint16_t *)pExtraValue;
             case valTypeInt16:  return *(int16_t *)pMemoryValue < *(int16_t *)pExtraValue;
             case valTypeUInt32: return *(uint32_t *)pMemoryValue < *(uint32_t *)pExtraValue;
             case valTypeInt32:  return *(int32_t *)pMemoryValue < *(int32_t *)pExtraValue;
             case valTypeUInt64: return *(uint64_t *)pMemoryValue < *(uint64_t *)pExtraValue;
             case valTypeInt64:  return *(int64_t *)pMemoryValue < *(int64_t *)pExtraValue;
             case valTypeFloat:  return *(float *)pMemoryValue < *(float *)pExtraValue;
             case valTypeDouble: return *(double *)pMemoryValue < *(double *)pExtraValue;
             case valTypeArrBytes:
             case valTypeString: return false;
          }
          return false;
       }
       case cmpTypeDecreasedValueBy:
       {

          switch (valType) {
             case valTypeUInt8:  return *pMemoryValue == (uint8_t)(*pExtraValue - *pScanValue);
             case valTypeInt8:   return *(int8_t *)pMemoryValue == (int8_t)(*(int8_t *)pExtraValue - *(int8_t *)pScanValue);
             case valTypeUInt16: return *(uint16_t *)pMemoryValue == (uint16_t)(*(uint16_t *)pExtraValue - *(uint16_t *)pScanValue);
             case valTypeInt16:  return *(int16_t *)pMemoryValue == (int16_t)(*(int16_t *)pExtraValue - *(int16_t *)pScanValue);
             case valTypeUInt32: return *(uint32_t *)pMemoryValue == (*(uint32_t *)pExtraValue - *(uint32_t *)pScanValue);
             case valTypeInt32:  return *(int32_t *)pMemoryValue == (*(int32_t *)pExtraValue - *(int32_t *)pScanValue);
             case valTypeUInt64: return *(uint64_t *)pMemoryValue == (*(uint64_t *)pExtraValue - *(uint64_t *)pScanValue);
             case valTypeInt64:  return *(int64_t *)pMemoryValue == (*(int64_t *)pExtraValue - *(int64_t *)pScanValue);
             case valTypeFloat:  return *(float *)pMemoryValue == (*(float *)pExtraValue - *(float *)pScanValue);
             case valTypeDouble: {

                double delta_d = *(double *)pScanValue;
                return *(double *)pMemoryValue == (*(double *)pExtraValue - delta_d);
             }
             case valTypeArrBytes:
             case valTypeString: return false;
          }
          return false;
       }
       case cmpTypeChangedValue:
       {

          switch (valType) {
             case valTypeUInt8:  return *pMemoryValue != *pExtraValue;
             case valTypeInt8:   return *(int8_t *)pMemoryValue != *(int8_t *)pExtraValue;
             case valTypeUInt16: return *(uint16_t *)pMemoryValue != *(uint16_t *)pExtraValue;
             case valTypeInt16:  return *(int16_t *)pMemoryValue != *(int16_t *)pExtraValue;
             case valTypeUInt32: return *(uint32_t *)pMemoryValue != *(uint32_t *)pExtraValue;
             case valTypeInt32:  return *(int32_t *)pMemoryValue != *(int32_t *)pExtraValue;
             case valTypeUInt64: return *(uint64_t *)pMemoryValue != *(uint64_t *)pExtraValue;
             case valTypeInt64:  return *(int64_t *)pMemoryValue != *(int64_t *)pExtraValue;
             case valTypeFloat:  return *(float *)pMemoryValue != *(float *)pExtraValue;
             case valTypeDouble: return *(double *)pMemoryValue != *(double *)pExtraValue;
             case valTypeArrBytes:
             case valTypeString: return false;
          }
          return false;
       }
       case cmpTypeUnchangedValue:
       {

          switch (valType) {
             case valTypeUInt8:  return *pMemoryValue == *pExtraValue;
             case valTypeInt8:   return *(int8_t *)pMemoryValue == *(int8_t *)pExtraValue;
             case valTypeUInt16: return *(uint16_t *)pMemoryValue == *(uint16_t *)pExtraValue;
             case valTypeInt16:  return *(int16_t *)pMemoryValue == *(int16_t *)pExtraValue;
             case valTypeUInt32: return *(uint32_t *)pMemoryValue == *(uint32_t *)pExtraValue;
             case valTypeInt32:  return *(int32_t *)pMemoryValue == *(int32_t *)pExtraValue;
             case valTypeUInt64: return *(uint64_t *)pMemoryValue == *(uint64_t *)pExtraValue;
             case valTypeInt64:  return *(int64_t *)pMemoryValue == *(int64_t *)pExtraValue;
             case valTypeFloat:  return *(float *)pMemoryValue == *(float *)pExtraValue;
             case valTypeDouble: return *(double *)pMemoryValue == *(double *)pExtraValue;
             case valTypeArrBytes:
             case valTypeString: return false;
          }
          return false;
       }
       case cmpTypeUnknownInitialValue:
       case cmpTypeUnknownInitialLowValue:
       {

          return true;
       }
    }
    return false;
}

bool proc_scan_compareValues(cmd_proc_scan_comparetype cmpType, cmd_proc_scan_valuetype valType, size_t valTypeLength,
                                 unsigned char *pScanValue, unsigned char *pMemoryValue, unsigned char *pExtraValue,
                                 unsigned char *pMask) {
    switch (cmpType) {
       case cmpTypeExactValue:
       {

          if (valType == valTypeArrBytes && pMask != NULL) {
             for (size_t j = 0; j < valTypeLength; j++) {
                if (pScanValue[j] != pMemoryValue[j]) {
                   if (pMask[j] == 1) return false;

                }
             }
             return true;
          }

          if (valType == valTypeFloat) {
             uint32_t scanBits;
             memcpy(&scanBits, pScanValue, 4);
             if (scanBits == 0) {

                uint32_t memBits;
                memcpy(&memBits, pMemoryValue, 4);
                return memBits == 0;
             }
             float scanV, memV;
             memcpy(&scanV, pScanValue, 4);
             memcpy(&memV,  pMemoryValue, 4);
             return fuzzy_float_compare(scanV, memV);
          }

          if (valType == valTypeDouble) {
             uint64_t scanBits;
             memcpy(&scanBits, pScanValue, 8);
             if (scanBits == 0) {
                uint64_t memBits;
                memcpy(&memBits, pMemoryValue, 8);
                return memBits == 0;
             }
             double scanV, memV;
             memcpy(&scanV, pScanValue, 8);
             memcpy(&memV,  pMemoryValue, 8);
             return fuzzy_double_compare(scanV, memV);
          }

          bool isFound = false;
          for (size_t j = 0; j < valTypeLength; j++) {
             isFound = (pScanValue[j] == pMemoryValue[j]);
             if (!isFound)
                break;
          }
          return isFound;
       }
       case cmpTypeFuzzyValue:
       {
          if (valType == valTypeFloat) {
             float diff = *(float *)pScanValue - *(float *)pMemoryValue;
             return diff < 1.0f && diff > -1.0f;
          }
          else if (valType == valTypeDouble) {
             double diff = *(double *)pScanValue - *(double *)pMemoryValue;
             return diff < 1.0 && diff > -1.0;
          }
          else {
             return false;
          }
       }
       case cmpTypeBiggerThan:
       {
          switch (valType) {
             case valTypeUInt8:
                return *pMemoryValue > *pScanValue;
             case valTypeInt8:
                return *(int8_t *)pMemoryValue > *(int8_t *)pScanValue;
             case valTypeUInt16:
                return *(uint16_t *)pMemoryValue > *(uint16_t *)pScanValue;
             case valTypeInt16:
                return *(int16_t *)pMemoryValue > *(int16_t *)pScanValue;
             case valTypeUInt32:
                return *(uint32_t *)pMemoryValue > *(uint32_t *)pScanValue;
             case valTypeInt32:
                return *(int32_t *)pMemoryValue > *(int32_t *)pScanValue;
             case valTypeUInt64:
                return *(uint64_t *)pMemoryValue > *(uint64_t *)pScanValue;
             case valTypeInt64:
                return *(int64_t *)pMemoryValue > *(int64_t *)pScanValue;
             case valTypeFloat:
                return *(float *)pMemoryValue > *(float *)pScanValue;
             case valTypeDouble:
                return *(double *)pMemoryValue > *(double *)pScanValue;
             case valTypeArrBytes:
             case valTypeString:
                return false;
          }
       }
       case cmpTypeSmallerThan:
       {
          switch (valType) {
             case valTypeUInt8:
                return *pMemoryValue < *pScanValue;
             case valTypeInt8:
                return *(int8_t *)pMemoryValue < *(int8_t *)pScanValue;
             case valTypeUInt16:
                return *(uint16_t *)pMemoryValue < *(uint16_t *)pScanValue;
             case valTypeInt16:
                return *(int16_t *)pMemoryValue < *(int16_t *)pScanValue;
             case valTypeUInt32:
                return *(uint32_t *)pMemoryValue < *(uint32_t *)pScanValue;
             case valTypeInt32:
                return *(int32_t *)pMemoryValue < *(int32_t *)pScanValue;
             case valTypeUInt64:
                return *(uint64_t *)pMemoryValue < *(uint64_t *)pScanValue;
             case valTypeInt64:
                return *(int64_t *)pMemoryValue < *(int64_t *)pScanValue;
             case valTypeFloat:
                return *(float *)pMemoryValue < *(float *)pScanValue;
             case valTypeDouble:
                return *(double *)pMemoryValue < *(double *)pScanValue;
             case valTypeArrBytes:
             case valTypeString:
                return false;
          }
       }
       case cmpTypeValueBetween:
       {

          switch (valType) {
             case valTypeUInt8:
                if (*pExtraValue > *pScanValue)
                   return *pMemoryValue >= *pScanValue && *pMemoryValue <= *pExtraValue;
                return *pMemoryValue <= *pScanValue && *pMemoryValue >= *pExtraValue;
             case valTypeInt8:
                if (*(int8_t *)pExtraValue > *(int8_t *)pScanValue)
                   return *(int8_t *)pMemoryValue >= *(int8_t *)pScanValue && *(int8_t *)pMemoryValue <= *(int8_t*)pExtraValue;
                return *(int8_t *)pMemoryValue <= *(int8_t *)pScanValue && *(int8_t *)pMemoryValue >= *(int8_t *)pExtraValue;
             case valTypeUInt16:
                if (*(uint16_t *)pExtraValue > *(uint16_t *)pScanValue)
                   return *(uint16_t *)pMemoryValue >= *(uint16_t *)pScanValue && *(uint16_t *)pMemoryValue <= *(uint16_t*)pExtraValue;
                return *(uint16_t *)pMemoryValue <= *(uint16_t *)pScanValue && *(uint16_t *)pMemoryValue >= *(uint16_t *)pExtraValue;
             case valTypeInt16:
                if (*(int16_t *)pExtraValue > *(int16_t *)pScanValue)
                   return *(int16_t *)pMemoryValue >= *(int16_t *)pScanValue && *(int16_t *)pMemoryValue <= *(int16_t*)pExtraValue;
                return *(int16_t *)pMemoryValue <= *(int16_t *)pScanValue && *(int16_t *)pMemoryValue >= *(int16_t *)pExtraValue;
             case valTypeUInt32:
                if (*(uint32_t *)pExtraValue > *(uint32_t *)pScanValue)
                   return *(uint32_t *)pMemoryValue >= *(uint32_t *)pScanValue && *(uint32_t *)pMemoryValue <= *(uint32_t*)pExtraValue;
                return *(uint32_t *)pMemoryValue <= *(uint32_t *)pScanValue && *(uint32_t *)pMemoryValue >= *(uint32_t *)pExtraValue;
             case valTypeInt32:
                if (*(int32_t *)pExtraValue > *(int32_t *)pScanValue)
                   return *(int32_t *)pMemoryValue >= *(int32_t *)pScanValue && *(int32_t *)pMemoryValue <= *(int32_t*)pExtraValue;
                return *(int32_t *)pMemoryValue <= *(int32_t *)pScanValue && *(int32_t *)pMemoryValue >= *(int32_t *)pExtraValue;
             case valTypeUInt64:
                if (*(uint64_t *)pExtraValue > *(uint64_t *)pScanValue)
                   return *(uint64_t *)pMemoryValue >= *(uint64_t *)pScanValue && *(uint64_t *)pMemoryValue <= *(uint64_t*)pExtraValue;
                return *(uint64_t *)pMemoryValue <= *(uint64_t *)pScanValue && *(uint64_t *)pMemoryValue >= *(uint64_t *)pExtraValue;
             case valTypeInt64:
                if (*(int64_t *)pExtraValue > *(int64_t *)pScanValue)
                   return *(int64_t *)pMemoryValue >= *(int64_t *)pScanValue && *(int64_t *)pMemoryValue <= *(int64_t*)pExtraValue;
                return *(int64_t *)pMemoryValue <= *(int64_t *)pScanValue && *(int64_t *)pMemoryValue >= *(int64_t *)pExtraValue;
             case valTypeFloat:
                if (*(float *)pExtraValue > *(float *)pScanValue)
                   return *(float *)pMemoryValue >= *(float *)pScanValue && *(float *)pMemoryValue <= *(float*)pExtraValue;
                return *(float *)pMemoryValue <= *(float *)pScanValue && *(float *)pMemoryValue >= *(float *)pExtraValue;
             case valTypeDouble:
                if (*(double *)pExtraValue > *(double *)pScanValue)
                   return *(double *)pMemoryValue >= *(double *)pScanValue && *(double *)pMemoryValue <= *(double*)pExtraValue;
                return *(double *)pMemoryValue <= *(double *)pScanValue && *(double *)pMemoryValue >= *(double *)pExtraValue;
             case valTypeArrBytes:
             case valTypeString:
                return false;
          }
       }
       case cmpTypeIncreasedValue:
       {
          switch (valType) {
             case valTypeUInt8:
                return *pMemoryValue > *pExtraValue;
             case valTypeInt8:
                return *(int8_t *)pMemoryValue > *(int8_t *)pExtraValue;
             case valTypeUInt16:
                return *(uint16_t *)pMemoryValue > *(uint16_t *)pExtraValue;
             case valTypeInt16:
                return *(int16_t *)pMemoryValue > *(int16_t *)pExtraValue;
             case valTypeUInt32:
                return *(uint32_t *)pMemoryValue > *(uint32_t *)pExtraValue;
             case valTypeInt32:
                return *(int32_t *)pMemoryValue > *(int32_t *)pExtraValue;
             case valTypeUInt64:
                return *(uint64_t *)pMemoryValue > *(uint64_t *)pExtraValue;
             case valTypeInt64:
                return *(int64_t *)pMemoryValue > *(int64_t *)pExtraValue;
             case valTypeFloat:
                return *(float *)pMemoryValue > *(float *)pExtraValue;
             case valTypeDouble:
                return *(double *)pMemoryValue > *(double *)pExtraValue;
             case valTypeArrBytes:
             case valTypeString:
                return false;
          }
       }
       case cmpTypeIncreasedValueBy:
       {
          switch (valType) {
             case valTypeUInt8:
                return *pMemoryValue == (*pExtraValue + *pScanValue);
             case valTypeInt8:
                return *(int8_t *)pMemoryValue == (*(int8_t *)pExtraValue + *(int8_t *)pScanValue);
             case valTypeUInt16:
                return *(uint16_t *)pMemoryValue == (*(uint16_t *)pExtraValue + *(uint16_t *)pScanValue);
             case valTypeInt16:
                return *(int16_t *)pMemoryValue == (*(int16_t *)pExtraValue + *(int16_t *)pScanValue);
             case valTypeUInt32:
                return *(uint32_t *)pMemoryValue == (*(uint32_t *)pExtraValue + *(uint32_t *)pScanValue);
             case valTypeInt32:
                return *(int32_t *)pMemoryValue == (*(int32_t *)pExtraValue + *(int32_t *)pScanValue);
             case valTypeUInt64:
                return *(uint64_t *)pMemoryValue == (*(uint64_t *)pExtraValue + *(uint64_t *)pScanValue);
             case valTypeInt64:
                return *(int64_t *)pMemoryValue == (*(int64_t *)pExtraValue + *(int64_t *)pScanValue);
             case valTypeFloat:
                return *(float *)pMemoryValue == (*(float *)pExtraValue + *(float *)pScanValue);
             case valTypeDouble:

                return *(double *)pMemoryValue == (*(double *)pExtraValue + *(double *)pScanValue);
             case valTypeArrBytes:
             case valTypeString:
                return false;
          }
       }
       case cmpTypeDecreasedValue:
       {
          switch (valType) {
             case valTypeUInt8:
                return *pMemoryValue < *pExtraValue;
             case valTypeInt8:
                return *(int8_t *)pMemoryValue < *(int8_t *)pExtraValue;
             case valTypeUInt16:
                return *(uint16_t *)pMemoryValue < *(uint16_t *)pExtraValue;
             case valTypeInt16:
                return *(int16_t *)pMemoryValue < *(int16_t *)pExtraValue;
             case valTypeUInt32:
                return *(uint32_t *)pMemoryValue < *(uint32_t *)pExtraValue;
             case valTypeInt32:
                return *(int32_t *)pMemoryValue < *(int32_t *)pExtraValue;
             case valTypeUInt64:
                return *(uint64_t *)pMemoryValue < *(uint64_t *)pExtraValue;
             case valTypeInt64:
                return *(int64_t *)pMemoryValue < *(int64_t *)pExtraValue;
             case valTypeFloat:
                return *(float *)pMemoryValue < *(float *)pExtraValue;
             case valTypeDouble:
                return *(double *)pMemoryValue < *(double *)pExtraValue;
             case valTypeArrBytes:
             case valTypeString:
                return false;
          }
       }
       case cmpTypeDecreasedValueBy:
       {
          switch (valType) {
             case valTypeUInt8:
                return *pMemoryValue == (*pExtraValue - *pScanValue);
             case valTypeInt8:
                return *(int8_t *)pMemoryValue == (*(int8_t *)pExtraValue - *(int8_t *)pScanValue);
             case valTypeUInt16:
                return *(uint16_t *)pMemoryValue == (*(uint16_t *)pExtraValue - *(uint16_t *)pScanValue);
             case valTypeInt16:
                return *(int16_t *)pMemoryValue == (*(int16_t *)pExtraValue - *(int16_t *)pScanValue);
             case valTypeUInt32:
                return *(uint32_t *)pMemoryValue == (*(uint32_t *)pExtraValue - *(uint32_t *)pScanValue);
             case valTypeInt32:
                return *(int32_t *)pMemoryValue == (*(int32_t *)pExtraValue - *(int32_t *)pScanValue);
             case valTypeUInt64:
                return *(uint64_t *)pMemoryValue == (*(uint64_t *)pExtraValue - *(uint64_t *)pScanValue);
             case valTypeInt64:
                return *(int64_t *)pMemoryValue == (*(int64_t *)pExtraValue - *(int64_t *)pScanValue);
             case valTypeFloat:
                return *(float *)pMemoryValue == (*(float *)pExtraValue - *(float *)pScanValue);
             case valTypeDouble:

                return *(double *)pMemoryValue == (*(double *)pExtraValue - *(double *)pScanValue);
             case valTypeArrBytes:
             case valTypeString:
                return false;
          }
       }
       case cmpTypeChangedValue:
       {
          switch (valType) {
             case valTypeUInt8:
                return *pMemoryValue != *pExtraValue;
             case valTypeInt8:
                return *(int8_t *)pMemoryValue != *(int8_t *)pExtraValue;
             case valTypeUInt16:
                return *(uint16_t *)pMemoryValue != *(uint16_t *)pExtraValue;
             case valTypeInt16:
                return *(int16_t *)pMemoryValue != *(int16_t *)pExtraValue;
             case valTypeUInt32:
                return *(uint32_t *)pMemoryValue != *(uint32_t *)pExtraValue;
             case valTypeInt32:
                return *(int32_t *)pMemoryValue != *(int32_t *)pExtraValue;
             case valTypeUInt64:
                return *(uint64_t *)pMemoryValue != *(uint64_t *)pExtraValue;
             case valTypeInt64:
                return *(int64_t *)pMemoryValue != *(int64_t *)pExtraValue;
             case valTypeFloat:
                return *(float *)pMemoryValue != *(float *)pExtraValue;
             case valTypeDouble:
                return *(double *)pMemoryValue != *(double *)pExtraValue;
             case valTypeArrBytes:
             case valTypeString:
                return false;
          }
       }
       case cmpTypeUnchangedValue:
       {
          switch (valType) {
             case valTypeUInt8:
                return *pMemoryValue == *pExtraValue;
             case valTypeInt8:
                return *(int8_t *)pMemoryValue == *(int8_t *)pExtraValue;
             case valTypeUInt16:
                return *(uint16_t *)pMemoryValue == *(uint16_t *)pExtraValue;
             case valTypeInt16:
                return *(int16_t *)pMemoryValue == *(int16_t *)pExtraValue;
             case valTypeUInt32:
                return *(uint32_t *)pMemoryValue == *(uint32_t *)pExtraValue;
             case valTypeInt32:
                return *(int32_t *)pMemoryValue == *(int32_t *)pExtraValue;
             case valTypeUInt64:
                return *(uint64_t *)pMemoryValue == *(uint64_t *)pExtraValue;
             case valTypeInt64:
                return *(int64_t *)pMemoryValue == *(int64_t *)pExtraValue;
             case valTypeFloat:
                return *(float *)pMemoryValue == *(float *)pExtraValue;
             case valTypeDouble:
                return *(double *)pMemoryValue == *(double *)pExtraValue;
             case valTypeArrBytes:
             case valTypeString:
                return false;
          }
       }
       case cmpTypeUnknownInitialValue:
       {

          switch (valType) {
             case valTypeUInt8:  case valTypeInt8:
                return *pMemoryValue != 0;
             case valTypeUInt16: case valTypeInt16:
                return *(uint16_t *)pMemoryValue != 0;
             case valTypeUInt32: case valTypeInt32:
                return *(uint32_t *)pMemoryValue != 0;
             case valTypeUInt64: case valTypeInt64:
                return *(uint64_t *)pMemoryValue != 0;
             case valTypeFloat:
                return *(float  *)pMemoryValue != 0.0f;
             case valTypeDouble:
                return *(double *)pMemoryValue != 0.0;
             case valTypeArrBytes:
             case valTypeString:
             default:
                return false;
          }
       }
       case cmpTypeUnknownInitialLowValue:
       {

          switch (valType) {
             case valTypeUInt8:
                if (*pMemoryValue == 0) return false;
                return *pMemoryValue <= *pScanValue;
             case valTypeInt8: {
                int8_t mv = *(int8_t *)pMemoryValue;
                int8_t sv = *(int8_t *)pScanValue;
                if (mv == 0) return false;
                return (uint64_t)(int64_t)mv <= (uint64_t)(int64_t)sv;
             }
             case valTypeUInt16: {
                uint16_t mv = *(uint16_t *)pMemoryValue;
                if (mv == 0) return false;
                return mv <= *(uint16_t *)pScanValue;
             }
             case valTypeInt16: {
                int16_t mv = *(int16_t *)pMemoryValue;
                int16_t sv = *(int16_t *)pScanValue;
                if (mv == 0) return false;
                return (uint64_t)(int64_t)mv <= (uint64_t)(int64_t)sv;
             }
             case valTypeUInt32: {
                uint32_t mv = *(uint32_t *)pMemoryValue;
                if (mv == 0) return false;
                return mv <= *(uint32_t *)pScanValue;
             }
             case valTypeInt32: {
                int32_t mv = *(int32_t *)pMemoryValue;
                int32_t sv = *(int32_t *)pScanValue;
                if (mv == 0) return false;
                return (uint64_t)(int64_t)mv <= (uint64_t)(int64_t)sv;
             }
             case valTypeUInt64: {
                uint64_t mv = *(uint64_t *)pMemoryValue;
                if (mv == 0) return false;
                return mv <= *(uint64_t *)pScanValue;
             }
             case valTypeInt64: {
                int64_t mv = *(int64_t *)pMemoryValue;
                int64_t sv = *(int64_t *)pScanValue;
                if (mv == 0) return false;
                return (uint64_t)mv <= (uint64_t)sv;
             }
             case valTypeFloat: {
                float mv = *(float *)pMemoryValue;
                if (mv == 0.0f) return false;
                float abs_mv = mv < 0.0f ? -mv : mv;
                return abs_mv <= *(float *)pScanValue;
             }
             case valTypeDouble: {
                double mv = *(double *)pMemoryValue;
                if (mv == 0.0) return false;
                double abs_mv = mv < 0.0 ? -mv : mv;
                return abs_mv <= *(double *)pScanValue;
             }
             case valTypeArrBytes:
             case valTypeString:
             default:
                return false;
          }
       }
    }
    return false;
}

int proc_scan_handle(int fd, struct cmd_packet *packet) {
    struct cmd_proc_scan_packet *sp = (struct cmd_proc_scan_packet *)packet->data;

    if(!sp) {
        net_send_int32(fd, CMD_DATA_NULL);
       return 1;
    }

    size_t valueLength = proc_scan_getSizeOfValueType(sp->valueType);
    if (!valueLength) {
       valueLength = sp->lenData;
    }

    unsigned char *data = (unsigned char *)net_alloc_buffer(sp->lenData);
    if (!data) {
       net_send_int32(fd, CMD_DATA_NULL);
       return 1;
    }

    net_send_int32(fd, CMD_SUCCESS);

    net_recv_all(fd, data, sp->lenData, 1);

    struct sys_proc_vm_map_args args;
    memset(&args, NULL, sizeof(struct sys_proc_vm_map_args));
    if (sys_proc_cmd(sp->pid, SYS_PROC_VM_MAP, &args)) {
        free(data);
        net_send_int32(fd, CMD_ERROR);
        return 1;
    }

    size_t size = args.num * sizeof(struct proc_vm_map_entry);
    args.maps = (struct proc_vm_map_entry *)net_alloc_buffer(size);
    if (!args.maps) {
        free(data);
        net_send_int32(fd, CMD_DATA_NULL);
        return 1;
    }

    if (sys_proc_cmd(sp->pid, SYS_PROC_VM_MAP, &args)) {
        free(args.maps);
        free(data);
        net_send_int32(fd, CMD_ERROR);
        return 1;
    }

    net_send_int32(fd, CMD_SUCCESS);

    unsigned char *pExtraValue = valueLength == sp->lenData ? NULL : &data[valueLength];
    unsigned char *scanBuffer = (unsigned char *)net_alloc_buffer(PAGE_SIZE);
    for (size_t i = 0; i < args.num; i++) {
       if ((args.maps[i].prot & PROT_READ) != PROT_READ) {
            continue;
       }

       uint64_t sectionStartAddr = args.maps[i].start;
       size_t sectionLen = args.maps[i].end - sectionStartAddr;

       for (uint64_t j = 0; j < sectionLen; j += valueLength) {
            if(j == 0 || !(j % PAGE_SIZE)) {
                sys_proc_rw(sp->pid, sectionStartAddr, scanBuffer, PAGE_SIZE, 0);
            }

            uint64_t scanOffset = j % PAGE_SIZE;
            uint64_t curAddress = sectionStartAddr + j;

            if (proc_scan_legacy_compareValues(sp->compareType, sp->valueType, valueLength, data, scanBuffer + scanOffset, pExtraValue)) {
                net_send_all(fd, &curAddress, sizeof(uint64_t));
            }
       }
    }

    uint64_t endflag = 0xFFFFFFFFFFFFFFFF;
    net_send_all(fd, &endflag, sizeof(uint64_t));

    free(scanBuffer);
    free(args.maps);
    free(data);

    return 0;
}

int proc_info_handle(int fd, struct cmd_packet *packet) {
    struct cmd_proc_info_packet *ip;
    struct sys_proc_info_args args;
    struct cmd_proc_info_response resp;

    ip = (struct cmd_proc_info_packet *)packet->data;

    if(ip) {
        sys_proc_cmd(ip->pid, SYS_PROC_INFO, &args);

        resp.pid = args.pid;
        memcpy(resp.name, args.name, sizeof(resp.name));
        memcpy(resp.path, args.path, sizeof(resp.path));
        memcpy(resp.titleid, args.titleid, sizeof(resp.titleid));
        memcpy(resp.contentid, args.contentid, sizeof(resp.contentid));

        net_send_int32(fd, CMD_SUCCESS);
        net_send_all(fd, &resp, CMD_PROC_INFO_RESPONSE_SIZE);
        return 0;
    }

    net_send_int32(fd, CMD_DATA_NULL);

    return 0;
}

int proc_alloc_handle(int fd, struct cmd_packet *packet) {
    struct cmd_proc_alloc_packet *ap;
    struct sys_proc_alloc_args args;
    struct cmd_proc_alloc_response resp;

    ap = (struct cmd_proc_alloc_packet *)packet->data;

    if(ap) {
        args.length = ap->length;
        sys_proc_cmd(ap->pid, SYS_PROC_ALLOC, &args);

        resp.address = args.address;

        net_send_int32(fd, CMD_SUCCESS);
        net_send_all(fd, &resp, CMD_PROC_ALLOC_RESPONSE_SIZE);
        return 0;
    }

    net_send_int32(fd, CMD_DATA_NULL);

    return 0;
}

int proc_free_handle(int fd, struct cmd_packet *packet) {
    struct cmd_proc_free_packet *fp;
    struct sys_proc_free_args args;

    fp = (struct cmd_proc_free_packet *)packet->data;

    if(fp) {
        args.address = fp->address;
        args.length = fp->length;
        sys_proc_cmd(fp->pid, SYS_PROC_FREE, &args);

        net_send_int32(fd, CMD_SUCCESS);
        return 0;
    }

    net_send_int32(fd, CMD_DATA_NULL);

    return 0;
}

int proc_alloc_hinted_handle(int fd, struct cmd_packet *packet) {
    struct cmd_proc_alloc_hinted_packet *ahp;
    struct sys_proc_alloc_args args;
    struct cmd_proc_alloc_response resp;

    ahp = (struct cmd_proc_alloc_hinted_packet *)packet->data;

    if(ahp) {
        args.address = ahp->hint;
        args.length = ahp->length;
        sys_proc_cmd(ahp->pid, SYS_PROC_ALLOC_HINTED, &args);

        resp.address = args.address;

        net_send_int32(fd, CMD_SUCCESS);
        net_send_all(fd, &resp, CMD_PROC_ALLOC_RESPONSE_SIZE);
        return 0;
    }

    net_send_int32(fd, CMD_DATA_NULL);

    return 0;
}

int proc_scan_aob_handle(int fd, struct cmd_packet *packet) {
    struct cmd_proc_scan_aob_packet *sp = (struct cmd_proc_scan_aob_packet *)packet->data;
    if (!sp) {
        net_send_int32(fd, CMD_DATA_NULL);
        return 1;
    }
    uint32_t plen = sp->pattern_length;
    if (plen == 0) {
        net_send_int32(fd, CMD_DATA_NULL);
        return 1;
    }

    uint8_t *pattern = (uint8_t *)net_alloc_buffer(plen);
    if (!pattern) {
        net_send_int32(fd, CMD_DATA_NULL);
        return 1;
    }
    uint8_t *mask = (uint8_t *)net_alloc_buffer(plen);
    if (!mask) {
        free(pattern);
        net_send_int32(fd, CMD_DATA_NULL);
        return 1;
    }

    net_send_int32(fd, CMD_SUCCESS);
    net_recv_all(fd, pattern, plen, 1);
    net_recv_all(fd, mask, plen, 1);

    uint32_t chunkSize = 0x8000;
    if (chunkSize % plen != 0) {
        chunkSize = (chunkSize / plen) * plen;
    }

    if (chunkSize < plen) chunkSize = plen;

    uint8_t *readBuf = (uint8_t *)net_alloc_buffer(chunkSize);
    if (!readBuf) {
        free(pattern); free(mask);
        net_send_int32(fd, CMD_DATA_NULL);
        return 1;
    }

    net_send_int32(fd, CMD_SUCCESS);

    uint8_t target_count = sp->max_matches;
    bool stop_unique = (sp->stop_flag == 1);

    uint64_t addr = sp->address;
    uint64_t remaining = sp->length;
    uint64_t match_addr = 0;
    uint64_t match_count = 0;
    bool invalidated = false;

    while (remaining > 0) {
        uint32_t toRead = chunkSize < remaining ? chunkSize : (uint32_t)remaining;
        memset(readBuf, 0, toRead);
        sys_proc_rw(sp->pid, addr, readBuf, toRead, 0);

        uint64_t limit = toRead >= plen ? toRead - plen : 0;
        for (uint64_t j = 0; j <= limit; j++) {

            bool matched = true;
            for (uint32_t k = 0; k < plen; k++) {
                if (readBuf[j+k] != pattern[k] && mask[k] == 1) {
                    matched = false;
                    break;
                }
            }
            if (!matched) continue;

            match_count++;
            if (match_count == target_count) {

                match_addr = addr + j;
                if (stop_unique) continue;
                goto chunk_done;
            }
            if (match_count > target_count && stop_unique) {

                match_addr = 0;
                invalidated = true;
                goto chunk_done;
            }
        }
chunk_done:

        {
            uint32_t advance = chunkSize - plen + 1;
            addr += advance;
            if (remaining > advance) remaining -= advance;
            else remaining = 0;
        }

        if (invalidated && stop_unique) break;
        if (!stop_unique && match_count >= target_count) break;
    }

    net_send_all(fd, &match_addr, 8);
    free(readBuf);
    free(pattern);
    free(mask);
    net_send_int32(fd, CMD_SUCCESS);
    return 0;
}

#define AOB_SCAN_THREADS 6

struct aob_ps4_fill { uint64_t pid; uint64_t base; };

static void aob_ps4_fill_fn(void *ctx, uint64_t off, uint8_t *buf, uint32_t len) {
    struct aob_ps4_fill *f = (struct aob_ps4_fill *)ctx;
    sys_proc_rw(f->pid, f->base + off, buf, len, 0);
}

static void *aob_thread(void *arg) {
    aob_scan_range((struct aob_worker *)arg);
    return NULL;
}

int proc_scan_aob_multi_handle(int fd, struct cmd_packet *packet) {
    struct cmd_proc_scan_aob_multi_packet *mp = (struct cmd_proc_scan_aob_multi_packet *)packet->data;
    if (!mp) {
        net_send_int32(fd, CMD_DATA_NULL);
        return 1;
    }
    uint32_t patterns_length = mp->patterns_length;
    if (patterns_length == 0) {
        net_send_int32(fd, CMD_DATA_NULL);
        return 1;
    }

    uint8_t *blob = (uint8_t *)net_alloc_buffer(patterns_length);
    if (!blob) {
        net_send_int32(fd, CMD_DATA_NULL);
        return 1;
    }

    net_send_int32(fd, CMD_SUCCESS);
    net_recv_all(fd, blob, patterns_length, 1);

    uint32_t pat_count = 0;
    uint32_t max_plen = 1;
    {
        uint32_t cursor = 0;
        while (cursor + 5 <= patterns_length) {
            uint32_t plen;
            memcpy(&plen, blob + cursor + 1, 4);
            if (plen == 0 || cursor + 5 + 2 * plen > patterns_length) break;
            if (plen > max_plen) max_plen = plen;
            cursor += 5 + 2 * plen;
            pat_count++;
        }
    }

    if (pat_count == 0) {
        free(blob);
        net_send_int32(fd, CMD_DATA_NULL);
        return 1;
    }

    uint32_t chunkSize = 0x10000;

    struct aob_pat *pats = NULL;
    uint32_t *bidx = NULL, *widx = NULL, *slot_off = NULL;
    uint64_t *output = NULL;
    uint8_t  *readbufs = NULL, *wdone = NULL;
    uint64_t *waddrs = NULL;
    uint32_t *wcounts = NULL;
    struct aob_dispatch disp;
    uint32_t total_slots = 0;
    uint32_t num_workers = 1;
    uint64_t span = 1;
    bool stop_unique = (mp->stop_flag == 1);

    pats     = (struct aob_pat *)net_alloc_buffer((size_t)pat_count * sizeof(struct aob_pat));
    bidx     = (uint32_t *)net_alloc_buffer((size_t)pat_count * sizeof(uint32_t));
    widx     = (uint32_t *)net_alloc_buffer((size_t)pat_count * sizeof(uint32_t));
    slot_off = (uint32_t *)net_alloc_buffer((size_t)(pat_count + 1) * sizeof(uint32_t));
    output   = (uint64_t *)net_alloc_buffer((size_t)pat_count * 8);
    if (!pats || !bidx || !widx || !slot_off || !output) goto aob_fail;

    {
        uint32_t cursor = 0;
        for (uint32_t pi = 0; pi < pat_count; pi++) {
            uint32_t plen;
            memcpy(&plen, blob + cursor + 1, 4);
            const uint8_t *pat = blob + cursor + 5;
            const uint8_t *msk = pat + plen;
            pats[pi].pat = pat;
            pats[pi].msk = msk;
            pats[pi].plen = plen;
            pats[pi].target_count = blob[cursor];
            pats[pi].first_fixed = (msk[0] == 1) ? (int16_t)pat[0] : (int16_t)-1;
            cursor += 5 + 2 * plen;
        }
    }
    aob_build_dispatch(pats, pat_count, &disp, bidx, widx);

    slot_off[0] = 0;
    for (uint32_t pi = 0; pi < pat_count; pi++)
        slot_off[pi + 1] = slot_off[pi] + ((uint32_t)pats[pi].target_count + 1);
    total_slots = slot_off[pat_count];

    num_workers = AOB_SCAN_THREADS;
    if ((uint64_t)num_workers > mp->length) num_workers = (mp->length > 0) ? (uint32_t)mp->length : 1;
    span = (mp->length + num_workers - 1) / num_workers;
    if (span == 0) span = 1;
    num_workers = (uint32_t)((mp->length + span - 1) / span);
    if (num_workers == 0) num_workers = 1;
    if (num_workers > AOB_SCAN_THREADS) num_workers = AOB_SCAN_THREADS;

    readbufs = (uint8_t  *)net_alloc_buffer((size_t)num_workers * (chunkSize + max_plen));
    waddrs   = (uint64_t *)net_alloc_buffer((size_t)num_workers * total_slots * 8);
    wcounts  = (uint32_t *)net_alloc_buffer((size_t)num_workers * pat_count * sizeof(uint32_t));
    wdone    = (uint8_t  *)net_alloc_buffer((size_t)num_workers * pat_count);
    if (!readbufs || !waddrs || !wcounts || !wdone) goto aob_fail;

    net_send_int32(fd, CMD_SUCCESS);
    memset(output, 0, (size_t)pat_count * 8);

    {
        struct aob_ps4_fill fillctx;
        struct aob_worker workers[AOB_SCAN_THREADS];
        ScePthread tids[AOB_SCAN_THREADS];
        bool spawned[AOB_SCAN_THREADS];
        const uint64_t *ca[AOB_SCAN_THREADS];
        const uint32_t *cc[AOB_SCAN_THREADS];

        fillctx.pid = mp->pid;
        fillctx.base = mp->address;

        for (uint32_t w = 0; w < num_workers; w++) {
            uint64_t s = (uint64_t)w * span;
            uint64_t e = s + span;
            if (s > mp->length) s = mp->length;
            if (e > mp->length) e = mp->length;
            workers[w].pats = pats;
            workers[w].pat_count = pat_count;
            workers[w].disp = &disp;
            workers[w].base_addr = mp->address;
            workers[w].length = mp->length;
            workers[w].max_plen = max_plen;
            workers[w].chunkSize = chunkSize;
            workers[w].fill = aob_ps4_fill_fn;
            workers[w].fill_ctx = &fillctx;
            workers[w].slot_off = slot_off;
            workers[w].start = s;
            workers[w].end = e;
            workers[w].readBuf = readbufs + (size_t)w * (chunkSize + max_plen);
            workers[w].addrs = waddrs + (size_t)w * total_slots;
            workers[w].counts = wcounts + (size_t)w * pat_count;
            workers[w].done = wdone + (size_t)w * pat_count;
            spawned[w] = false;
        }

        for (uint32_t w = 1; w < num_workers; w++) {
            if (scePthreadCreate(&tids[w], NULL, aob_thread, &workers[w], "aobscan") == 0) {
                spawned[w] = true;
            }
        }
        aob_scan_range(&workers[0]);
        for (uint32_t w = 1; w < num_workers; w++) {
            if (spawned[w]) scePthreadJoin(tids[w], NULL);
            else aob_scan_range(&workers[w]);
        }

        for (uint32_t w = 0; w < num_workers; w++) {
            ca[w] = waddrs + (size_t)w * total_slots;
            cc[w] = wcounts + (size_t)w * pat_count;
        }
        aob_merge(pats, pat_count, stop_unique, slot_off, num_workers, ca, cc, output);
    }

    net_send_all(fd, output, (int)((size_t)pat_count * 8));
    if (wdone) free(wdone);
    if (wcounts) free(wcounts);
    if (waddrs) free(waddrs);
    if (readbufs) free(readbufs);
    free(output);
    free(slot_off);
    free(widx);
    free(bidx);
    free(pats);
    free(blob);
    net_send_int32(fd, CMD_SUCCESS);
    return 0;

aob_fail:
    if (wdone) free(wdone);
    if (wcounts) free(wcounts);
    if (waddrs) free(waddrs);
    if (readbufs) free(readbufs);
    if (output) free(output);
    if (slot_off) free(slot_off);
    if (widx) free(widx);
    if (bidx) free(bidx);
    if (pats) free(pats);
    free(blob);
    net_send_int32(fd, CMD_DATA_NULL);
    return 1;
}

uint32_t g_proc_auth_bits = 0;

static struct { uint32_t s1, s2, s3, s4; } g_auth_lfsr;

static uint32_t auth_lfsr_next(void) {
    g_auth_lfsr.s1 = ((g_auth_lfsr.s1 << 18) & 0xFFF80000u) ^ ((g_auth_lfsr.s1 ^ (g_auth_lfsr.s1 << 6))  >> 13);
    g_auth_lfsr.s2 = ((g_auth_lfsr.s2 <<  2) & 0xFFFFFFE0u) ^ ((g_auth_lfsr.s2 ^ (g_auth_lfsr.s2 << 2))  >> 27);
    g_auth_lfsr.s3 = ((g_auth_lfsr.s3 <<  7) & 0xFFFFF800u) ^ ((g_auth_lfsr.s3 ^ (g_auth_lfsr.s3 << 13)) >> 21);
    g_auth_lfsr.s4 = ((g_auth_lfsr.s4 << 13) & 0xFFF00000u) ^ ((g_auth_lfsr.s4 ^ (g_auth_lfsr.s4 << 3))  >> 12);
    return g_auth_lfsr.s1 ^ g_auth_lfsr.s2 ^ g_auth_lfsr.s3 ^ g_auth_lfsr.s4;
}

static void proc_auth_xor_keystream(const uint8_t *in, uint8_t *out, uint16_t len) {
    g_auth_lfsr.s1 = 200;
    g_auth_lfsr.s2 = 300;
    g_auth_lfsr.s3 = 400;
    g_auth_lfsr.s4 = 500;
    for (uint16_t i = 0; i < len; i++) {
        out[i] = in[i] ^ (uint8_t)auth_lfsr_next();
    }
}

int proc_auth_handle(int fd, struct cmd_packet *packet) {
    struct cmd_proc_auth_packet *ap = (struct cmd_proc_auth_packet *)packet->data;
    if (!ap || ap->magic != CMD_PROC_AUTH_MAGIC) {
        net_send_int32(fd, CMD_DATA_NULL);
        return 1;
    }

    net_send_int32(fd, CMD_SUCCESS);

    uint16_t chlen = 64;
    uint8_t challenge[64];
    uint8_t expected[64];
    uint8_t response[64];

    for (uint16_t i = 0; i < chlen; i++) {
        int r = rand();
        challenge[i] = (uint8_t)(r - r / -255);
    }

    proc_auth_xor_keystream(challenge, expected, chlen);

    net_send_all(fd, &chlen, sizeof(chlen));
    net_send_all(fd, challenge, chlen);
    net_recv_all(fd, response, chlen, 1);

    for (uint16_t i = 0; i < chlen; i++) {
        if (response[i] != expected[i]) {
            net_send_int32(fd, CMD_DATA_NULL);
            return 1;
        }
    }

    if (ap->flags & 1) g_proc_auth_bits |= 1;
    if (ap->flags & 2) g_proc_auth_bits |= 2;

    net_send_int32(fd, CMD_SUCCESS);
    return 0;
}

extern uint32_t g_proc_auth_bits;
#define SCAN_REQUIRE_AUTH(fd) \
    do { if (!(g_proc_auth_bits & 2)) { net_send_int32((fd), CMD_DATA_NULL); return 1; } } while (0)

#define SCAN_READ_CHUNK    0x8000
#define SCAN_RESULT_CHUNK  0x10000
#define SCAN_IO_CHUNK      0x10000

static const uint8_t g_cmptype_needs_value[16] = {
    1, 1, 1, 1, 1, 0, 1, 0, 1, 0, 0, 0, 1, 0, 0, 0
};
static const uint8_t g_cmptype_needs_extra[16] = {
    0, 0, 0, 0, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0
};
static const uint8_t g_cmptype_needs_previous[16] = {
    0, 0, 0, 0, 0, 1, 1, 1, 1, 1, 1, 0, 0, 0, 0, 0
};

int proc_scan_start_handle(int fd, struct cmd_packet *packet) {
    SCAN_REQUIRE_AUTH(fd);
    struct cmd_proc_scan_start_packet *sp = (struct cmd_proc_scan_start_packet *)packet->data;
    if (!sp) {
        net_send_int32(fd, CMD_DATA_NULL);
        return 1;
    }

    size_t valueLength = proc_scan_getSizeOfValueType(sp->valueType);
    if (!valueLength) valueLength = sp->lenData;
    if (!valueLength) {
        net_send_int32(fd, CMD_DATA_NULL);
        return 1;
    }

    size_t step = sp->alignment ? sp->alignment : valueLength;

    bool needs_scan_value = ((1u << sp->compareType) & 0x114Fu) != 0;
    bool is_between       = sp->compareType == cmpTypeValueBetween;
    bool is_arrbytes      = sp->valueType == valTypeArrBytes;
    if (is_between) needs_scan_value = true;

    unsigned char *pattern = NULL;
    unsigned char *mask = NULL;
    if (needs_scan_value && sp->lenData) {
        pattern = (unsigned char *)net_alloc_buffer(sp->lenData);
        if (!pattern) {
            net_send_int32(fd, CMD_DATA_NULL);
            return 1;
        }
    }
    if (is_arrbytes) {
        mask = (unsigned char *)net_alloc_buffer(valueLength);
        if (!mask) {
            if (pattern) free(pattern);
            net_send_int32(fd, CMD_DATA_NULL);
            return 1;
        }
    }

    net_send_int32(fd, CMD_SUCCESS);

    if (pattern) {
        net_recv_all(fd, pattern, sp->lenData, 1);
    }
    if (mask) {
        net_recv_all(fd, mask, valueLength, 1);
    }

    unsigned char *pExtraValue = (is_between && pattern) ? pattern + valueLength : NULL;

    size_t chunkSize = SCAN_READ_CHUNK;
    if (SCAN_READ_CHUNK % valueLength) {
        chunkSize = valueLength * (SCAN_READ_CHUNK / valueLength);
    }

    unsigned char *readBuf = (unsigned char *)net_alloc_buffer(chunkSize);
    unsigned char *resultBuf = (unsigned char *)net_alloc_buffer(SCAN_RESULT_CHUNK);
    if (!readBuf || !resultBuf) {
        if (readBuf) free(readBuf);
        if (resultBuf) free(resultBuf);
        if (pattern) free(pattern);
        if (mask)    free(mask);
        net_send_int32(fd, CMD_DATA_NULL);
        return 1;
    }

    net_send_int32(fd, CMD_SUCCESS);

    uint64_t addr = sp->address;
    uint32_t remaining = sp->length;
    uint64_t resultLen = 0;

    uint64_t flushThresh = 0xFFE8 - valueLength;

    while (remaining > 0) {
        uint32_t toRead = remaining > chunkSize ? (uint32_t)chunkSize : remaining;
        memset(readBuf, 0, toRead);
        sys_proc_rw(sp->pid, addr, readBuf, toRead, 0);

        uint64_t limit = toRead >= valueLength ? toRead - valueLength : 0;
        for (uint64_t j = 0; j <= limit; j += step) {
            if (proc_scan_compareValues(sp->compareType, sp->valueType, valueLength,
                                         pattern, readBuf + j, pExtraValue, mask)) {
                if (resultLen > flushThresh) {
                    memcpy(resultBuf, &resultLen, 8);
                    net_send_all(fd, resultBuf, (int)(resultLen + 8));
                    resultLen = 0;
                }
                uint32_t offset = (uint32_t)((addr + j) - sp->address);
                memcpy(resultBuf + 8 + resultLen, &offset, 4);
                memcpy(resultBuf + 8 + resultLen + 4, readBuf + j, valueLength);
                resultLen += 4 + valueLength;
            }
        }

        uint32_t advance = toRead + (uint32_t)step - (uint32_t)valueLength;
        if (advance == 0 || advance > toRead) advance = toRead;
        addr += advance;
        remaining = remaining > advance ? remaining - advance : 0;
    }

    if (resultLen) {
        memcpy(resultBuf, &resultLen, 8);
        net_send_all(fd, resultBuf, (int)(resultLen + 8));
    }

    uint64_t sentinel = 0xFFFFFFFFFFFFFFFFULL;
    net_send_all(fd, &sentinel, 8);

    free(readBuf);
    free(resultBuf);
    if (pattern) free(pattern);
    if (mask)    free(mask);

    net_send_int32(fd, CMD_SUCCESS);
    return 0;
}

int proc_scan_count_handle(int fd, struct cmd_packet *packet) {
    SCAN_REQUIRE_AUTH(fd);
    struct cmd_proc_scan_count_packet *cp = (struct cmd_proc_scan_count_packet *)packet->data;
    if (!cp) {
        net_send_int32(fd, CMD_DATA_NULL);
        return 1;
    }

    size_t valueLength = proc_scan_getSizeOfValueType(cp->valueType);
    if (!valueLength) valueLength = cp->lenData;
    if (!valueLength || valueLength > 0x1000) {
        net_send_int32(fd, CMD_DATA_NULL);
        return 1;
    }

    bool needs_value_flag    = false;
    bool needs_extra_flag    = false;
    bool needs_previous_flag = false;
    if ((uint8_t)cp->compareType <= 12) {
        needs_value_flag    = g_cmptype_needs_value   [(uint8_t)cp->compareType] != 0;
        needs_extra_flag    = g_cmptype_needs_extra   [(uint8_t)cp->compareType] != 0;
        needs_previous_flag = g_cmptype_needs_previous[(uint8_t)cp->compareType] != 0;
    }

    bool needs_scan_value = needs_value_flag || needs_extra_flag;
    bool is_arrbytes = cp->valueType == valTypeArrBytes;

    unsigned char *pattern = NULL;
    unsigned char *mask = NULL;
    if (needs_scan_value && cp->lenData) {
        pattern = (unsigned char *)net_alloc_buffer(cp->lenData);
        if (!pattern) {
            net_send_int32(fd, CMD_DATA_NULL);
            return 1;
        }
    }
    if (is_arrbytes) {
        mask = (unsigned char *)net_alloc_buffer(valueLength);
        if (!mask) {
            if (pattern) free(pattern);
            net_send_int32(fd, CMD_DATA_NULL);
            return 1;
        }
    }

    unsigned char *chunkBuf = (unsigned char *)net_alloc_buffer(0x20000);
    unsigned char *resultBuf = (unsigned char *)net_alloc_buffer(SCAN_READ_CHUNK);
    unsigned char *valueNow = (unsigned char *)net_alloc_buffer(valueLength);

    unsigned char *memWindow = (unsigned char *)net_alloc_buffer(0x8000);
    if (!chunkBuf || !resultBuf || !valueNow || !memWindow) {
        if (chunkBuf) free(chunkBuf);
        if (resultBuf) free(resultBuf);
        if (valueNow) free(valueNow);
        if (memWindow) free(memWindow);
        if (pattern) free(pattern);
        if (mask)    free(mask);
        net_send_int32(fd, CMD_DATA_NULL);
        return 1;
    }

    net_send_int32(fd, CMD_SUCCESS);

    if (pattern) {
        net_recv_all(fd, pattern, cp->lenData, 1);
    }
    if (mask) {
        net_recv_all(fd, mask, valueLength, 1);
    }

    unsigned char *pExtraValue = (needs_extra_flag && pattern) ? pattern + valueLength : NULL;

    bool includesPrevValue = needs_previous_flag;
    size_t entrySize = includesPrevValue ? (4 + valueLength) : 4;

    uint64_t flushThresh = 0x7FE8 - 2 * valueLength;

    while (1) {
        uint32_t chunkLen = 0;
        net_recv_all(fd, &chunkLen, 4, 1);
        if (chunkLen == 0xFFFFFFFF) break;
        if (chunkLen == 0) {

            uint64_t sentinel = 0xFFFFFFFFFFFFFFFFULL;
            net_send_all(fd, &sentinel, 8);
            continue;
        }
        if (chunkLen > 0x20000) break;

        net_recv_all(fd, chunkBuf, chunkLen, 1);

        uint64_t lastEntryAddr = 0;
        if (chunkLen >= entrySize) {
            uint32_t lastOffset;
            memcpy(&lastOffset, chunkBuf + chunkLen - entrySize, 4);
            lastEntryAddr = cp->base_address + lastOffset;
        }

        uint64_t windowStart = 0;
        uint64_t windowEnd = 0;
        bool firstBatch = true;

        uint64_t resultLen = 0;
        for (size_t off = 0; off + entrySize <= chunkLen; off += entrySize) {
            uint32_t entryOffset;
            memcpy(&entryOffset, chunkBuf + off, 4);
            unsigned char *prevValuePtr = includesPrevValue ? (chunkBuf + off + 4) : pExtraValue;

            uint64_t addr = cp->base_address + entryOffset;

            if (addr >= windowEnd) {

                uint32_t readSize;
                if (firstBatch) {
                    readSize = 0x8000;
                    firstBatch = false;
                } else {
                    uint64_t remaining = (lastEntryAddr > addr) ? (lastEntryAddr - addr) : 0;
                    if (remaining == 0) {
                        readSize = (uint32_t)valueLength;
                    } else if (remaining > 0x7FFF) {
                        readSize = 0x8000;
                    } else {
                        readSize = (uint32_t)remaining;
                    }
                }
                memset(memWindow, 0, readSize);
                sys_proc_rw(cp->pid, addr, memWindow, readSize, 0);
                windowStart = addr;
                windowEnd = addr + readSize;
            }

            unsigned char *memValuePtr = memWindow + (addr - windowStart);

            if (proc_scan_compareValues(cp->compareType, cp->valueType, valueLength,
                                         pattern, memValuePtr, prevValuePtr, mask)) {
                if (resultLen > flushThresh) {
                    memcpy(resultBuf, &resultLen, 8);
                    net_send_all(fd, resultBuf, (int)(resultLen + 8));
                    resultLen = 0;
                }
                memcpy(resultBuf + 8 + resultLen, &entryOffset, 4);
                memcpy(resultBuf + 8 + resultLen + 4, memValuePtr, valueLength);
                resultLen += 4 + valueLength;
            }
        }

        if (resultLen) {
            memcpy(resultBuf, &resultLen, 8);
            net_send_all(fd, resultBuf, (int)(resultLen + 8));
        }

        uint64_t sentinel = 0xFFFFFFFFFFFFFFFFULL;
        net_send_all(fd, &sentinel, 8);
    }

    free(chunkBuf);
    free(resultBuf);
    free(valueNow);
    free(memWindow);
    if (pattern) free(pattern);
    if (mask)    free(mask);

    net_send_int32(fd, CMD_SUCCESS);
    return 0;
}

int proc_scan_get_handle(int fd, struct cmd_packet *packet) {
    SCAN_REQUIRE_AUTH(fd);
    struct cmd_proc_scan_get_packet *gp = (struct cmd_proc_scan_get_packet *)packet->data;
    if (!gp || gp->count == 0) {
        net_send_int32(fd, CMD_DATA_NULL);
        return 1;
    }

    size_t entriesLen = (size_t)gp->count * 12;
    unsigned char *entries = (unsigned char *)net_alloc_buffer(entriesLen);
    unsigned char *buf = (unsigned char *)net_alloc_buffer(SCAN_IO_CHUNK);
    if (!entries || !buf) {
        if (entries) free(entries);
        if (buf) free(buf);
        net_send_int32(fd, CMD_DATA_NULL);
        return 1;
    }

    net_send_int32(fd, CMD_SUCCESS);
    net_recv_all(fd, entries, (int)entriesLen, 1);

    for (uint32_t i = 0; i < gp->count; i++) {
        uint64_t addr;
        uint32_t length;
        memcpy(&addr, entries + 12 * i, 8);
        memcpy(&length, entries + 12 * i + 8, 4);

        while (length > 0) {
            uint32_t toRead = length > SCAN_IO_CHUNK ? SCAN_IO_CHUNK : length;
            memset(buf, 0, toRead);
            sys_proc_rw(gp->pid, addr, buf, toRead, 0);
            net_send_all(fd, buf, (int)toRead);
            addr += toRead;
            length -= toRead;
        }
    }

    uint64_t sentinel = 0xFFFFFFFFFFFFFFFFULL;
    net_send_all(fd, &sentinel, 8);

    free(buf);
    free(entries);
    return 0;
}

int proc_ptwalk_test_handle(int fd, struct cmd_packet *packet) {
    struct ptw_test_req { uint32_t pid; uint64_t va; } __attribute__((packed));
    struct ptw_test_req *r = (struct ptw_test_req *)packet->data;
    if (!r) { net_send_int32(fd, CMD_DATA_NULL); return 1; }

    uint64_t out[12];
    memset(out, 0, sizeof(out));
    out[0] = (uint64_t)(int64_t)ptw_discover();
    out[1] = proc_ptwalk_dmap_base();

    uint64_t k = 0, v = 0; int lvl = -1;
    out[2] = (uint64_t)(int64_t)proc_ptwalk_leaf_addr(r->pid, r->va, &k, &v, &lvl);
    out[3] = k;
    out[4] = v;
    out[5] = (uint64_t)(int64_t)lvl;

    int huge = 0; uint64_t pb = 0, lk = 0, pte = 0;
    out[6] = (uint64_t)(int64_t)proc_ptwalk_span_resolve(r->pid, r->va & ~0x1FFFFFULL,
                                                         &huge, &pb, &lk, &pte);
    out[7]  = (uint64_t)huge;
    out[8]  = pb;
    out[9]  = lk;
    out[10] = pte;
    out[11] = r->va;

    net_send_int32(fd, CMD_SUCCESS);
    net_send_all(fd, out, sizeof(out));
    return 0;
}

int proc_alias_test_handle(int fd, struct cmd_packet *packet) {
    struct alias_test_req { uint32_t pid; uint64_t addr; uint32_t len; } __attribute__((packed));
    struct alias_test_req *r = (struct alias_test_req *)packet->data;
    if (!r) { net_send_int32(fd, CMD_DATA_NULL); return 1; }

    uint32_t len = r->len;
    if (len == 0 || len > 0x4000) len = 0x1000;

    uint64_t out[12];
    memset(out, 0, sizeof(out));
    out[0] = (uint64_t)(int64_t)ptw_discover();
    out[1] = proc_ptwalk_dmap_base();

    long mret = syscall(477, 0L, 0x200000L, 3L, 0x1002L, -1L, 0L);
    out[2] = (uint64_t)mret;
    if (mret != -1 && mret != 0) syscall(73, mret, 0x200000L);

    scan_alias_ctx *ctx = scan_alias_begin(r->pid, 0);
    if (ctx) {
        out[3] = 1;
        uint64_t mlen = 0;
        const void *p = scan_alias_map(ctx, r->addr, len, &mlen);
        if (p) {
            out[4] = 1;
            uint8_t *am = (uint8_t *)malloc(len);
            uint8_t *mm = (uint8_t *)malloc(len);
            if (am && mm) {
                memcpy(am, p, len);
                sys_proc_rw(r->pid, r->addr, mm, len, 0);
                uint64_t off = 0; int match = 1;
                for (uint64_t i = 0; i < len; i++)
                    if (am[i] != mm[i]) { match = 0; off = i; break; }
                out[5] = (uint64_t)match;
                out[6] = off;
                out[7] = len;
                memcpy(&out[8], am, 8);
                memcpy(&out[9], mm, 8);
            }
            if (am) free(am);
            if (mm) free(mm);
            scan_alias_release(ctx);
        }
        scan_alias_end(ctx);
    }

    net_send_int32(fd, CMD_SUCCESS);
    net_send_all(fd, out, sizeof(out));
    return 0;
}

int proc_handle(int fd, struct cmd_packet *packet, unsigned char client_idx) {
    switch(packet->cmd) {
        case CMD_PROC_LIST:           return proc_list_handle(fd, packet);
        case CMD_PROC_READ:           return proc_read_handle(fd, packet);
        case CMD_PROC_READ_STACK:     return proc_read_stack_handle(fd, packet);
        case CMD_PROC_WRITE:          return proc_write_handle(fd, packet);
        case 0xBDAACC04u:             return proc_write_multi_handle(fd, packet);
        case 0xBDAACC10u:             return proc_turboscan_caps_handle(fd, packet);
        case 0xBDAACC11u:             return proc_turboscan_start_handle(fd, packet, client_idx);
        case 0xBDAACC12u:             return proc_turboscan_count_handle(fd, packet, client_idx);
        case 0xBDAACC13u:             return proc_turboscan_get_handle(fd, packet, client_idx);
        case 0xBDAACC14u:             return proc_turboscan_end_handle(fd, packet, client_idx);
        case 0xBDAACC15u:             return proc_turboscan_config_handle(fd, packet);
        case 0xBDAACC16u:             return proc_turboscan_regions_handle(fd, packet);
        case 0xBDAACC17u:             return proc_turboscan_cancel_handle(fd, packet);
        case 0xBDAACC30u:             return proc_ptwalk_test_handle(fd, packet);
        case 0xBDAACC31u:             return proc_alias_test_handle(fd, packet);
        case 0xBDAACC32u:             return proc_turboscan_fileprobe_handle(fd, packet);
        case CMD_PROC_MAPS:           return proc_maps_handle(fd, packet);
        case CMD_PROC_INTALL:         return proc_install_handle(fd, packet);
        case CMD_PROC_CALL:           return proc_call_handle(fd, packet);
        case CMD_PROC_ELF:            return proc_elf_handle(fd, packet);
        case CMD_PROC_ELF_RPC:        return proc_elf_rpc_handle(fd, packet);
        case CMD_PROC_DISASM_REGION:       return proc_disasm_region_handle(fd, packet);
        case CMD_PROC_EXTRACT_CODE_XREFS:  return proc_extract_code_xrefs_handle(fd, packet);
        case CMD_PROC_FIND_XREFS_TO:       return proc_find_xrefs_to_handle(fd, packet);
        case CMD_PROC_PROTECT:        return proc_protect_handle(fd, packet);
        case CMD_PROC_SCAN:           return proc_scan_handle(fd, packet);
        case CMD_PROC_INFO:           return proc_info_handle(fd, packet);
        case CMD_PROC_ALLOC:          return proc_alloc_handle(fd, packet);
        case CMD_PROC_FREE:           return proc_free_handle(fd, packet);
        case CMD_PROC_ALLOC_HINTED:   return proc_alloc_hinted_handle(fd, packet);
        case CMD_PROC_SCAN_AOB:       return proc_scan_aob_handle(fd, packet);
        case CMD_PROC_SCAN_AOB_MULTI: return proc_scan_aob_multi_handle(fd, packet);
        case CMD_PROC_AUTH:           return proc_auth_handle(fd, packet);
        case CMD_PROC_SCAN_START:     return proc_scan_start_handle(fd, packet);
        case CMD_PROC_SCAN_COUNT:     return proc_scan_count_handle(fd, packet);
        case CMD_PROC_SCAN_GET:       return proc_scan_get_handle(fd, packet);
        case 0xBDAA0024u:             return proc_assemble_handle(fd, packet);
    }

    net_send_int32(fd, CMD_ERROR);
    return 1;
}
