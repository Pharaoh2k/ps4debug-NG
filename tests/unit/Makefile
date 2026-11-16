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

TESTS	:=	test_debug_port test_capability test_attach_wait test_attach_session

.PHONY: all clean

all: $(TESTS)
	@for t in $(TESTS); do ./$$t || exit 1; done

test_debug_port: test_debug_port.c $(ROOT)/debugger/include/debug_port.h
	$(HOSTCC) $(CFLAGS) -o $@ $<

test_capability: test_capability.c $(ROOT)/debugger/include/capability.h $(ROOT)/version.h
	$(HOSTCC) $(CFLAGS) -DSERVER_C_PATH='"$(ROOT)/debugger/source/server.c"' -o $@ $<

test_attach_wait: test_attach_wait.c $(ROOT)/debugger/include/attach_wait.h
	$(HOSTCC) $(CFLAGS) -o $@ $<

# Builds the real debug.c against the libPS4 headers, with a fake kernel.
# The -Wno-* flags only cover warnings that debug.c and the SDK headers
# already produce on the host.
SESSION_CFLAGS := -O1 -std=gnu11 -Wall -Wextra -Werror -I$(ROOT)/ps4-payload-sdk/libPS4/include \
	-iquote $(ROOT)/debugger/include -iquote $(ROOT) -Wno-unused-parameter \
	-Wno-ignored-qualifiers -Wno-builtin-declaration-mismatch -Wno-packed-not-aligned \
	-Wno-stringop-overread -Wno-array-bounds

test_attach_session: test_attach_session.c $(ROOT)/debugger/source/debug.c $(ROOT)/debugger/include/attach_wait.h
	$(HOSTCC) $(SESSION_CFLAGS) -o $@ $<

clean:
	rm -f $(TESTS)
