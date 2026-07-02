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

Note: A mirror of this repo is available on: https://git.slowb.ro/OpenSourcereR/ps4debug-NG

---

## Supported firmwares

36 firmware versions across the 5.05-13.50 range. Each has a dedicated kernel
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
| 13.xx | 13.00, 13.02, 13.04, 13.50                  |

Clients can read the running FW with `CMD_FW_VERSION` (returns a `uint16_t` in
`major*100 + minor` form - e.g. `0x1F4 = 500 = 5.00`).

**13.x support:** 13.00 / 13.02 / 13.04 / 13.50 run the full command set, including
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
- **Read and write target memory** in 64 KiB streamed chunks.
- **Bulk write / freeze** (`0xBDAACC04`) - apply many `{address, length, bytes}`
  writes in one exchange (the write counterpart to bulk-read), collapsing a
  freeze / multi-poke loop into a single round-trip with an optional per-entry
  status array. Raw-literal opcode.
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
- **Turbo Scan** (`0xBDAACC10` - `0xBDAACC17`) - a high-throughput scan engine
  with a SIMD comparator, a page-table **aliasing** read path (reads target
  physical pages via the server's own VA instead of per-chunk copies),
  **server-resident** survivor sets (narrowing passes never ship the full
  address list), and worker-thread **parallel** compare. Covers both known-value
  scanning (Phase A) and snapshot / unknown-initial-value scanning (Phase B).
  Capabilities are negotiated via a `CAPS` query; all flags are opt-in per
  request. A long scan can be **cancelled mid-flight** (`0xBDAACC17`, sent from a
  second connection) - handy for aborting a snapshot / unknown-value scan over a
  huge region. Raw-literal opcodes; see [PROTOCOL.md](PROTOCOL.md) §2.2 / §7.4.
- **Auth-gated** - all scan commands (classic, AOB, and Turbo except its `CAPS`
  probe) require a prior `CMD_PROC_AUTH` handshake.

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

Followed by the command's fixed request struct (if any), any trailing
variable-length payload, and a `uint32_t` status code reply.

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
./build.sh           # incremental build
./build.sh clean     # full clean + rebuild
```

Output: `ps4debug-ng.bin` at the repo root. Send this file to your PS4's
payload loader (usually `nc`-able on port 9020 or 9021 depending on your
jailbreak flavour). Once loaded, the payload jailbreaks itself, installs the
kernel module, and begins listening on port 744.

You should see a system notification confirming the payload is alive:

```
ps4debug-NG by OSR v1.3.0
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
│   └── PROTOCOL.md          # complete wire-protocol reference
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