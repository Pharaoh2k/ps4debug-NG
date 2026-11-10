// SPDX-License-Identifier: GPL-3.0-only
//
// Central packet->datalen requirements, enforced in handle_client() before
// any dispatch. Every entry here must stay in sync with the switch statements
// in server.c / proc.c / debug.c / kern.c / console.c; tests/check_table.sh
// verifies the sync in CI.

#include "cmd_guards.h"

struct cmd_datalen_req {
    uint32_t cmd;
    uint32_t min_datalen;
};

static const struct cmd_datalen_req g_cmd_datalen_reqs[] = {
    // process commands
    { CMD_PROC_LIST,              0 },
    { CMD_PROC_READ,              CMD_PROC_READ_PACKET_SIZE },
    { CMD_PROC_WRITE,             CMD_PROC_WRITE_PACKET_SIZE },
    { CMD_PROC_MAPS,              CMD_PROC_MAPS_PACKET_SIZE },
    { CMD_PROC_INTALL,            CMD_PROC_INSTALL_PACKET_SIZE },
    { CMD_PROC_CALL,              CMD_PROC_CALL_PACKET_SIZE },
    { CMD_PROC_ELF,               CMD_PROC_ELF_PACKET_SIZE },
    { CMD_PROC_PROTECT,           CMD_PROC_PROTECT_PACKET_SIZE },
    { CMD_PROC_SCAN,              CMD_PROC_SCAN_PACKET_SIZE },
    { CMD_PROC_INFO,              CMD_PROC_INFO_PACKET_SIZE },
    { CMD_PROC_ALLOC,             CMD_PROC_ALLOC_PACKET_SIZE },
    { CMD_PROC_FREE,              CMD_PROC_FREE_PACKET_SIZE },
    { CMD_PROC_ALLOC_HINTED,      CMD_PROC_ALLOC_HINTED_PACKET_SIZE },
    { CMD_PROC_ELF_RPC,           CMD_PROC_ELF_RPC_PACKET_SIZE },
    { CMD_PROC_DISASM_REGION,     CMD_PROC_DISASM_PACKET_SIZE },
    { CMD_PROC_EXTRACT_CODE_XREFS, CMD_PROC_DISASM_PACKET_SIZE },
    { CMD_PROC_FIND_XREFS_TO,     CMD_PROC_XREFS_TO_PACKET_SIZE },
    { CMD_PROC_READ_STACK,        CMD_PROC_READ_STACK_PACKET_SIZE },
    { 0xBDAA0024u,                CMD_PROC_ASSEMBLE_HDR_SIZE }, // CMD_PROC_ASSEMBLE
    { CMD_PROC_SCAN_AOB,          CMD_PROC_SCAN_AOB_PACKET_SIZE },
    { CMD_PROC_SCAN_AOB_MULTI,    CMD_PROC_SCAN_AOB_MULTI_PACKET_SIZE },
    { CMD_PROC_AUTH,              CMD_PROC_AUTH_PACKET_SIZE },
    { CMD_PROC_SCAN_START,        CMD_PROC_SCAN_START_PACKET_SIZE },
    { CMD_PROC_SCAN_COUNT,        CMD_PROC_SCAN_COUNT_PACKET_SIZE },
    { CMD_PROC_SCAN_GET,          CMD_PROC_SCAN_GET_PACKET_SIZE },
    { CMD_PROC_MODULES,           CMD_PROC_MAPS_PACKET_SIZE },
    { CMD_PROC_SNAPSHOT,          CMD_PROC_SNAPSHOT_PACKET_SIZE },
    { 0xBDAACC04u,                CMD_PROC_WRITE_MULTI_PACKET_SIZE }, // CMD_PROC_WRITE_MULTI
    // turboscan commands; payloads validated inside scan_turbo.c
    { 0xBDAACC10u,                0 }, // CMD_PROC_TURBOSCAN_CAPS
    { 0xBDAACC11u,                0 }, // CMD_PROC_TURBOSCAN_START
    { 0xBDAACC12u,                0 }, // CMD_PROC_TURBOSCAN_COUNT
    { 0xBDAACC13u,                0 }, // CMD_PROC_TURBOSCAN_GET
    { 0xBDAACC14u,                0 }, // CMD_PROC_TURBOSCAN_END
    { 0xBDAACC15u,                0 }, // CMD_PROC_TURBOSCAN_CONFIG
    { 0xBDAACC16u,                0 }, // CMD_PROC_TURBOSCAN_REGIONS
    { 0xBDAACC17u,                0 }, // CMD_PROC_TURBOSCAN_CANCEL

    // debug commands
    { CMD_DEBUG_PROCESS_STOP,     8 }, // reads pid + state byte
    { CMD_DEBUG_ATTACH,           sizeof(uint32_t) }, // pid; client_ip optional
    { CMD_DEBUG_DETACH,           0 },
    { CMD_DEBUG_SET_BREAKPOINT,   CMD_DEBUG_BREAKPT_PACKET_SIZE },
    { CMD_DEBUG_SET_WATCHPOINT,   CMD_DEBUG_WATCHPT_PACKET_SIZE },
    { CMD_DEBUG_GET_THREAD_LIST,  0 },
    { CMD_DEBUG_SUSPEND_THREAD,   CMD_DEBUG_STOPTHR_PACKET_SIZE },
    { CMD_DEBUG_RESUME_THREAD,    CMD_DEBUG_RESUMETHR_PACKET_SIZE },
    { CMD_DEBUG_GETREGS,          CMD_DEBUG_GETREGS_PACKET_SIZE },
    { CMD_DEBUG_SETREGS,          CMD_DEBUG_SETREGS_PACKET_SIZE },
    { CMD_DEBUG_GETFPREGS,        CMD_DEBUG_GETREGS_PACKET_SIZE },
    { CMD_DEBUG_SETFPREGS,        CMD_DEBUG_SETREGS_PACKET_SIZE },
    { CMD_DEBUG_GETDBREGS,        CMD_DEBUG_GETREGS_PACKET_SIZE },
    { CMD_DEBUG_SETDBREGS,        CMD_DEBUG_SETREGS_PACKET_SIZE },
    { 0xBDBB000Eu,                CMD_DEBUG_GETREGS_PACKET_SIZE }, // CMD_DEBUG_GETFSGSBASE
    { 0xBDBB000Fu,                CMD_DEBUG_SETREGS_PACKET_SIZE }, // CMD_DEBUG_SETFSGSBASE
    { CMD_DEBUG_CONTINUE,         CMD_DEBUG_STOPGO_PACKET_SIZE },
    { CMD_DEBUG_THREAD_INFO,      CMD_DEBUG_THRINFO_PACKET_SIZE },
    { CMD_DEBUG_STEP,             0 },
    { CMD_DEBUG_STEP_THREAD,      0 }, // payload optional

    // kernel commands
    { CMD_KERN_BASE,              0 },
    { CMD_KERN_READ,              CMD_KERN_READ_PACKET_SIZE },
    { CMD_KERN_WRITE,             CMD_KERN_WRITE_PACKET_SIZE },

    // console commands
    { CMD_CONSOLE_REBOOT,         0 },
    { CMD_CONSOLE_END,            0 },
    { CMD_CONSOLE_PRINT,          sizeof(uint32_t) }, // length prefix
    { CMD_CONSOLE_NOTIFY,         CMD_CONSOLE_NOTIFY_PACKET_SIZE },
    { CMD_CONSOLE_INFO,           0 },
    { 0xBDDD0006u,                0 }, // CMD_CONSOLE_FOREGROUND_APP

    // server-direct commands (handled in cmd_handler before sub-dispatch)
    { CMD_VERSION,                0 },
    { CMD_FW_VERSION,             0 },
    { CMD_BRANDING,               0 },
    { CMD_PLATFORM_ID,            0 },
    { CMD_PROC_NOP,               0 },
};

uint32_t cmd_min_datalen(uint32_t cmd) {
    for (unsigned int i = 0; i < sizeof(g_cmd_datalen_reqs) / sizeof(g_cmd_datalen_reqs[0]); i++) {
        if (g_cmd_datalen_reqs[i].cmd == cmd) {
            return g_cmd_datalen_reqs[i].min_datalen;
        }
    }
    return 0;
}
