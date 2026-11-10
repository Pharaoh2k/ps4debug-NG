# SPDX-License-Identifier: GPL-3.0-only
#
# Host-side tests for ps4debug-NG. The payload itself only builds inside the
# pinned WSL toolchain (see ../build.sh); these targets run on any dev box.
#
#   make check   - verify every dispatched command has a datalen table entry
#   make fuzz    - build the libFuzzer harness (needs clang)
#   make fuzz-run- build + run the harness briefly

CLANG ?= clang
ROOT  := $(abspath $(CURDIR)/..)
INCS  := -I$(ROOT)/tests/host_include -I$(ROOT)/debugger/include
SRC   := $(ROOT)/debugger/source/cmd_guards.c

.PHONY: check fuzz fuzz-run

check:
	python3 $(ROOT)/tests/check_table.py

fuzz:
	$(CLANG) -g -O1 -fsanitize=fuzzer,address,undefined $(INCS) \
		$(SRC) $(ROOT)/tests/fuzz/fuzz_cmd_guards.c -o fuzz_cmd_guards

fuzz-run: fuzz
	./fuzz_cmd_guards -max_total_time=30
