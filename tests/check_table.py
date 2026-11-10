#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
#
# Verifies that every command reachable from the dispatch switches in
# server.c / proc.c / debug.c / kern.c / console.c has an entry in the
# cmd_guards.c datalen table. A command missing from the table gets min 0
# (unchecked), which is exactly the bug class the table exists to prevent.

import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
PROTOCOL = ROOT / "debugger" / "include" / "protocol.h"
TABLE = ROOT / "debugger" / "source" / "cmd_guards.c"
DISPATCH_FILES = [
    ROOT / "debugger" / "source" / n
    for n in ("server.c", "proc.c", "debug.c", "kern.c", "console.c")
]

CASE_RE = re.compile(r"case\s+(CMD_[A-Z0-9_]+|0x[0-9A-Fa-f]+)u?\s*:")
DEFINE_RE = re.compile(r"#define\s+(CMD_[A-Z0-9_]+)\s+(0x[0-9A-Za-z]+)")
TABLE_RE = re.compile(r"\{\s*(CMD_[A-Z0-9_]+|0x[0-9A-Fa-f]+)u?\s*,")


def fail(msg):
    print(f"FAIL: {msg}")
    sys.exit(1)


def main():
    defines = {}
    for name, value in DEFINE_RE.findall(PROTOCOL.read_text()):
        try:
            defines[name] = int(value, 16)
        except ValueError:
            pass

    def resolve(token):
        if token.startswith("0x"):
            return int(token, 16)
        return defines.get(token)

    table_tokens = TABLE_RE.findall(TABLE.read_text())
    table_cmds = set()
    for tok in table_tokens:
        v = resolve(tok)
        if v is None:
            fail(f"cmd_guards.c references {tok} which is not defined in protocol.h")
        table_cmds.add(v)

    missing = []
    for src in DISPATCH_FILES:
        text = src.read_text()
        # only look at switch bodies (case labels), not comments
        text = re.sub(r"//.*", "", text)
        for tok in CASE_RE.findall(text):
            v = resolve(tok)
            if v is None:
                fail(f"{src.name}: case {tok} not resolvable from protocol.h")
            if v not in table_cmds:
                missing.append(f"{src.name}: {tok} (0x{v:08X})")

    if missing:
        print("Commands missing from the cmd_guards.c datalen table:")
        for m in missing:
            print(f"  {m}")
        fail("add entries to debugger/source/cmd_guards.c")

    print(f"OK: {len(table_cmds)} table entries cover every dispatched command")


if __name__ == "__main__":
    main()
