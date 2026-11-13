# SPDX-License-Identifier: GPL-3.0-only
#
# Host-side unit tests (built with the host compiler, not the PS4 toolchain).
#
#   make -C tests/unit         - build and run all unit tests
#   make -C debugger test      - same, from the debugger directory

HOSTCC	?=	cc
ROOT	:=	$(abspath $(CURDIR)/../..)
INCS	:=	-iquote $(ROOT)/debugger/include -iquote $(ROOT)
CFLAGS	:=	-O2 -std=c11 -Wall -Wextra -Werror $(INCS)

TESTS	:=	test_debug_port test_capability

.PHONY: all clean

all: $(TESTS)
	@for t in $(TESTS); do ./$$t || exit 1; done

test_debug_port: test_debug_port.c $(ROOT)/debugger/include/debug_port.h
	$(HOSTCC) $(CFLAGS) -o $@ $<

test_capability: test_capability.c $(ROOT)/debugger/include/capability.h $(ROOT)/version.h
	$(HOSTCC) $(CFLAGS) -DSERVER_C_PATH='"$(ROOT)/debugger/source/server.c"' -o $@ $<

clean:
	rm -f $(TESTS)
