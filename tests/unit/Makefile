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

TESTS	:=	test_debug_port test_capability test_attach_wait test_attach_session test_read_lz4 \
		test_read_lz4_handle

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

# The payload's own read_lz4.c and the vendored LZ4, built with the payload's
# LZ4 flags (see debugger/Makefile).
LZ4DIR	:=	$(ROOT)/debugger/third_party/lz4
LZ4DEFS	:=	-DLZ4_FREESTANDING=1 -DLZ4_memcpy=__builtin_memcpy \
	-DLZ4_memmove=__builtin_memmove -DLZ4_memset=__builtin_memset

test_read_lz4: test_read_lz4.c $(ROOT)/debugger/source/read_lz4.c $(ROOT)/debugger/include/read_lz4.h \
		$(LZ4DIR)/lz4.c $(LZ4DIR)/lz4.h $(ROOT)/debugger/source/proc.c
	$(HOSTCC) $(CFLAGS) -I$(LZ4DIR) $(LZ4DEFS) -DPROC_C_PATH='"$(ROOT)/debugger/source/proc.c"' \
		-o $@ $< $(ROOT)/debugger/source/read_lz4.c $(LZ4DIR)/lz4.c

# The real handler on the SDK headers (as test_attach_session), linked with
# read_lz4.c and LZ4 built without them.
test_read_lz4_handle: test_read_lz4_handle.c $(ROOT)/debugger/source/proc_read_lz4.c \
		$(ROOT)/debugger/source/read_lz4.c $(ROOT)/debugger/include/read_lz4.h $(LZ4DIR)/lz4.c $(LZ4DIR)/lz4.h
	$(HOSTCC) $(SESSION_CFLAGS) -c -o $@.o $<
	$(HOSTCC) $(CFLAGS) -I$(LZ4DIR) $(LZ4DEFS) -c -o $@.codec.o $(ROOT)/debugger/source/read_lz4.c
	$(HOSTCC) $(CFLAGS) -I$(LZ4DIR) $(LZ4DEFS) -c -o $@.lz4.o $(LZ4DIR)/lz4.c
	$(HOSTCC) -o $@ $@.o $@.codec.o $@.lz4.o
	rm -f $@.o $@.codec.o $@.lz4.o

clean:
	rm -f $(TESTS)
