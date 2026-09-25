# ps4debug-NG

A debugger payload for jailbroken PlayStation 4 consoles.
Ships a userland command server **plus** a companion kernel module that lets
remote clients inspect and manipulate running processes, the kernel itself,
and the system UI over a simple TCP protocol.

ps4debug-NG is an independent reimplementation of Ctn's `ps4debug v1.1.19`,
wire-compatible with existing clients. It is licensed under GPL-3.

**Feature parity and then some.** Every command, every behavior, and every
firmware supported by Ctn's v1.1.19 is implemented here - existing clients
should work without modification.

## Official Discord Server: [Team Reaper](https://discord.gg/7bjtgZf4PY)

## Official PlayStation 5 version [ps4debug-NG](https://github.com/Pharaoh2k/ps5debug-NG)

---

## Supported firmwares

37 firmware versions across the 5.05-13.52 range. Each has a dedicated kernel
patch routine in [installer/source/installer.c](installer/source/installer.c);
booting on an unsupported FW prints `unsupported firmware <N> - kernel not
patched` to the kernel log and aborts cleanly.

| Major | Versions                                    |
|-------|---------------------------------------------|
| 5.xx  | 5.05, 5.07                                  |
| 6.xx  | 6.71, 6.72                                  |
| 7.xx  | 7.00, 7.02, 7.50, 7.51, 7.55                |
| 8.xx  | 8.00, 8.03, 8.50, 8.52                      |
| 9.xx  | 9.00, 9.03, 9.04, 9.50, 9.51, 9.60          |
| 10.xx | 10.00, 10.01, 10.50, 10.70, 10.71           |
| 11.xx | 11.00, 11.02, 11.50, 11.52                  |
| 12.xx | 12.00, 12.02, 12.50, 12.52                  |
| 13.xx | 13.00, 13.02, 13.04, 13.50, 13.52           |

Clients can read the running FW with `CMD_FW_VERSION` (returns a `uint16_t` in
`major*100 + minor` form - e.g. `0x1F4 = 500 = 5.00`).

**13.x support:** 13.00 / 13.02 / 13.04 / 13.50 / 13.52 run the full command set, including
the commands that spawn a worker thread in the target process (`CMD_PROC_INTALL`,
`CMD_PROC_CALL`, `CMD_PROC_ELF`, `CMD_PROC_ELF_RPC`) - the libkernel.sprx symbol
offsets (`scePthreadAttrInit`, `scePthreadAttrSetstacksize`, `scePthreadCreate`,
`_thr_initial`) are now sourced for the 13.x series in the `proc_create_thread`
switch in [kdebugger/source/proc.c](kdebugger/source/proc.c). (13.x firmware
offsets/patches were validated against decrypted kernels; on-hardware validation
to date has been on earlier firmware.)

---

## Primary Features (fully functional & optimized)

### Process inspection and manipulation
- **Enumerate processes** (`p_comm` + pid list).
- **Read and content-verified write target memory** in 64 KiB streamed chunks.
  Writes report success only after exact readback; short transfers, syscall
  failures, and byte mismatches return failure.
- **Bulk write / freeze** (`0xBDAACC04`) - apply many `{address, length, bytes}`
  writes in one exchange (the write counterpart to bulk-read), collapsing a
  freeze / multi-poke loop into a single round-trip with content-verified
  per-entry status. Raw-literal opcode.
- **List virtual memory maps (including missing sections improvements)** - ranges, protections, backing names.
- **Query process metadata** - name, path, titleId, contentId.
- **Identify the foreground app** (`0xBDDD0006`) - returns pid + titleid + contentid
  + process name + the game's version, parsed server-side from the title's
  `param.sfo`. Useful for clients that need to know what's currently running
  without listing every process.
- **Server-side stack walk** (`CMD_PROC_READ_STACK`, v1.2.2+) - the server
  walks the RBP chain itself (up to 64 frames) and bundles each frame's
  saved-RBP, return address, frame-local bytes, and a 200-byte code window
  around the return address into one response. Clients avoid paying ~4 TCP
  round-trips per stack frame.
- **Change memory protection** on arbitrary target regions.
- **Allocate / free / hint-allocate** memory inside any target process.

### In-target code execution
- **Install an RPC stub** (`CMD_PROC_INTALL`) - injects a reusable trampoline
  with its own thread into the target.
- **Call arbitrary functions** with up to six SysV ABI register arguments and
  read back `rax` (`CMD_PROC_CALL`).
- **Load ELFs** into a target process - either jump to the entry point
  immediately (`CMD_PROC_ELF`) or return the entry for later invocation
  (`CMD_PROC_ELF_RPC`).

### Full userland debugger
- **Attach** to a single target with `CMD_DEBUG_ATTACH` (sets up an async
  interrupt channel back to the client).
- **Software breakpoints** - up to **30** slots, transparent `0xCC` injection.
- **Hardware watchpoints** - up to **4** DR0-DR3 slots with read / write /
  read-write and 1/2/4/8-byte granularity.
- **Thread control** - list, suspend, resume, single-step, and per-thread step.
- **Full register access** - general-purpose (`__reg64`), FPU + YMM
  (`savefpu_ymm`, 512 B), and debug registers (`__dbreg64`, 16 × u64).
- **Per-thread FS/GS base** (`0xBDBB000E` get / `0xBDBB000F` set) - read/write the
  `FSBASE`/`GSBASE` thread pointers of an attached process, needed to resolve
  thread-local storage. Raw-literal opcodes.
- **Continue / stop / halt** the whole process from one command.
- **Asynchronous interrupt packets** (1184 bytes each) delivered on a separate
  TCP connection so the client never polls.

### Kernel access
- Get the **kernel base address**.
- **Read** arbitrary kernel memory.
- **Write** arbitrary kernel memory - the server toggles `CR0.WP` around the
  write for you.

**Custom syscall slots (PS4-HEN / GoldHEN coexistence).** The kdebugger installs
its six syscalls at **257-262** (`syscalls.h`), not at the traditional ps4debug
107-112 block. PS4-HEN and GoldHEN install their own `sys_proc_list` /
`sys_proc_rw` / `sys_proc_cmd` at 107 / 108 / 109 whenever `enable_plugins` is
set (their default), and `install_syscall()` overwrites the `sysent` entry
outright - so whichever payload loaded last silently owned those slots. Because
HEN's `sys_proc_cmd` implements only `SYS_PROC_VM_MAP`, a HEN payload installing
after ours left read/write working while allocation, RPC, ELF injection and
thread info all failed. Moving our block removes that ordering hazard entirely
and leaves HEN's plugin syscalls untouched. 257-262 were verified `nosys` on the
decrypted kernels of **all** supported firmwares (229 slots are free on every
one of them); re-check against a new firmware's `sysent` table before adding it.

### Built-in Zydis disassembler
Large memory regions never leave the PS4. Three server-side decoder commands
keep bandwidth low:
- `CMD_PROC_DISASM_REGION` - packed 32-byte-per-instruction stream with
  control-flow, memory-operand, and RIP-relative metadata.
- `CMD_PROC_EXTRACT_CODE_XREFS` - all resolved RIP-relative branch/call
  targets in a region, deduplicated.
- `CMD_PROC_FIND_XREFS_TO` - only instructions that reference a specific
  target address.

### Built-in Keystone assembler (x86-64)
A cross-compiled LLVM-MC Keystone (x86-only, no exceptions / no RTTI, static
~4 MB) is embedded in the payload, exposed via the raw-literal opcode
`0xBDAA0024`. Lets clients assemble asm text into machine code on the console
itself - the on-console equivalent of what Reaper Studio does with its
client-side `keystone.dll` when applying `VarType.ASM` patches.
- Pure userspace - needs no attached process and no `CMD_PROC_AUTH` handshake.
- Request: `u64 base_addr; u32 ks_opt_syntax;` + asm text (NUL not required).
  `ks_opt_syntax` defaults to Intel; pass 1/2/3/4/5 for Intel/ATT/NASM/MASM/GAS.
- Response: `CMD_SUCCESS` + `u32 byte_len; u32 insn_count;` + machine bytes,
  or `CMD_ERROR` + `u32 ks_errno; u32 msg_len;` + Keystone's human-readable error.
- The opcode is deliberately a raw literal (no `CMD_*` macro) so the published
  `CMD_*` set that some clients enumerate stays unchanged.

### Memory scanning
- **Value scan** (`CMD_PROC_SCAN`) - single-pass, 12 value types × 13 compare
  modes (exact, fuzzy, bigger/smaller, between, increased, decreased, changed,
  etc.).
- **Iterative scan session** (`SCAN_START` → `SCAN_COUNT` → `SCAN_GET`) - lets
  clients narrow a result set server-side over many passes.
- **AOB scan** (`CMD_PROC_SCAN_AOB`) - byte patterns with `??` wildcards.
- **Multi-pattern AOB scan** (`CMD_PROC_SCAN_AOB_MULTI`) - many patterns in
  one pass, scanned by region-parallel worker threads.
- **Auth-gated** - the classic value scan, both AOB scans, and the iterative trio
  all require a prior `CMD_PROC_AUTH` handshake.

### Turbo Scan family (v1.3.0, additive + capability-gated)
A faster, opt-in scan path (`0xBDAACC10`-`0xBDAACC17`) that runs alongside the
unchanged classic / iterative / AOB scanners. A client detects it via
`CMD_PROC_TURBOSCAN_CAPS` (`0xBDAACC10`) and falls back to the iterative trio when
it (or a specific engine) is absent. Result format mirrors the iterative scan, so
clients reuse one parser. Raw-literal opcodes (no `CMD_*` macro); see
[PROTOCOL.md](PROTOCOL.md) §2.2 / §7.4.
- **SIMD comparator** (`TSE_SIMD_COMPARE`) - typed exact-match inner loop. The PS4
  is AMD Jaguar (AVX1, **no AVX2**), so it uses a 256-bit YMM load with a dual
  128-bit `PCMPEQD` recombine - byte-identical results to the AVX2 path, without the
  `#UD` a 256-bit integer compare would raise.
- **Server-resident result sets** (`TSE_SERVER_RESIDENT`) - the survivor set can
  live in a per-connection server buffer instead of being re-uploaded each pass;
  rescans refresh each survivor's baseline so "since last scan" deltas work without
  the client holding state. `CMD_PROC_TURBOSCAN_GET` fetches values on demand.
- **Unknown-initial-value scans** (`TSE_SNAPSHOT`) - a server-side value snapshot
  (RAM, or a `/data` file for large regions) drives
  increased/decreased/changed narrowing with no known starting value. By default
  dense snapshots use a bitmap while Simple float snapshots use survivor records
  from the start. The seed **drops all-zero slots**; `TS_SNAPSHOT_INCLUDE_ZEROS` keeps them.
- **Multi-segment scans** (`TSE_SNAPSHOT_SEGMENTS`) - one session can cover a list
  of disjoint regions instead of a single contiguous range, so a scattered
  module/section selection uses the server-side path and never reads the gaps
  between segments.
- **Snapshot storage tuning** (`TSE_SNAPSHOT_CONFIG`, `CMD_PROC_TURBOSCAN_CONFIG`) -
  the client can set the RAM threshold (how large the value store may grow before it
  spills to disk; default 512 MiB) and the spill directory (default `/data`; a USB
  mount can be faster). Spill writes use 16 MiB chunks.
- **Aliasing read engine** (`TSE_ALIASING`, opt-in, default off) - maps the target's
  physical pages into the server's address space via guarded page-table writes so the
  scan reads in place instead of copying. Enabled per request with `TS_USE_ALIASING`;
  always falls back to the normal read path on any guard/verify miss (mdbg is the floor).
- **Parallel compare** (`TSE_PARALLEL_COMPARE`, opt-in, default off) - with
  `TS_PARALLEL_COMPARE` on an aliased exact-match streaming scan, the server splits
  the work across worker threads. For **single-connection** clients; a
  multi-connection client parallelizes by opening more connections instead (don't set
  both - they over-subscribe). Same wire result either way.
- **Rescan aliasing** (`TSE_RESCAN_ALIASING`, opt-in, default off) - with
  `TS_RESCAN_ALIASING` on a `COUNT` rescan, dense (gap-bridged) survivor windows read
  via the aliasing engine instead of mdbg; scattered windows and any alias miss stay
  on mdbg. The survivor set is per-connection, so single-connection by nature.
- **Float policy offload** (`TSE_FLOAT_POLICY`, `TSE_COMPACT_SIMPLE_SNAPSHOT`) - clients may send
  `TS_FLOAT_SIMPLE` plus their exponent-distance threshold so extreme/denormal
  float and double candidates are excluded from survivor membership, later
  narrowing, and streaming/GET transfer. `TS_FLOAT_EXACT` selects numeric IEEE
  equality for exact-value scans. Both policies are applied during streaming START,
  snapshot creation, and every resident or client-driven COUNT rescan. When Simple
  seeds a snapshot, filtering happens before storage: only survivor records are
  written, no raw-slot bitmap is allocated, optional First/Previous values are embedded
  per record, and each later narrow compacts the stream again. The dedicated compact
  capability bit lets clients distinguish this storage guarantee from older payloads
  that advertised float-policy correctness but still used dense snapshot backing.
- **Region classify** (`0xBDAACC16`) - returns every readable region with its cache
  attribute (uncached `PCD` leaf-PTE bit) and a measured read throughput, so the
  client can offer a per-region "exclude uncached/slow" choice. The server never
  drops anything - exclusion is the client's opt-in, user-overridable decision.
- **Cancel** (`0xBDAACC17`) - abort a long scan mid-flight (e.g. an unknown-value
  snapshot over a huge region). Sent from a second connection, since the scanning
  connection is busy streaming; a cancelled snapshot create returns `snapshot_ok=0`.
- **Auth-gated** - every turbo command except `CMD_PROC_TURBOSCAN_CAPS` requires a
  prior `CMD_PROC_AUTH` handshake.

### System UI integration
- **Push notifications** to the user's screen
  (`sceSysUtilSendSystemNotificationWithText`) with arbitrary UTF-8 text -
  handy for status updates from client tools.
- **Print** to the kernel console (dmesg).
- **Reboot** the console.

### Discovery
- A UDP broadcast responder on port `1010` echoes a handshake magic
  (`0xFFFFAAAA`) so clients can find the PS4 on the LAN without hard-coding
  an IP.

### Rest-mode support
- The payload **survives suspend / resume** without needing to be reloaded.
  A supervisory loop around the TCP server polls the network every 2 s: when
  the console drops into rest mode the server exits cleanly, and as soon as
  the network comes back the server restarts and a fresh "online" notification
  fires.
- Clients see a clean disconnect on port 744 when rest mode begins and can
  simply reconnect after wake - there is no state to restore on the PS4 side
  beyond an active debug session (which is torn down on disconnect).
- Backoff is adaptive: 2 s retries for the first ~100 attempts, then 1000 s
  between checks to avoid burning power on a console left unplugged.

### Performance-oriented design
- Non-blocking sockets with `TCP_NODELAY`, `SO_KEEPALIVE`, 64 KiB transfer
  chunks.
- Zydis amalgamation compiled at `-O3 -DNDEBUG` for maximum decode throughput;
  rest of the payload at `-O2` for size.
- Link-time dead stripping (`-ffunction-sections -fdata-sections
  -Wl,--gc-sections`).
- Interrupt packets streamed over a dedicated side channel to avoid blocking
  the command loop.

---

## Architecture

```
┌──────────────────────────────────────────────────────────────┐
│                        ps4debug-ng.bin                       │
│                                                              │
│   ┌───────────────────┐    loads    ┌───────────────────┐    │
│   │   installer.bin   │────────────▶│  kdebugger.elf    │    │
│   │  (self-extracts)  │             │ (kernel module)   │    │
│   └───────────────────┘             └────────┬──────────┘    │
│                                              │ hooks         │
│                                              ▼               │
│                              ┌──────────────────────────┐    │
│                              │   debugger.bin           │    │
│                              │   - TCP server  :744     │    │
│                              │   - debug async :755     │    │
│                              │   - UDP bcast   :1010    │    │
│                              └──────────────────────────┘    │
└──────────────────────────────────────────────────────────────┘
```

Three components, one deliverable:

| File               | Size (approx.) | Role                                                           |
|--------------------|----------------|----------------------------------------------------------------|
| `debugger.bin`     | ~3.0 MB        | Userland command server. Runs as a pid, listens on 744/755. Includes the embedded Keystone assembler + Zydis. |
| `kdebugger.elf`    | ~30 KB         | Kernel module. Provides the `sys_proc_*` / `sys_kern_*` ops.   |
| `installer.bin`    | ~3.1 MB        | Loader that installs the kernel module and starts the server.  |
| **`ps4debug-ng.bin`** | **~3.1 MB** | **The single file you send to the PS4 payload loader.**       |

---

## Network protocol at a glance

| Port  | Proto | Direction   | Purpose                           |
|-------|-------|-------------|-----------------------------------|
| 744   | TCP   | client → PS4| Command server (see below)        |
| 755   | TCP   | PS4 → client| Async debug interrupts (1184 B ea)|
| 1010  | UDP   | bidirectional | Discovery beacon (`0xFFFFAAAA`) |

Every command begins with a 12-byte header:

```c
struct cmd_packet {
    uint32_t magic;      // 0xFFAABBCC
    uint32_t cmd;        // 0xBDAA..., 0xBDBB..., 0xBDCC..., 0xBDDD...
    uint32_t datalen;    // length of request body that follows
};
```

Followed by the command's fixed request struct (if any) and any trailing
variable-length payload. The reply shape is per-command: most replies begin with a
`uint32_t` status word (`CMD_SUCCESS = 0x80000000`, sent **raw** - PS4 does not
bit-swap the status word, unlike PS5), some send two (e.g. `KERN_WRITE`, bulk write,
and the `SET*REGS` / set-FS/GS-base data-phase commands), and the info/version
commands send their data with no status word at all - see
[PROTOCOL.md](PROTOCOL.md) for the exact sequence of each.

**Full protocol specification:** [PROTOCOL.md](PROTOCOL.md) - 68 commands, every
packet struct, every enum, every status code, and the kernel-side syscall
interface. (The old `debugger/PROTOCOL.md` now just points here, so the spec
lives in one place.)

---

## Command coverage

| Namespace     | Count | Examples                                                       |
|---------------|-------|----------------------------------------------------------------|
| Info / ping   | 5     | `VERSION`, `FW_VERSION`, `BRANDING`, `PLATFORM_ID`, `NOP`      |
| Process       | 34    | `READ`, `WRITE`, bulk-write, `MAPS`, `CALL`, `SCAN_*`, Turbo `*` (incl. CANCEL), `DISASM_*`, assemble |
| Debug         | 20    | `ATTACH`, `SET_BREAKPOINT`, `GETREGS`, FS/GS-base, `STEP`, `CONTINUE` |
| Kernel R/W    | 3     | `KERN_BASE`, `KERN_READ`, `KERN_WRITE`                         |
| Console       | 6     | `NOTIFY`, `PRINT`, `REBOOT`, `INFO`, `END`, foreground-app     |
| **Total**     | **68**| The newest commands are dispatched as raw hex literals with no `CMD_*` macro (some clients enumerate the macro set). Excludes 3 dev-only diagnostics stripped from release builds. |

---

## Building

Prerequisites: a Linux host (native or WSL2) with `gcc`, `ar`, `objcopy`,
`elfedit`, and `strip`. No cross-compiler is needed - the payload builds with
the host `gcc` using freestanding flags targeting the PS4's AMD Jaguar CPU
(`-march=btver2 -m64 -mabi=sysv -mcmodel=small -nostdlib -nostartfiles`).

```bash
(cd debugger && make clean)
(cd kdebugger && make clean)
(cd installer && make clean)
./build.sh
```

Output: `ps4debug-ng.bin` at the repo root. Send this file to your PS4's
payload loader (usually `nc`-able on port 9020 or 9021 depending on your
jailbreak flavour). Once loaded, the payload jailbreaks itself, installs the
kernel module, and begins listening on port 744.

You should see a system notification confirming the payload is alive:

```
ps4debug-NG by OSR v1.3.2
Special thanks to golden,
Ctn, SiSTRo, DeathRGH
& Pharaoh2k! ♥
```

(The version line is built from `version.h`, so it tracks the current build automatically.)

---

## Writing your own client

The protocol is deliberately simple - a raw TCP client in any language can
drive it. Example: pinging the server and reading its version string, in
Python:

```python
import socket, struct

PACKET_MAGIC = 0xFFAABBCC
CMD_VERSION  = 0xBD000001

s = socket.create_connection(("IP_ADDRESS", 744))
s.sendall(struct.pack("<III", PACKET_MAGIC, CMD_VERSION, 0))
(length,) = struct.unpack("<I", s.recv(4))
print("server version:", s.recv(length).decode())
```

See [PROTOCOL.md](PROTOCOL.md) for the exact byte layout of
every command, response, and async interrupt packet.

---

## Source layout

```
.
├── build.sh                 # one-command full build
├── ps4debug-ng.bin          # the payload you send to the PS4
│
├── debugger/                # userland TCP server
│   ├── source/              # server, proc, debug, kern, net, console handlers
│   ├── include/             # protocol.h, debug.h, kern.h, ...
│   ├── third_party/zydis/   # Zydis amalgamation (decoder-only)
│   └── PROTOCOL.md          # pointer to the root PROTOCOL.md (canonical spec)
│
├── kdebugger/               # kernel module (installs sys_proc_* / sys_kern_*)
│   └── source/              # elf, hooks, proc, main
│
├── installer/               # payload that loads the above two
│   └── source/              # elf loader, installer entry
│
├── ps4-payload-sdk/         # libPS4 - freestanding runtime for userland
└─── ps4-ksdk/                # libKSDK - kernel-side helpers
```

---

## Vendored dependencies

Everything the payload needs is checked in (under `debugger/third_party/` plus the
two SDK trees), so a clean checkout builds without fetching anything:

- **Keystone** 0.9.2-based fork - the on-console x86-64 assembler (`0xBDAA0024`),
  built x86-only (`-fno-exceptions -fno-rtti`, static).
- **Zydis** amalgamation - decoder-only disassembler (`ZYAN_NO_LIBC`, `-DNDEBUG`).
- **libc++ / libc++abi / libunwind** (`debugger/third_party/cxxrt`) - the C++
  runtime the Keystone C++ translation units link against.
- **libPS4** (`ps4-payload-sdk`) - freestanding userland runtime; **libKSDK**
  (`ps4-ksdk`) - kernel-side helpers for the companion module.

---

## Credits

- **jogolden** - original public `ps4debug` and the wire protocol this project
  indirectly inherits.
- **Ctn & SiSTRo** - authors of `ps4debug v1.1.19`, the implementation whose externally
  observable behavior this project independently reimplements.
- **DeathRGH** - Frame4 author. Inspiration.
- **OSR** (OpenSourcereR) - author of this clean-room rewrite; added the
  Zydis-backed disassembly commands, iterative-scan pipeline, auth gating,
  side-channel interrupt architecture, and multi-FW patch table.
- **Zydis** - x86 disassembler used in decoder-only mode
  (`ZYAN_NO_LIBC`, `-DNDEBUG`). Third-party, unmodified; MIT-licensed.
- **Keystone** - LLVM-MC-based assembler (0.9.2-based fork); cross-compiled here
  for the PS4 payload (x86-only, `-fno-exceptions -fno-rtti`, static).

---

## License

Licensed under the **GNU General Public License v3.0** - see [LICENSE.txt](LICENSE.txt)
for the full text.

In short:
- You may use, study, modify, and redistribute this software freely.
- If you distribute a modified binary, you **must** also make the complete
  corresponding source code available under the same license.
- The software is provided **without warranty** of any kind.

For the original upstream work this project is based on, see golden's
public `ps4debug` repository. Any redistribution of this fork must preserve
attribution to the original author.
