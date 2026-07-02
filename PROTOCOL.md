# ps4debug-NG - Wire Protocol & Kernel Syscall Reference

Developer-facing reference for every command and syscall exposed by the debugger
payload. All field offsets, sizes, opcodes, and behaviors below are taken directly
from the sources under `debugger/` and `kdebugger/` in this tree, with file:line
citations. Nothing is summarized from external documentation.

---

## 1. Wire protocol fundamentals

### 1.1 Core constants

| Symbol              | Value                                | Source                     |
|---------------------|--------------------------------------|----------------------------|
| `PACKET_VERSION`    | `"1.3"`                              | protocol.h:12              |
| `PACKET_BRANDING`   | `"ps4debug-NG by OSR v1.3.0\01.0"`   | protocol.h:13 (built from `version.h`; + capability level, see 2.1) |
| `PACKET_MAGIC`      | `0xFFAABBCC`                         | protocol.h:14              |
| `BROADCAST_MAGIC`   | `0xFFFFAAAA`                         | server.h:19                |

### 1.2 Ports

| Port | Protocol | Role                                          | Source            |
|------|----------|-----------------------------------------------|-------------------|
| 744  | TCP      | Main command server                           | server.h:15       |
| 755  | TCP      | Asynchronous debug-interrupt channel (server→client) | debug.h:137 |
| 1010 | UDP      | Discovery broadcast                           | server.h:18       |

### 1.3 Byte order

All multi-byte integers are **little-endian** (native x86-64 layout). `__attribute__((packed))`
is used on every wire struct, so there is no implicit padding between fields.

### 1.4 Packet framing

Every request on port 744 begins with a fixed 12-byte header. `void *data` in the
C struct is NOT on the wire - it is a client-side pointer filled in by the server
after the variable-length body is received (protocol.h:104-110, server.c:167-185):

```
struct cmd_packet {        // 12 bytes on the wire
    uint32_t magic;        // must equal PACKET_MAGIC (0xFFAABBCC)
    uint32_t cmd;          // command ID (see §4)
    uint32_t datalen;      // length of request body that follows the header
    void    *data;         // NOT transmitted; server-side pointer only
};
#define CMD_PACKET_SIZE 12
```

Framing loop (server.c:102-203):

1. Server blocking-reads 12 bytes → `cmd_packet` header.
2. Validates `magic == PACKET_MAGIC`; on mismatch the packet is silently dropped
   and the loop continues (no error response).
3. If `datalen > 0`, server allocates a buffer via `net_alloc_buffer(datalen)`
   and reads exactly `datalen` bytes into it.
4. Dispatches to the appropriate handler; the handler reads any *additional*
   trailing data (beyond the fixed request struct) directly from the socket.
5. Handler replies with `CMD_SUCCESS` (or an error code from §1.6) via
   `net_send_int32`, optionally followed by response payload.

### 1.5 Dispatch routing (server.c:43-85)

`cmd_handler` inlines five infrequent commands, then routes everything else by
the middle byte of the command ID:

| Namespace      | Match (`(cmd >> 16) & 0xFF`) | Dispatch target                     | File          |
|----------------|------------------------------|-------------------------------------|---------------|
| Info/ping      | n/a (inlined)                | `cmd_handler` itself                | server.c:56   |
| Process        | `0xAA`                       | `proc_handle`                       | proc.c:2505   |
| Debug          | `0xBB`                       | `debug_handle`                      | debug.c:950   |
| Kernel R/W     | `0xCC`                       | `kern_handle`                       | kern.c:57     |
| Console        | `0xDD`                       | `console_handle`                    | console.c:205 |

`VALID_CMD(cmd)` additionally requires the top byte to be `0xBD` (protocol.h:88).

**Special case** (server.c:188-192): if `cmd == CMD_DEBUG_ATTACH`, the
per-connection handler `debug_attach_handle_svc` is invoked directly with the
`server_client *` (so it can attach debug state to the specific connection).
Although `debug_handle` also lists `CMD_DEBUG_ATTACH → debug_attach_handle` in
its switch (kern.c:784), that branch is unreachable in normal flow.

### 1.6 Status codes (protocol.h:95-102)

| Code                  | Value        | Meaning                                           |
|-----------------------|--------------|---------------------------------------------------|
| `CMD_SUCCESS`         | `0x80000000` | Command succeeded; response payload may follow.   |
| `CMD_ERROR`           | `0xF0000001` | Generic failure.                                  |
| `CMD_TOO_MUCH_DATA`   | `0xF0000002` | Client requested more than the server will send.  |
| `CMD_DATA_NULL`       | `0xF0000003` | NULL/missing packet body or allocation failure.   |
| `CMD_ALREADY_DEBUG`   | `0xF0000004` | A debug session is already active.                |
| `CMD_INVALID_INDEX`   | `0xF0000005` | Breakpoint/watchpoint index out of range.         |
| `CMD_FATAL_STATUS(s)` | macro        | `((s >> 28) == 15)` - any `0xFxxxxxxx` is fatal.  |

### 1.7 Discovery (server.c:235-306)

A UDP server on port 1010 accepts a single 4-byte datagram. If the payload equals
`BROADCAST_MAGIC` (`0xFFFFAAAA`), the server echoes the same 4 bytes back to the
sender. No other payload is accepted; there is no authentication on this channel.

### 1.8 Authentication (proc.c:1862-1900)

A single connection-scoped flag word `g_proc_auth_bits` gates the scan family.
Bit 1 (`0x02`) is the *scan-enabled* bit; all `CMD_PROC_SCAN*` handlers check
it on entry (proc.c:1904-1905, 1691, 1860, etc.).

`CMD_PROC_AUTH` protocol:

1. Client sends a `struct cmd_proc_auth_packet`:
   - `uint32_t magic` - must equal `CMD_PROC_AUTH_MAGIC` (`0xBB40E64D`).
   - `uint32_t flags` - requested bits to OR into `g_proc_auth_bits` on success.
2. Server generates a 64-byte challenge and sends: `uint16_t challenge_length = 64`
   followed by the 64 bytes of challenge.
3. Client XORs the challenge against a keystream (derived client-side; mechanism
   intentionally undocumented) and sends 64 response bytes back.
4. On match, server OR-s the requested flags into `g_proc_auth_bits` and replies
   `CMD_SUCCESS`; otherwise `CMD_DATA_NULL`.

State is per-connection: disconnecting resets it. There is *no* server-wide
authentication on other command families - reads/writes/debug/kernel RW are
unauthenticated.

### 1.9 Asynchronous interrupt channel (port 755)

When a client issues `CMD_DEBUG_ATTACH`, the server also opens a TCP connection
*outbound* to that client's IP on port 755 (debug.c:25). The client must be
listening on 755 before sending `CMD_DEBUG_ATTACH`.

Interrupts (hit breakpoints/watchpoints, single-step completions) are delivered
asynchronously as fixed-size packets of `DEBUG_INTERRUPT_PACKET_SIZE = 0x4A0`
bytes (1184) on this channel (debug.h:105-113):

```
struct debug_interrupt_packet {   // 0x4A0 bytes
    uint32_t            lwpid;
    uint32_t            status;
    char                tdname[40];
    struct __reg64      reg64;      // GP registers; 184 bytes
    struct savefpu_ymm  savefpu;    // FPU+YMM; 512 bytes
    struct __dbreg64    dbreg64;    // 16× uint64_t = 128 bytes
};
```

Note: the struct is `packed`, so `savefpu` is not cache-line aligned - the
compiler emits warnings for this (observed during build) but the layout is
stable.

---

## 2. Command reference

68 commands reach a handler in a release build. Not every one has a `CMD_*` macro:
the newer additions (bulk write `0xBDAACC04`, Turbo Scan `0xBDAACC10-0xBDAACC17`,
assemble `0xBDAA0024`, FS/GS-base `0xBDBB000E/0xBDBB000F`, foreground-app
`0xBDDD0006`) are dispatched as **raw hex literals** with no macro, because some
clients enumerate the published `CMD_*` set and depend on its count (see
`protocol.h:305-311`). Three dev-only diagnostics (`0xBDAACC30/31/32`) exist in
development builds but are **stripped from release payloads** and are not listed here.

### 2.1 Info & ping (`0xBD000xxx`, `0xBDAACC06`)

All five are handled inline in `cmd_handler` (server.c:52-71).

#### `CMD_VERSION = 0xBD000001`
- **Request body:** none.
- **Response:** `uint32_t length`, then `length` bytes of `PACKET_VERSION` ("1.3").

#### `CMD_FW_VERSION = 0xBD000500`
- **Request body:** none.
- **Response:** `uint16_t` firmware version (BCD, e.g. `0x505` = 5.05).

#### `CMD_BRANDING = 0xBD000501`
- **Request body:** none.
- **Response:** `uint32_t length`, then `length` bytes: the human branding string
  (`PACKET_BRANDING`, e.g. `"ps4debug-NG by OSR v1.3.0"`), a single `NUL`, then a
  **capability level** string (`"1.0"`), with no trailing NUL.
- **Capability level:** C-string clients read up to the first `NUL` and see only
  the unchanged brand; capability-aware clients read past the `NUL` to get the
  level and gate features on it (e.g. `0xBDAACC04` bulk write). Built from
  `PACKET_BRANDING "\0" "1.0"` in `server.c` - the literals are split so `\0` is a
  NUL byte, not the octal escape `\01`. Bump the level as new capabilities land.

#### `CMD_PLATFORM_ID = 0xBD000502`
- **Request body:** none.
- **Response:** `uint16_t` platform tag - hardcoded `4` (PS4). Clients display this
  as the console version (e.g. logged as "Console PS4"). Was previously named
  `CMD_PROTOCOL_ID`; renamed to disambiguate from the wire-protocol version
  reported by `CMD_VERSION` (which is the server protocol revision, currently `1.3`).

#### `CMD_PROC_NOP = 0xBDAACC06`
- **Request body:** none.
- **Response:** `CMD_SUCCESS`. Keepalive / liveness probe.

---

### 2.2 Process commands (`0xBDAAxxxx`) - dispatched by `proc_handle` (proc.c:2268-2295)

#### `CMD_PROC_LIST = 0xBDAA0001` (proc.c:330-358)
- **Request body:** none.
- **Response:** `CMD_SUCCESS`, `uint32_t num`, then `num × struct proc_list_entry`:
  ```
  struct proc_list_entry { char p_comm[32]; int pid; };  // 36 bytes each
  ```
- **Kernel path:** `sys_proc_list` (kdbg.h:17).

#### `CMD_PROC_READ = 0xBDAA0002` (proc.c:360-397)
- **Request body:** `struct cmd_proc_read_packet` (16 bytes).
- **Response:** `CMD_SUCCESS`, then up to `length` bytes in 64 KiB chunks.
- **Kernel path:** `sys_proc_rw(..., write=0)`.

#### `CMD_PROC_WRITE = 0xBDAA0003` (proc.c:399-432)
- **Request body:** `struct cmd_proc_write_packet` (16 bytes).
- **Trailing data:** `length` bytes to write.
- **Response:** `CMD_SUCCESS`.
- **Kernel path:** `sys_proc_rw(..., write=1)`.

#### `0xBDAACC04` (`proc_write_multi_handle`)
Bulk write - the write counterpart to `CMD_PROC_SCAN_GET` (bulk read). Collapses a
freeze / multi-poke loop of N single `CMD_PROC_WRITE`s into one exchange.
Dispatched raw (no name macro); **not** auth-gated (mirrors the single write).
- **Request body:** `struct cmd_proc_write_multi_packet` (12 bytes,
  `{ uint32_t pid; uint32_t count; uint32_t flags; }`). `flags` bit 0
  (`PROC_WRITE_MULTI_F_STATUS`) requests a per-entry status array in the response.
- **Trailing data:** after the server's first `CMD_SUCCESS`, the client streams
  `count` entries, each `{ uint64_t address; uint32_t length; <length> bytes }`
  concatenated. Per-entry `length` capped at `0x100000`; `count` at `0xFFFF`.
- **Response:** `CMD_SUCCESS` (ack), the server applies each entry via
  `sys_proc_rw(..., write=1)` in 64 KiB chunks, then - if `flags` bit 0 was set -
  a `count`-byte status array (one byte per entry, `0` = ok, `1` = write failed),
  then a trailing `CMD_SUCCESS`. **Two** status words, plus the optional array
  between them.
- **Semantics:** best-effort and non-atomic - a failed entry does not stop the
  rest. A `count`/`length` cap violation makes the server reply `CMD_ERROR` and
  abort the command. Status words are sent raw (`CMD_SUCCESS = 0x80000000`).

#### `CMD_PROC_MAPS = 0xBDAA0004` (proc.c:434-477)
- **Request body:** `struct cmd_proc_maps_packet` (4 bytes, just `pid`).
- **Response:** `CMD_SUCCESS`, `uint32_t num`, then `num × struct proc_vm_map_entry`
  (`{uint64_t start; uint64_t end; uint64_t offset; uint64_t prot; char name[32];}` = 64 bytes each).
- **Kernel path:** `SYS_PROC_VM_MAP`.

#### `CMD_PROC_INTALL = 0xBDAA0005` (proc.c:479-505)
Note the literal spelling `INTALL` (typo in source, preserved for compat).
- **Request body:** `struct cmd_proc_install_packet` (4 bytes, just `pid`).
- **Response:** `CMD_SUCCESS`, `struct cmd_proc_install_response` (`uint64_t rpcstub`, 8 bytes).
- **Effect:** injects the RPC stub code + per-process thread into the target. The
  returned `rpcstub` is the stub header address and must be supplied back on
  every subsequent `CMD_PROC_CALL`.
- **Kernel path:** `SYS_PROC_INSTALL`.

#### `CMD_PROC_CALL = 0xBDAA0006` (proc.c:507-540)
- **Request body:** `struct cmd_proc_call_packet` (68 bytes):
  ```
  uint32_t pid;
  uint64_t rpcstub;        // from CMD_PROC_INTALL response
  uint64_t rpc_rip;        // target function address
  uint64_t rpc_rdi, rpc_rsi, rpc_rdx, rpc_rcx, rpc_r8, rpc_r9;  // SysV args
  ```
- **Response:** `CMD_SUCCESS`, `struct cmd_proc_call_response` (`{uint32_t pid; uint64_t rpc_rax;}`, 12 bytes).
- **Kernel path:** `SYS_PROC_CALL`; blocks until stub marks `rpc_done`.

#### `CMD_PROC_ELF = 0xBDAA0007` (proc.c:542-579)
- **Request body:** `struct cmd_proc_elf_packet` (`{uint32_t pid; uint32_t length;}`, 8 bytes).
- **Trailing data:** `length` bytes of ELF image.
- **Response:** `CMD_SUCCESS` or error.
- **Kernel path:** `SYS_PROC_ELF` - full load + relocation + jmp into entry.

#### `CMD_PROC_PROTECT = 0xBDAA0008` (proc.c:618-640)
- **Request body:** `struct cmd_proc_protect_packet` (20 bytes):
  `{uint32_t pid; uint64_t address; uint32_t length; uint32_t newprot;}`.
- **Response:** `CMD_SUCCESS`.
- **Kernel path:** `SYS_PROC_PROTECT`. `newprot` uses FreeBSD `VM_PROT_*` flags.

#### `CMD_PROC_SCAN = 0xBDAA0009` (proc.c:1401-1485) - **requires auth bit 1**
Legacy single-pass scan; superseded by the `SCAN_START/COUNT/GET` trio.
- **Request body:** `struct cmd_proc_scan_packet` (10 bytes).
- **Trailing data:** `lenData` bytes of comparison value(s).
- **Response:** `uint32_t num`, then `num × uint64_t` addresses.

#### `CMD_PROC_INFO = 0xBDAA000A` (proc.c:1487-1511)
- **Request body:** `struct cmd_proc_info_packet` (4 bytes, `pid`).
- **Response:** `CMD_SUCCESS`, `struct cmd_proc_info_response` (188 bytes):
  `{uint32_t pid; char name[40]; char path[64]; char titleid[16]; char contentid[64];}`.
- **Kernel path:** `SYS_PROC_INFO`.

#### `CMD_PROC_ALLOC = 0xBDAA000B` (proc.c:1513-1534)
- **Request body:** `struct cmd_proc_alloc_packet` (8 bytes, `{pid, length}`).
- **Response:** `CMD_SUCCESS`, `struct cmd_proc_alloc_response` (`uint64_t address`, 8 bytes).
- **Kernel path:** `SYS_PROC_ALLOC`.

#### `CMD_PROC_FREE = 0xBDAA000C` (proc.c:1536-1554)
- **Request body:** `struct cmd_proc_free_packet` (16 bytes, `{pid, address, length}`).
- **Response:** `CMD_SUCCESS`.
- **Kernel path:** `SYS_PROC_FREE`.

#### `CMD_PROC_ALLOC_HINTED = 0xBDAA000E` (proc.c:1557-1580)
- **Request body:** `struct cmd_proc_alloc_hinted_packet` (16 bytes, `{pid, hint, length}`).
- **Response:** `CMD_SUCCESS`, `struct cmd_proc_alloc_response` (`uint64_t address`).
- **Kernel path:** `SYS_PROC_ALLOC_HINTED`.

#### `CMD_PROC_ELF_RPC = 0xBDAA0010` (proc.c:581-616)
Like `CMD_PROC_ELF` but returns the entry address instead of jumping into it.
- **Request body:** `struct cmd_proc_elf_rpc_packet` (8 bytes, `{pid, length}`).
- **Trailing data:** `length` bytes of ELF image.
- **Response:** `CMD_SUCCESS`, `struct cmd_proc_elf_rpc_response` (`uint64_t entry`, 8 bytes).
- **Kernel path:** `SYS_PROC_ELF_RPC`.

#### `CMD_PROC_DISASM_REGION = 0xBDAA0020` (proc.c:145-184)
- **Request body:** `struct cmd_proc_disasm_packet` (20 bytes, `{pid, address, length, max_entries}`).
- **Response:** `CMD_SUCCESS`, a stream of `struct disasm_instr_entry` (32 bytes each),
  terminated by a sentinel entry with all fields `0xFF`.
- **Backend:** Zydis amalgamation, compiled with `ZYAN_NO_LIBC`.

`disasm_instr_entry` layout (protocol.h:143-154):
```
struct disasm_instr_entry {   // 32 bytes
    uint64_t addr;              // instruction start
    uint64_t rip_rel_target;    // resolved absolute addr if MEM_IS_RIP_REL; else 0
    int64_t  mem_disp;          // [base + index*scale + disp]
    uint8_t  length;
    uint8_t  kind;              // bitmask below
    uint8_t  mem_base_reg;      // ZydisRegister low byte; 0 if none
    uint8_t  mem_index_reg;
    uint8_t  mem_scale;         // 1/2/4/8; 0 if none
    uint8_t  mnemonic_lo;       // ZydisMnemonic low byte (category hint)
    uint16_t pad;
};
```

Kind bitmask (protocol.h:139-142):
| Bit  | Meaning                            |
|------|------------------------------------|
| 0x01 | `CALL`                             |
| 0x02 | `RET`                              |
| 0x04 | Unconditional `JMP`                |
| 0x08 | Conditional branch                 |
| 0x10 | Instruction has a memory operand   |
| 0x20 | Memory operand is RIP-relative     |
| 0x40 | Memory read                        |
| 0x80 | Memory write                       |

#### `CMD_PROC_EXTRACT_CODE_XREFS = 0xBDAA0021` (proc.c:220-294)
- **Request body:** `struct cmd_proc_disasm_packet` (20 bytes).
- **Response:** `CMD_SUCCESS`, `uint32_t num_targets`, `num_targets × uint64_t`
  (deduplicated RIP-relative branch/call targets inside the scanned range).

#### `CMD_PROC_FIND_XREFS_TO = 0xBDAA0022` (proc.c:296-328)
- **Request body:** `struct cmd_proc_xrefs_to_packet` (24 bytes):
  `{uint32_t pid; uint64_t scan_address; uint32_t scan_length; uint64_t target_address;}`.
- **Response:** `CMD_SUCCESS`, `uint32_t num`, then `num × (uint64_t address, uint8_t kind)`
  - the `kind` byte reuses the `disasm_instr_entry.kind` bitmask.

#### `CMD_PROC_READ_STACK = 0xBDAA0023` (proc.c)
Server-side RBP-chain walk. The server walks the frame chain itself and bundles
every frame's data (saved RBP, return address, frame-local bytes, and a code
window around the return address) into one response, so clients avoid paying
~4 TCP round-trips per stack frame. Clients can feature-detect this command via
the `CMD_BRANDING` version string (added in v1.2.2).
- **Request body:** `struct cmd_proc_read_stack_packet` (24 bytes):
  `{uint32_t pid; uint64_t rbp; uint64_t rsp; uint32_t depth;}`
  - `rbp` / `rsp` describe the local (top) frame; `depth` caps frames returned (incl. local).
- **Response:** `CMD_SUCCESS`, `uint32_t bundle_len`, then `bundle_len` bytes:
  ```
  u32 n_frames
  per frame {
      u64 rbp; u64 rsp; u64 saved_rbp; u64 ret_addr;
      u32 flags;            // bit0: frame-locals omitted (oversized/invalid) - read rsp..rbp yourself
      u32 frame_locals_len;
      u32 code_len;         // bytes of code at (ret_addr - 10); 0 if unavailable
      u8  frame_locals[frame_locals_len];
      u8  code[code_len];
  }
  ```
  - Caps: `CMD_PROC_READ_STACK_MAX_DEPTH = 64`, `CMD_PROC_READ_STACK_LOCALS_CAP = 0x1000` per frame,
    `CMD_PROC_READ_STACK_CODE_OFF = 10`, `CMD_PROC_READ_STACK_CODE_LEN = 200`.

#### `0xBDAA0024` - assemble x86-64 (proc.c, `proc_assemble_handle`)
Assemble x86-64 text into machine bytes using the Keystone (LLVM-MC) assembler
embedded in the payload. Pure userspace - needs no attached process, no
elevation. The on-console equivalent of what Reaper Studio does with its
client-side `keystone.dll` when applying `VarType.ASM` patches.
**The bare opcode is intentionally NOT given a `CMD_*` macro** - it's a raw
literal in `proc_handle`'s switch - so the published `CMD_*` set (which some
clients enumerate) stays unchanged. Clients that need it should send the literal.
- **Request body:** `struct cmd_proc_assemble_packet` (12 bytes):
  `{uint64_t base_addr; uint32_t ks_opt_syntax;}`
  - `base_addr` is what Keystone resolves PC-relative operands against.
  - `ks_opt_syntax` = 0 keeps the engine default (Intel); pass a Keystone
    `KS_OPT_SYNTAX_*` value (1=Intel, 2=ATT, 3=NASM, 4=MASM, 5=GAS) to switch.
- **Trailing data:** `(datalen - 12)` bytes of asm text. NUL-termination not required.
- **Response on success:** `CMD_SUCCESS`, `struct cmd_proc_assemble_ok` (8 bytes,
  `{uint32_t byte_len; uint32_t insn_count;}`), then `byte_len` bytes of machine code.
- **Response on assembler error:** `CMD_ERROR`, `struct cmd_proc_assemble_err`
  (8 bytes, `{uint32_t ks_errno; uint32_t msg_len;}`), then `msg_len` bytes of
  the human-readable Keystone error (`ks_strerror`).
- **Caveats:** Keystone here was built with `LLVM_ENABLE_THREADS=OFF`; the
  payload's command dispatcher serialises clients so this is fine for the
  current usage, but concurrent assembles would need an external mutex.

#### `CMD_PROC_SCAN_AOB = 0xBDAA0501` (proc.c:1582-1691) - **requires auth bit 1**
- **Request body:** `struct cmd_proc_scan_aob_packet` (22 bytes):
  `{pid, address, length, max_matches(u8), stop_flag(u8), pattern_length(u32)}`.
- **Trailing data:** `pattern_length` bytes. Each byte is either an exact value
  or the wildcard marker `0x??` (see handler at proc.c:1638).
- **Response:** `uint32_t num`, then `num × uint64_t` addresses.

#### `CMD_PROC_SCAN_AOB_MULTI = 0xBDAA0502` (proc.c:1693-1860) - **requires auth bit 1**
- **Request body:** `struct cmd_proc_scan_aob_multi_packet` (21 bytes):
  `{pid, address, length, stop_flag(u8), patterns_length(u32)}`.
- **Trailing data:** `patterns_length` bytes of concatenated patterns; each
  pattern is prefixed by its own `uint8_t length` byte.
- **Response:** `uint32_t num_patterns`, then for each pattern: `uint32_t num`,
  `num × uint64_t`.

#### `CMD_PROC_AUTH = 0xBDAACCFF` (proc.c:1862-1900)
See §1.8.

#### `CMD_PROC_SCAN_START = 0xBDAACC01` (proc.c:1922-2044) - **requires auth bit 1**
- **Request body:** `struct cmd_proc_scan_start_packet` (23 bytes):
  `{pid, address, length, valueType(u8), compareType(u8), alignment(u8), lenData(u32)}`.
- **Trailing data:** `lenData` bytes of seed value(s).
- **Response:** `CMD_SUCCESS`. Server now holds a scan session keyed on `pid`.

#### `CMD_PROC_SCAN_COUNT = 0xBDAACC02` (proc.c:2046-2221) - **requires auth bit 1**
Narrows the current scan set by applying a second comparison.
- **Request body:** `struct cmd_proc_scan_count_packet` (18 bytes):
  `{pid, base_address, valueType(u8), compareType(u8), lenData(u32)}`.
- **Trailing data:** `lenData` bytes.
- **Response:** `uint32_t num_results` (the new cardinality).

#### `CMD_PROC_SCAN_GET = 0xBDAACC03` (proc.c:2223-2265) - **requires auth bit 1**
- **Request body:** `struct cmd_proc_scan_get_packet` (8 bytes, `{pid, count}`).
- **Response:** `CMD_SUCCESS`, `uint32_t actual`, then `actual × uint64_t`.

For the enum values used by `valueType` and `compareType`, see §7.

#### Turbo Scan family `0xBDAACC10` - `0xBDAACC17` (scan_turbo.c) - **raw literals**

A high-throughput scan engine ported from ps5debug-NG (opcodes chosen to not
collide with the published `CMD_*` set, so no macros - see `protocol.h:305-316`).
It layers three accelerators over the classic scan: a SIMD comparator, a
page-table **aliasing** read path (maps target physical pages into the server's
own address space and reads them via a normal VA, far faster than per-chunk
`sys_proc_rw`), a **server-resident** survivor set (results kept server-side so
narrowing passes never ship the full address list), and worker-thread
**parallel** compare. The release payload advertises **both** phases:

- **Phase A** - known-value scan: exact/relative comparisons over a resident,
  aliased, parallel pipeline (the fast path for "find value X, then narrow").
- **Phase B** - snapshot / unknown-initial-value: the server snapshots the region
  into a RAM+disk hybrid value store so later passes can compare against the
  first or previous snapshot (increased/decreased/changed with no seed value).

All flags are **opt-in per request**; capabilities are negotiated via `CAPS`.
Every opcode except `CAPS` **requires auth bit 1** (like the rest of the scan
family). Flag/engine bit tables are in §7.4.

##### `0xBDAACC10` CAPS - `proc_turboscan_caps_handle` (no auth)
- **Request body:** none.
- **Response:** `CMD_SUCCESS`, `struct cmd_proc_turboscan_caps_response` (16 bytes):
  `{u32 version(=1); u32 engines(TSE_* bitmask); u32 max_threads(=4); u32 reserved;}`.
  A release build reports `engines = 0x3FF` (all engines, Phase A + Phase B).
  Clients call this first and gate their UI on the bits they see.

##### `0xBDAACC11` START - `proc_turboscan_start_handle` (auth bit 1)
- **Request body:** `struct cmd_proc_turboscan_start_packet` (mirrors
  `cmd_proc_scan_start_packet` + a trailing `u32 flags` of `TS_*` bits).
- **Trailing data:** `lenData` bytes of seed value(s) (as for `SCAN_START`).
- **Response:** if `TS_SERVER_RESIDENT` was requested, `CMD_SUCCESS` then
  `struct cmd_proc_turboscan_resident_summary` (`{u32 resident_stored; u64 count;}`)
  - when `resident_stored==1` the survivors stay server-side (fetch with GET) and
  no result runs follow. Otherwise the reply is the classic batched
  `(offset,value)` run stream terminated by the 8-byte `0xFFFF...` sentinel, wire-
  identical to the async `SCAN_START` so existing parsers work unchanged.

##### `0xBDAACC12` COUNT (rescan) - `proc_turboscan_count_handle` (auth bit 1)
- **Request body:** `struct cmd_proc_turboscan_count_packet` (mirrors
  `cmd_proc_scan_count_packet` + a trailing `u32 flags`). Narrows the current
  resident set with a second comparison.
- **Response:** same shape as START (resident summary, or streamed runs).

##### `0xBDAACC13` GET - `proc_turboscan_get_handle` (auth bit 1)
- **Request body:** `struct cmd_proc_turboscan_get_packet` (12 bytes,
  `{u32 start_index; u32 count; u32 flags;}`) - fetch resident survivors
  `[start_index, start_index+count)`.
- **Response:** `CMD_SUCCESS`, `u32 hdr`, then `(hdr & 0x7FFFFFFF)` records, then a
  trailing `CMD_SUCCESS`. `hdr` low 31 bits = records returned; bit 31 set => each
  record carries a first-scan value (Phase B 3-value shape). A record is
  `{u64 addr; value current; value previous}` (`8 + 2*value_length` bytes);
  `previous == current` for sessions not keeping a previous snapshot.

##### `0xBDAACC14` END - `proc_turboscan_end_handle` (auth bit 1)
- **Request body:** none.
- **Response:** `CMD_SUCCESS`. Frees this connection's resident survivor set +
  aliasing arena. (Also freed automatically on disconnect.)

##### `0xBDAACC15` CONFIG - `proc_turboscan_config_handle` (auth bit 1)
- **Request body:** `struct cmd_proc_turboscan_config_packet` (8 bytes,
  `{u32 ram_thresh_mb; u32 spill_path_len;}`) then `spill_path_len` path bytes.
  Sets the Phase B snapshot value-store RAM threshold and disk spill directory.
- **Response:** `CMD_SUCCESS`.

##### `0xBDAACC16` REGIONS (classify) - `proc_turboscan_regions_handle` (auth bit 1)
- **Request body:** `struct cmd_proc_turboscan_regions_packet` (16 bytes,
  `{u32 pid; u32 max; u32 probe_bytes; u32 reserved;}`).
- **Response:** `CMD_SUCCESS`, `u32 num`, then `num × struct cmd_proc_turboscan_region_info`
  (32 bytes each: `{u64 start; u64 end; u32 prot; u32 flags; u32 mbps; u32 reserved;}`).
  `flags` bit 0 = leaf PTE `PCD=1` (uncached / slow); `mbps` is a measured probe
  throughput. Informational only - the server does **not** drop any region; this
  lets a client let the user exclude slow/uncached regions from a scan.

##### `0xBDAACC17` CANCEL - `proc_turboscan_cancel_handle` (auth bit 1)
Abort an in-flight turbo scan (`START` or `COUNT`) running for a given pid. Because the
command dispatcher is serial per connection, the connection that issued the long scan is
busy streaming and cannot receive a command - so **CANCEL must be sent on a second,
separately authed connection**. It arms a pid-scoped flag that the scan loops poll (all
parallel workers observe it), so they stop early and emit their normal terminator; the
waiting client on the first connection then unblocks.
- **Request body:** `u32 pid` (the pid whose in-flight scan to cancel; must be non-zero).
- **Response:** `CMD_SUCCESS` (the flag is armed - it does not wait for the scan to notice).
  `CMD_DATA_NULL` if not authed / no body / `pid == 0`.
- **Effect on the cancelled scan:**
  - **Snapshot `START` (unknown-initial-value):** stops cleanly, frees the half-built
    snapshot, and returns the snapshot summary with `snapshot_ok = 0`. Safe to detect.
  - **`START` (known-value) and `COUNT` (rescan):** stop early and return their normal
    terminator with **partial** results; the resident/snapshot session is left
    **indeterminate**. After a cancel, treat the session as invalid - issue `END`
    (`0xBDAACC14`) to discard it, or a fresh `START`, before trusting any results.
- **Timing / staleness:** a cancel arriving before the scan starts is discarded (`START`/
  `COUNT` clear a stale same-pid flag on entry), so you can only cancel a scan that is
  actually running. A cancel for a pid with no running scan is a harmless no-op until the
  next scan on that pid begins (which clears it). The client-driven streaming `COUNT`
  (where the client feeds chunks) is already stoppable by sending its end sentinel and is
  unaffected by this flag.

---

### 2.3 Debug commands (`0xBDBBxxxx`) - dispatched by `debug_handle` (kern.c:779-803)

All debug commands (except `ATTACH`, see §1.5) require an already-attached
debug session. The dispatcher returns early with no response if none exists.

#### `CMD_DEBUG_ATTACH = 0xBDBB0001` (kern.c:98-166, via server.c:188)
- **Request body:** `struct cmd_debug_attach_packet` (8 bytes, `{pid, client_ip}`).
- **Response:** `CMD_SUCCESS`, or `CMD_ALREADY_DEBUG` / `CMD_ERROR`.
- **Side effect:** server opens outbound TCP to client on port 755 (see §1.9)
  and `ptrace(PT_ATTACH)`-es the target.

`debug_attach_handle` at kern.c:92-96 is a stub that always replies `CMD_ERROR`
and returns `1`; it exists so the switch at kern.c:784 is complete, but is
unreachable in normal flow because `server.c:188` intercepts `CMD_DEBUG_ATTACH`
before `cmd_handler` runs and routes to `debug_attach_handle_svc` instead.

#### `CMD_DEBUG_DETACH = 0xBDBB0002` (kern.c:216-461)
- **Request body:** none.
- **Response:** `CMD_SUCCESS`. Clears all breakpoints/watchpoints, resumes target,
  closes port 755 connection.

#### `CMD_DEBUG_SET_BREAKPOINT = 0xBDBB0003` (kern.c:463-501)
- **Request body:** `struct cmd_debug_breakpt_packet` (16 bytes, `{index, enabled, address}`).
- **Response:** `CMD_SUCCESS` or `CMD_INVALID_INDEX`.
- **Backend:** software breakpoint - writes `0xCC` at `address`, saves original byte.
  `MAX_BREAKPOINTS = 30` (protocol.h:438).

#### `CMD_DEBUG_SET_WATCHPOINT = 0xBDBB0004` (kern.c:504-568)
- **Request body:** `struct cmd_debug_watchpt_packet` (24 bytes):
  `{index, enabled, length, breaktype, address}`.
- **Response:** `CMD_SUCCESS` or `CMD_INVALID_INDEX`.
- **Backend:** hardware breakpoint via DR0-DR3 / DR7. `MAX_WATCHPOINTS = 4`
  (protocol.h:439). `breaktype` uses `DBREG_DR7_*` encoding (debug.h:119-126):
  `EXEC=0`, `WRONLY=1`, `RDWR=3`; length `1/2/4/8` via `DBREG_DR7_LEN_*`.

#### `CMD_DEBUG_GET_THREAD_LIST = 0xBDBB0005` (kern.c:570-604)
- **Request body:** none.
- **Response:** `CMD_SUCCESS`, `uint32_t num`, `num × uint32_t lwpid`.

#### `CMD_DEBUG_SUSPEND_THREAD = 0xBDBB0006` (kern.c:606-614)
- **Request body:** `struct cmd_debug_stopthr_packet` (4 bytes, `lwpid`).
- **Response:** `CMD_SUCCESS`.

#### `CMD_DEBUG_RESUME_THREAD = 0xBDBB0007` (kern.c:616-624)
- **Request body:** `struct cmd_debug_resumethr_packet` (4 bytes, `lwpid`).
- **Response:** `CMD_SUCCESS`.

#### `CMD_DEBUG_GETREGS = 0xBDBB0008` (kern.c:626-639)
- **Request body:** `struct cmd_debug_getregs_packet` (4 bytes, `lwpid`).
- **Response:** `CMD_SUCCESS`, `struct __reg64` (184 bytes; debug.h:11-38).

#### `CMD_DEBUG_SETREGS = 0xBDBB0009` (kern.c:641-655)
- **Request body:** `struct cmd_debug_setregs_packet` (8 bytes, `{lwpid, length}`).
- **Trailing data:** `length` bytes - must be `sizeof(struct __reg64)`.
- **Response:** `CMD_SUCCESS`.

#### `CMD_DEBUG_GETFPREGS = 0xBDBB000A` (kern.c:657-670)
- **Request body:** `struct cmd_debug_getregs_packet` (4 bytes, `lwpid`).
- **Response:** `CMD_SUCCESS`, `struct savefpu_ymm` (512 bytes; debug.h:88-97).

#### `CMD_DEBUG_SETFPREGS = 0xBDBB000B` (kern.c:672-686)
- **Request body:** `struct cmd_debug_setregs_packet` (8 bytes).
- **Trailing data:** `length` bytes - `sizeof(struct savefpu_ymm)`.
- **Response:** `CMD_SUCCESS`.

#### `CMD_DEBUG_GETDBREGS = 0xBDBB000C` (kern.c:688-701)
- **Request body:** `struct cmd_debug_getregs_packet` (4 bytes).
- **Response:** `CMD_SUCCESS`, `struct __dbreg64` (128 bytes; 16 × uint64_t).

#### `CMD_DEBUG_SETDBREGS = 0xBDBB000D` (kern.c:703-717)
- **Request body:** `struct cmd_debug_setregs_packet` (8 bytes).
- **Trailing data:** `length` bytes (`sizeof(struct __dbreg64)`).
- **Response:** `CMD_SUCCESS`.

#### `0xBDBB000E` GET FS/GS-base - `debug_getfsgsbase_handle` (debug.c:906) - **raw literal**
Reads the per-thread `FSBASE`/`GSBASE` MSR shadow values for a thread of the
attached process. Dispatched as a raw literal (no `CMD_*` macro), same rationale
as the other new opcodes. Requires an active debug session.
- **Request body:** `uint32_t lwpid`.
- **Response:** `CMD_SUCCESS`, then **16 bytes**: `{u64 fsbase; u64 gsbase;}`. Returns
  `CMD_ERROR` if not attached or the kernel read fails.

#### `0xBDBB000F` SET FS/GS-base - `debug_setfsgsbase_handle` (debug.c:925) - **raw literal**
Writes `FSBASE`/`GSBASE` for a thread of the attached process. Two-status data-phase
pattern (like `CMD_DEBUG_SETREGS`).
- **Request body:** `{u32 lwpid; u32 length;}` - `length` must equal `16`.
- **Trailing data:** after the server's first `CMD_SUCCESS`, the client sends 16
  bytes `{u64 fsbase; u64 gsbase;}`.
- **Response:** `CMD_SUCCESS` (ack), then after the write a second `CMD_SUCCESS`
  (or `CMD_ERROR` on bad length / kernel failure). Clients consume **two** `u32`s.

#### `CMD_DEBUG_CONTINUE = 0xBDBB0010` (kern.c:719-729)
- **Request body:** `struct cmd_debug_stopgo_packet` (4 bytes, `stop`).
- `stop == 0` → resume all threads. `stop == 1` → enter single-step / halted mode.
- **Response:** `CMD_SUCCESS`.

#### `CMD_DEBUG_THREAD_INFO = 0xBDBB0011` (kern.c:731-744)
- **Request body:** `struct cmd_debug_thrinfo_packet` (4 bytes, `lwpid`).
- **Response:** `CMD_SUCCESS`, `struct cmd_debug_thrinfo_response` (40 bytes,
  `{lwpid, priority, name[32]}`).
- **Kernel path:** `SYS_PROC_THRINFO`.

#### `CMD_DEBUG_STEP = 0xBDBB0012` (kern.c:746-756)
- **Request body:** none.
- **Response:** `CMD_SUCCESS`. Single-step all threads of the debugged process.

#### `CMD_DEBUG_STEP_THREAD = 0xBDBB0013` (kern.c:758-776)
- **Request body:** `struct cmd_debug_stopthr_packet` (4 bytes, `lwpid`).
- **Response:** `CMD_SUCCESS`.

#### `CMD_DEBUG_PROCESS_STOP = 0xBDBB0500` (kern.c:54-89)
- **Request body:** 5 bytes of raw data (no named struct):
  `u32 pid; u8 state;` - `state ∈ {0,1,2}` maps to `{resume, stop, kill}`
  (kern.c:61-77). Returns `CMD_ERROR` if `state > 2` or `pid == 0`.
- **Response:** `CMD_SUCCESS`, or `CMD_DATA_NULL`, or `CMD_ERROR`.
- **Behavior:** If a debug session is active (`g_debugging != 0`), stashes a
  pending signal into `g_pending_signal` for the next event dispatch
  (`0 → 0`, `1 → SIGSTOP(17)`, `2 → SIGKILL(9)`). Otherwise calls `kill(pid, sig)`
  directly with `SIGSTOP(19 - FreeBSD SIGSTOP)`, `SIGTSTP(17)`, or `SIGKILL(9)`.

---

### 2.4 Kernel R/W commands (`0xBDCCxxxx`) - dispatched by `kern_handle` (console.c:60-67)

#### `CMD_KERN_BASE = 0xBDCC0001` (console.c:7-13)
- **Request body:** none.
- **Response:** `CMD_SUCCESS`, `uint64_t` kernel base.
- **Kernel path:** `sys_kern_base()`.

#### `CMD_KERN_READ = 0xBDCC0002` (console.c:16-35)
- **Request body:** `struct cmd_kern_read_packet` (12 bytes, `{address, length}`).
- **Response:** `CMD_SUCCESS`, then `length` bytes of kernel memory.
- **Kernel path:** `sys_kern_rw(..., write=0)`.

#### `CMD_KERN_WRITE = 0xBDCC0003` (console.c:38-57)
- **Request body:** `struct cmd_kern_write_packet` (12 bytes, `{address, length}`).
- **Trailing data:** `length` bytes to write.
- **Response pattern** (unique - note the **two** status messages):
  1. `CMD_SUCCESS` sent *before* the server receives the write payload (console.c:51).
  2. `CMD_SUCCESS` sent *after* the write completes (console.c:54).
- **Kernel path:** `sys_kern_rw(..., write=1)` - internally toggles CR0.WP around `memcpy`.

---

### 2.5 Console commands (`0xBDDDxxxx`) - dispatched by `console_handle` (debug.c:121-140)

#### `CMD_CONSOLE_REBOOT = 0xBDDD0001` (debug.c:110-118)
- **Request body:** none.
- **Response:** none - connection is closed; system reboots via `syscall(112, 1, 0)`.

#### `CMD_CONSOLE_END = 0xBDDD0002` (debug.c:133-136)
- **Request body:** none.
- **Response:** none - handler returns `1`, which tears down the connection.

#### `CMD_CONSOLE_PRINT = 0xBDDD0003` (debug.c:58-82)
- **Request body:** `uint32_t length`, then `length` bytes of UTF-8 text.
- **Response:** `CMD_SUCCESS`.
- **Kernel path:** `SYS_CONSOLE_CMD_PRINT` (prefixed `[ps4debug-ng] ` in dmesg).

#### `CMD_CONSOLE_NOTIFY = 0xBDDD0004` (debug.c:85-107)
- **Request body:** `struct cmd_console_notify_packet` (8 bytes, `{messageType, length}`).
- **Trailing data:** `length` bytes of UTF-8 text (must include trailing `\0`).
- **Response:** `CMD_SUCCESS`.
- **Effect:** calls `sceSysUtilSendSystemNotificationWithText(messageType, text)`
  on the PS4; `messageType = 222` is the generic icon used by main.c:39.

#### `CMD_CONSOLE_INFO = 0xBDDD0005` (debug.c:129-132)
- **Request body:** none.
- **Response:** `CMD_SUCCESS`. No side effect (ping variant).

#### `0xBDDD0006` - foreground-app metadata (console.c, `console_foreground_app_handle`)
Identify the currently-foregrounded game and return its metadata (pid, titleid,
contentid, process name, and version). Resolves the version from the title's
`param.sfo`. **Raw literal in `console_handle`'s switch - no `CMD_*` macro**
(same rationale as `0xBDAA0024`).
- **Request body:** none.
- **Response:** `CMD_SUCCESS`, `struct cmd_console_foreground_app_response` (132 bytes):
  ```
  u32  pid;
  char titleid[16];
  char contentid[64];
  char name[40];
  char app_ver[8];      // PS4 "XX.YY" version string from param.sfo
  ```
- **Behaviour:** if there is no foreground game, all fields are zeroed (`pid==0`).
- **Version selection:** when both `APP_VER` and `VERSION` are present in
  `param.sfo`, the server returns the lexically larger of the two so that
  packer-mistakes (where the bundled `VERSION` overtakes `APP_VER`) still
  yield the higher-precedence version string.

---

## 3. Kernel-side syscall interface

The debugger payload is userland code; it calls into the companion kernel module
(`kdebugger/`) through three wrapper functions declared in `debugger/include/kdbg.h`:

| C wrapper                                              | Purpose                                           |
|--------------------------------------------------------|---------------------------------------------------|
| `sys_proc_list(entries, num)`                          | Enumerate processes (backs `CMD_PROC_LIST`).      |
| `sys_proc_rw(pid, addr, data, len, write)`             | Target-process memory R/W.                        |
| `sys_proc_cmd(pid, cmd, data)`                         | Dispatch any `SYS_PROC_*` command below.          |
| `sys_kern_base(&kbase)`                                | Return kernel base address.                       |
| `sys_kern_rw(addr, data, len, write)`                  | Kernel memory R/W (toggles CR0.WP on write).      |
| `sys_console_cmd(cmd, data)`                           | Dispatch any `SYS_CONSOLE_CMD_*` below.           |

The `cmd` arg to `sys_proc_cmd` selects one of:

| Name                   | ID | Arg struct                       | Purpose                                                                                         |
|------------------------|----|----------------------------------|-------------------------------------------------------------------------------------------------|
| `SYS_PROC_ALLOC`       | 1  | `sys_proc_alloc_args`            | Allocate in target; fills `args->address`.                                                      |
| `SYS_PROC_FREE`        | 2  | `sys_proc_free_args`             | Free target region.                                                                             |
| `SYS_PROC_PROTECT`     | 3  | `sys_proc_protect_args`          | `mprotect` target region. `prot` uses FreeBSD VM flags.                                         |
| `SYS_PROC_VM_MAP`      | 4  | `sys_proc_vm_map_args`           | If `maps==NULL`, return count in `num`; else populate up to `num` entries.                      |
| `SYS_PROC_INSTALL`     | 5  | `sys_proc_install_args`          | Install RPC stub in target; return entry address.                                               |
| `SYS_PROC_CALL`        | 6  | `sys_proc_call_args`             | Execute function in target via RPC stub; blocks until `rpcstub_header.rpc_done`.                |
| `SYS_PROC_ELF`         | 7  | `sys_proc_elf_args`              | Load + relocate + run ELF in target.                                                            |
| `SYS_PROC_INFO`        | 8  | `sys_proc_info_args`             | Populate `{name, path, titleid, contentid}` for `pid`.                                          |
| `SYS_PROC_THRINFO`     | 9  | `sys_proc_thrinfo_args`          | Populate `{priority, name}` for `lwpid`.                                                        |
| `SYS_PROC_ALLOC_HINTED`| 10 | `sys_proc_alloc_args`            | Allocate near hint address (`args->address` is hint in, actual out).                            |
| `SYS_PROC_ELF_RPC`     | 11 | `sys_proc_elf_rpc_args`          | Load ELF, return entry without jumping.                                                         |

The `cmd` arg to `sys_console_cmd` selects one of:

| Name                          | ID | Purpose                                                                              |
|-------------------------------|----|--------------------------------------------------------------------------------------|
| `SYS_CONSOLE_CMD_REBOOT`      | 1  | `kern_reboot(0)` - hard reboot.                                                      |
| `SYS_CONSOLE_CMD_PRINT`       | 2  | `printf("[ps4debug-ng] %s\n", data)` into kernel console (also used by `uprintf`).   |
| `SYS_CONSOLE_CMD_JAILBREAK`   | 3  | Escalate calling process: `cr_uid=cr_ruid=cr_rgid=0`, clear prison (invoked once at payload startup, main.c:22). |

Note: `SYS_CONSOLE_CMD_JAILBREAK` is not reachable via the network protocol -
it is called locally by the payload before the server is started. Hostile
clients cannot invoke it directly.

---

## 4. Full command ID table

| Hex          | Name                             | Handler                         | Auth? |
|--------------|----------------------------------|---------------------------------|-------|
| `0xBD000001` | `CMD_VERSION`                    | inline (server.c:53)            |       |
| `0xBD000500` | `CMD_FW_VERSION`                 | inline (server.c:55)            |       |
| `0xBD000501` | `CMD_BRANDING`                   | inline (server.c:59)            |       |
| `0xBD000502` | `CMD_PLATFORM_ID`                | inline (server.c:64)            |       |
| `0xBDAA0001` | `CMD_PROC_LIST`                  | `proc_list_handle`              |       |
| `0xBDAA0002` | `CMD_PROC_READ`                  | `proc_read_handle`              |       |
| `0xBDAA0003` | `CMD_PROC_WRITE`                 | `proc_write_handle`             |       |
| `0xBDAACC04` | (bulk write, no name macro)      | `proc_write_multi_handle`       |       |
| `0xBDAA0004` | `CMD_PROC_MAPS`                  | `proc_maps_handle`              |       |
| `0xBDAA0005` | `CMD_PROC_INTALL`                | `proc_install_handle`           |       |
| `0xBDAA0006` | `CMD_PROC_CALL`                  | `proc_call_handle`              |       |
| `0xBDAA0007` | `CMD_PROC_ELF`                   | `proc_elf_handle`               |       |
| `0xBDAA0008` | `CMD_PROC_PROTECT`               | `proc_protect_handle`           |       |
| `0xBDAA0009` | `CMD_PROC_SCAN`                  | `proc_scan_handle`              | bit 1 |
| `0xBDAA000A` | `CMD_PROC_INFO`                  | `proc_info_handle`              |       |
| `0xBDAA000B` | `CMD_PROC_ALLOC`                 | `proc_alloc_handle`             |       |
| `0xBDAA000C` | `CMD_PROC_FREE`                  | `proc_free_handle`              |       |
| `0xBDAA000E` | `CMD_PROC_ALLOC_HINTED`          | `proc_alloc_hinted_handle`      |       |
| `0xBDAA0010` | `CMD_PROC_ELF_RPC`               | `proc_elf_rpc_handle`           |       |
| `0xBDAA0020` | `CMD_PROC_DISASM_REGION`         | `proc_disasm_region_handle`     |       |
| `0xBDAA0021` | `CMD_PROC_EXTRACT_CODE_XREFS`    | `proc_extract_code_xrefs_handle`|       |
| `0xBDAA0022` | `CMD_PROC_FIND_XREFS_TO`         | `proc_find_xrefs_to_handle`     |       |
| `0xBDAA0023` | `CMD_PROC_READ_STACK`            | `proc_read_stack_handle`        |       |
| `0xBDAA0024` | _(raw literal, no macro)_        | `proc_assemble_handle`          |       |
| `0xBDAA0501` | `CMD_PROC_SCAN_AOB`              | `proc_scan_aob_handle`          | bit 1 |
| `0xBDAA0502` | `CMD_PROC_SCAN_AOB_MULTI`        | `proc_scan_aob_multi_handle`    | bit 1 |
| `0xBDAACCFF` | `CMD_PROC_AUTH`                  | `proc_auth_handle`              |       |
| `0xBDAACC01` | `CMD_PROC_SCAN_START`            | `proc_scan_start_handle`        | bit 1 |
| `0xBDAACC02` | `CMD_PROC_SCAN_COUNT`            | `proc_scan_count_handle`        | bit 1 |
| `0xBDAACC03` | `CMD_PROC_SCAN_GET`              | `proc_scan_get_handle`          | bit 1 |
| `0xBDAACC06` | `CMD_PROC_NOP`                   | inline (server.c:68)            |       |
| `0xBDAACC10` | _(raw literal)_ Turbo CAPS       | `proc_turboscan_caps_handle`    |       |
| `0xBDAACC11` | _(raw literal)_ Turbo START      | `proc_turboscan_start_handle`   | bit 1 |
| `0xBDAACC12` | _(raw literal)_ Turbo COUNT      | `proc_turboscan_count_handle`   | bit 1 |
| `0xBDAACC13` | _(raw literal)_ Turbo GET        | `proc_turboscan_get_handle`     | bit 1 |
| `0xBDAACC14` | _(raw literal)_ Turbo END        | `proc_turboscan_end_handle`     | bit 1 |
| `0xBDAACC15` | _(raw literal)_ Turbo CONFIG     | `proc_turboscan_config_handle`  | bit 1 |
| `0xBDAACC16` | _(raw literal)_ Turbo REGIONS    | `proc_turboscan_regions_handle` | bit 1 |
| `0xBDAACC17` | _(raw literal)_ Turbo CANCEL     | `proc_turboscan_cancel_handle`  | bit 1 |
| `0xBDBB0001` | `CMD_DEBUG_ATTACH`               | `debug_attach_handle_svc`       |       |
| `0xBDBB0002` | `CMD_DEBUG_DETACH`               | `debug_detach_handle`           |       |
| `0xBDBB0003` | `CMD_DEBUG_SET_BREAKPOINT`       | `debug_set_breakpoint_handle`   |       |
| `0xBDBB0004` | `CMD_DEBUG_SET_WATCHPOINT`       | `debug_set_watchpoint_handle`   |       |
| `0xBDBB0005` | `CMD_DEBUG_GET_THREAD_LIST`      | `debug_get_thread_list_handle`  |       |
| `0xBDBB0006` | `CMD_DEBUG_SUSPEND_THREAD`       | `debug_suspend_thread_handle`   |       |
| `0xBDBB0007` | `CMD_DEBUG_RESUME_THREAD`        | `debug_resume_thread_handle`    |       |
| `0xBDBB0008` | `CMD_DEBUG_GETREGS`              | `debug_getregs_handle`          |       |
| `0xBDBB0009` | `CMD_DEBUG_SETREGS`              | `debug_setregs_handle`          |       |
| `0xBDBB000A` | `CMD_DEBUG_GETFPREGS`            | `debug_getfpregs_handle`        |       |
| `0xBDBB000B` | `CMD_DEBUG_SETFPREGS`            | `debug_setfpregs_handle`        |       |
| `0xBDBB000C` | `CMD_DEBUG_GETDBREGS`            | `debug_getdbregs_handle`        |       |
| `0xBDBB000D` | `CMD_DEBUG_SETDBREGS`            | `debug_setdbregs_handle`        |       |
| `0xBDBB000E` | _(raw literal)_ GET FS/GS-base   | `debug_getfsgsbase_handle`      |       |
| `0xBDBB000F` | _(raw literal)_ SET FS/GS-base   | `debug_setfsgsbase_handle`      |       |
| `0xBDBB0010` | `CMD_DEBUG_CONTINUE`             | `debug_continue_handle`         |       |
| `0xBDBB0011` | `CMD_DEBUG_THREAD_INFO`          | `debug_thread_info_handle`      |       |
| `0xBDBB0012` | `CMD_DEBUG_STEP`                 | `debug_step_handle`             |       |
| `0xBDBB0013` | `CMD_DEBUG_STEP_THREAD`          | `debug_step_thread_handle`      |       |
| `0xBDBB0500` | `CMD_DEBUG_PROCESS_STOP`         | `debug_process_stop_handle`     |       |
| `0xBDCC0001` | `CMD_KERN_BASE`                  | `kern_base_handle`              |       |
| `0xBDCC0002` | `CMD_KERN_READ`                  | `kern_read_handle`              |       |
| `0xBDCC0003` | `CMD_KERN_WRITE`                 | `kern_write_handle`             |       |
| `0xBDDD0001` | `CMD_CONSOLE_REBOOT`             | `console_reboot_handle`         |       |
| `0xBDDD0002` | `CMD_CONSOLE_END`                | inline (debug.c:133)            |       |
| `0xBDDD0003` | `CMD_CONSOLE_PRINT`              | `console_print_handle`          |       |
| `0xBDDD0004` | `CMD_CONSOLE_NOTIFY`             | `console_notify_handle`         |       |
| `0xBDDD0005` | `CMD_CONSOLE_INFO`               | inline (debug.c:129)            |       |
| `0xBDDD0006` | _(raw literal, no macro)_        | `console_foreground_app_handle` |       |

---

## 5. Request packet appendix

All structs are `__attribute__((packed))`. Sizes match the `CMD_*_PACKET_SIZE` macros.

| Struct                                  | Size | Fields                                                                    |
|-----------------------------------------|------|---------------------------------------------------------------------------|
| `cmd_packet` (wire portion)             | 12   | `u32 magic; u32 cmd; u32 datalen;`                                        |
| `cmd_proc_read_packet`                  | 16   | `u32 pid; u64 address; u32 length;`                                       |
| `cmd_proc_write_packet`                 | 16   | `u32 pid; u64 address; u32 length;`                                       |
| `cmd_proc_maps_packet`                  | 4    | `u32 pid;`                                                                |
| `cmd_proc_install_packet`               | 4    | `u32 pid;`                                                                |
| `cmd_proc_call_packet`                  | 68   | `u32 pid; u64 rpcstub,rpc_rip,rpc_rdi,rpc_rsi,rpc_rdx,rpc_rcx,rpc_r8,rpc_r9;` |
| `cmd_proc_elf_packet`                   | 8    | `u32 pid; u32 length;`                                                    |
| `cmd_proc_elf_rpc_packet`               | 8    | `u32 pid; u32 length;`                                                    |
| `cmd_proc_protect_packet`               | 20   | `u32 pid; u64 address; u32 length; u32 newprot;`                          |
| `cmd_proc_scan_packet`                  | 10   | `u32 pid; u8 valueType, compareType; u32 lenData;`                        |
| `cmd_proc_scan_start_packet`            | 23   | `u32 pid; u64 address; u32 length; u8 valueType, compareType, alignment; u32 lenData;` |
| `cmd_proc_scan_count_packet`            | 18   | `u32 pid; u64 base_address; u8 valueType, compareType; u32 lenData;`      |
| `cmd_proc_scan_aob_packet`              | 22   | `u32 pid; u64 address; u32 length; u8 max_matches, stop_flag; u32 pattern_length;` |
| `cmd_proc_scan_aob_multi_packet`        | 21   | `u32 pid; u64 address; u32 length; u8 stop_flag; u32 patterns_length;`    |
| `cmd_proc_auth_packet`                  | 8    | `u32 magic (0xBB40E64D); u32 flags;`                                      |
| `cmd_proc_scan_get_packet`              | 8    | `u32 pid; u32 count;`                                                     |
| `cmd_proc_info_packet`                  | 4    | `u32 pid;`                                                                |
| `cmd_proc_alloc_packet`                 | 8    | `u32 pid; u32 length;`                                                    |
| `cmd_proc_alloc_hinted_packet`          | 16   | `u32 pid; u64 hint; u32 length;`                                          |
| `cmd_proc_free_packet`                  | 16   | `u32 pid; u64 address; u32 length;`                                       |
| `cmd_proc_disasm_packet`                | 20   | `u32 pid; u64 address; u32 length; u32 max_entries;`                      |
| `cmd_proc_xrefs_to_packet`              | 24   | `u32 pid; u64 scan_address; u32 scan_length; u64 target_address;`         |
| `cmd_proc_read_stack_packet`            | 24   | `u32 pid; u64 rbp; u64 rsp; u32 depth;`                                   |
| `cmd_proc_assemble_packet`              | 12   | `u64 base_addr; u32 ks_opt_syntax;` (followed by asm text on the wire)    |
| `cmd_proc_turboscan_start_packet`       | 27   | `u32 pid; u64 address; u32 length; u8 valueType, compareType, alignment; u32 lenData; u32 flags;` |
| `cmd_proc_turboscan_count_packet`       | 22   | `u32 pid; u64 base_address; u8 valueType, compareType; u32 lenData; u32 flags;` |
| `cmd_proc_turboscan_get_packet`         | 12   | `u32 start_index; u32 count; u32 flags;`                                  |
| `cmd_proc_turboscan_config_packet`      | 8    | `u32 ram_thresh_mb; u32 spill_path_len;` (followed by spill path bytes)   |
| `cmd_proc_turboscan_regions_packet`     | 16   | `u32 pid; u32 max; u32 probe_bytes; u32 reserved;`                        |
| Turbo CANCEL request                    | 4    | `u32 pid;` (raw, no named struct; sent on a 2nd connection)               |
| GET FS/GS-base request                  | 4    | `u32 lwpid;` (raw, no named struct)                                       |
| SET FS/GS-base request                  | 8    | `u32 lwpid; u32 length(=16);` (16-byte `{u64 fsbase; u64 gsbase}` data phase) |
| `cmd_debug_attach_packet`               | 8    | `u32 pid; u32 client_ip;`                                                 |
| `cmd_debug_breakpt_packet`              | 16   | `u32 index, enabled; u64 address;`                                        |
| `cmd_debug_watchpt_packet`              | 24   | `u32 index, enabled, length, breaktype; u64 address;`                     |
| `cmd_debug_stopthr_packet`              | 4    | `u32 lwpid;`                                                              |
| `cmd_debug_resumethr_packet`            | 4    | `u32 lwpid;`                                                              |
| `cmd_debug_getregs_packet`              | 4    | `u32 lwpid;`                                                              |
| `cmd_debug_setregs_packet`              | 8    | `u32 lwpid; u32 length;`                                                  |
| `cmd_debug_stopgo_packet`               | 4    | `u32 stop;`                                                               |
| `cmd_debug_thrinfo_packet`              | 4    | `u32 lwpid;`                                                              |
| `cmd_kern_read_packet`                  | 12   | `u64 address; u32 length;`                                                |
| `cmd_kern_write_packet`                 | 12   | `u64 address; u32 length;`                                                |
| `cmd_console_notify_packet`             | 8    | `u32 messageType; u32 length;`                                            |

## 6. Response packet appendix

| Struct                                  | Size | Fields                                                                    |
|-----------------------------------------|------|---------------------------------------------------------------------------|
| `cmd_proc_install_response`             | 8    | `u64 rpcstub;`                                                            |
| `cmd_proc_call_response`                | 12   | `u32 pid; u64 rpc_rax;`                                                   |
| `cmd_proc_elf_rpc_response`             | 8    | `u64 entry;`                                                              |
| `cmd_proc_alloc_response`               | 8    | `u64 address;`                                                            |
| `cmd_proc_info_response`                | 188  | `u32 pid; char name[40], path[64], titleid[16], contentid[64];`           |
| `cmd_proc_assemble_ok`                  | 8    | `u32 byte_len; u32 insn_count;` (followed by `byte_len` machine bytes)    |
| `cmd_proc_assemble_err`                 | 8    | `u32 ks_errno; u32 msg_len;` (followed by `msg_len` chars of `ks_strerror`) |
| `cmd_console_foreground_app_response`   | 132  | `u32 pid; char titleid[16], contentid[64], name[40], app_ver[8];`         |
| `cmd_debug_thrinfo_response`            | 40   | `u32 lwpid, priority; char name[32];`                                     |
| `cmd_proc_turboscan_caps_response`      | 16   | `u32 version; u32 engines; u32 max_threads; u32 reserved;`                |
| `cmd_proc_turboscan_resident_summary`   | 12   | `u32 resident_stored; u64 count;`                                         |
| `cmd_proc_turboscan_region_info` (streamed) | 32 | `u64 start; u64 end; u32 prot; u32 flags; u32 mbps; u32 reserved;`       |
| GET FS/GS-base response                 | 16   | `u64 fsbase; u64 gsbase;` (after `CMD_SUCCESS`)                           |
| `disasm_instr_entry` (streamed)         | 32   | see §2.2                                                                  |

---

## 7. Enum reference

### 7.1 `cmd_proc_scan_valuetype` (protocol.h:221-234)

| Value | Name               |
|-------|--------------------|
| 0     | `valTypeUInt8`     |
| 1     | `valTypeInt8`      |
| 2     | `valTypeUInt16`    |
| 3     | `valTypeInt16`     |
| 4     | `valTypeUInt32`    |
| 5     | `valTypeInt32`     |
| 6     | `valTypeUInt64`    |
| 7     | `valTypeInt64`     |
| 8     | `valTypeFloat`     |
| 9     | `valTypeDouble`    |
| 10    | `valTypeArrBytes`  |
| 11    | `valTypeString`    |

### 7.2 `cmd_proc_scan_comparetype` (protocol.h:236-252)

| Value | Name                               |
|-------|------------------------------------|
| 0     | `cmpTypeExactValue`                |
| 1     | `cmpTypeFuzzyValue`                |
| 2     | `cmpTypeBiggerThan`                |
| 3     | `cmpTypeSmallerThan`               |
| 4     | `cmpTypeValueBetween`              |
| 5     | `cmpTypeIncreasedValue`            |
| 6     | `cmpTypeIncreasedValueBy`          |
| 7     | `cmpTypeDecreasedValue`            |
| 8     | `cmpTypeDecreasedValueBy`          |
| 9     | `cmpTypeChangedValue`              |
| 10    | `cmpTypeUnchangedValue`            |
| 11    | `cmpTypeUnknownInitialValue`       |
| 12    | `cmpTypeUnknownInitialLowValue`    |

### 7.3 DR7 encoding for hardware watchpoints (debug.h:119-126)

`breaktype` in `cmd_debug_watchpt_packet`:

| Value | Name                  |
|-------|-----------------------|
| 0     | `DBREG_DR7_EXEC`      |
| 1     | `DBREG_DR7_WRONLY`    |
| 3     | `DBREG_DR7_RDWR`      |

`length` encoding (reused as DR7 length field):

| Value | Bytes |
|-------|-------|
| 0     | 1     |
| 1     | 2     |
| 2     | 8     |
| 3     | 4     |

### 7.4 Turbo Scan engine bits + request flags (protocol.h:318-339)

`TSE_*` - reported by `CAPS` in `engines` (which accelerators/phases the server supports):

| Bit          | Name                    | Meaning                                        |
|--------------|-------------------------|------------------------------------------------|
| `0x00000001` | `TSE_SIMD_COMPARE`      | SIMD comparator                                |
| `0x00000002` | `TSE_ALIASING`          | page-table aliasing read path                  |
| `0x00000004` | `TSE_SERVER_RESIDENT`   | server-resident survivor set                   |
| `0x00000008` | `TSE_SNAPSHOT`          | Phase B snapshot store                          |
| `0x00000010` | `TSE_SNAPSHOT_SEGMENTS` | Phase B multi-segment snapshot                 |
| `0x00000020` | `TSE_SNAPSHOT_CONFIG`   | Phase B `CONFIG` (RAM threshold + spill dir)   |
| `0x00000040` | `TSE_SNAPSHOT_FIRST`    | compare vs first snapshot                      |
| `0x00000080` | `TSE_SNAPSHOT_PREVIOUS` | compare vs previous snapshot                   |
| `0x00000100` | `TSE_PARALLEL_COMPARE`  | worker-thread parallel compare                 |
| `0x00000200` | `TSE_RESCAN_ALIASING`   | aliasing on rescan (`COUNT`) too               |

`TS_*` - per-request opt-in flags in `START` / `COUNT` `flags`:

| Bit          | Name                        | Meaning                                     |
|--------------|-----------------------------|---------------------------------------------|
| `0x00000001` | `TS_USE_ALIASING`           | use the aliasing read path this scan        |
| `0x00000002` | `TS_SERVER_RESIDENT`        | keep survivors server-side (fetch via `GET`)|
| `0x00000004` | `TS_SNAPSHOT`               | take a Phase B snapshot                      |
| `0x00000008` | `TS_SNAPSHOT_INCLUDE_ZEROS` | include zero-valued slots in the snapshot   |
| `0x00000010` | `TS_SNAPSHOT_SEGMENTS`      | segmented snapshot                           |
| `0x00000020` | `TS_SNAPSHOT_KEEP_FIRST`    | retain first snapshot for later compares    |
| `0x00000040` | `TS_SNAPSHOT_KEEP_PREVIOUS` | retain previous snapshot for later compares |
| `0x00000080` | `TS_PARALLEL_COMPARE`       | request parallel compare                     |
| `0x00000100` | `TS_RESCAN_ALIASING`        | use aliasing on the `COUNT` rescan           |

---

## 8. Response framing patterns

Four reply shapes cover every handler:

1. **Fixed status**: `uint32_t status` (usually `CMD_SUCCESS`). Used by writes,
   breakpoint/watchpoint management, thread suspend/resume, etc.
2. **Status + fixed struct**: `uint32_t status` then a `struct cmd_*_response`
   of known size. Used by info/install/call/alloc.
3. **Status + count + array**: `uint32_t status`, `uint32_t count`,
   `count × element`. Used by list, thread list, maps, scan results.
4. **Streamed with sentinel**: `uint32_t status`, then a sequence of fixed-size
   records terminated by an all-`0xFF` sentinel. Only `CMD_PROC_DISASM_REGION`.

The `CMD_KERN_WRITE` handler is the only exception: it sends `CMD_SUCCESS`
*twice* (before and after the data phase). Clients must consume both `u32`s.

---

## 9. Known source-code oddities

Documented here so a developer doesn't mistake them for bugs:

- `CMD_PROC_INTALL` is misspelled (missing `S`). Preserved on the wire.
- `debug_attach_handle` (kern.c:92-96) is a stub that always returns `CMD_ERROR`;
  the real attach path is `debug_attach_handle_svc`, which `server.c:188`
  intercepts `CMD_DEBUG_ATTACH` to call directly. Both declarations remain in
  `include/kern.h`.
- `struct debug_interrupt_packet` is `packed` - the compiler warns about
  `savefpu` being offset 224 instead of a 64-byte boundary. The layout is
  intentional; clients rely on `DEBUG_INTERRUPT_PACKET_SIZE = 0x4A0` exactly.
- `SYS_CONSOLE_CMD_JAILBREAK` has no corresponding `CMD_*` opcode: it is
  invoked locally by the payload at startup (main.c:22) and is not reachable
  over the network.
- The disassembler amalgamation (`third_party/zydis/Zydis.c`) is compiled with
  `-O3 -DNDEBUG -w -DZYAN_NO_LIBC`; the rest of the debugger is `-O2`. This is
  a deliberate Makefile override (debugger/Makefile:45-46).

---

*This document reflects the ps4debug-NG `v1.3.0` payload (`PS4DEBUG_NG_VERSION_STR`
in `version.h`, surfaced via `PACKET_BRANDING` at protocol.h:13). Last reconciled
against the source tree on 2026-07-01, when the Turbo Scan family
(`0xBDAACC10-0xBDAACC17`, incl. the CANCEL command) and FS/GS-base
(`0xBDBB000E/0xBDBB000F`) were added (the v1.3.0 bump).
Wire layouts (struct sizes/fields, opcodes, status codes, enums) are authoritative;
per-command `file:line` citations are best-effort and can lag source edits - treat
them as starting points, not exact anchors.*
