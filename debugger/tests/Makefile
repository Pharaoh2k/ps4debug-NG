# SPDX-License-Identifier: GPL-3.0-only
#
# Host-side unit tests (built with the host compiler, not the PS4 toolchain).

HOSTCC	?=	cc
CFLAGS	:=	-O2 -std=c11 -Wall -Wextra -Werror

TESTS	:=	test_debug_port test_capability

.PHONY: all clean

all: $(TESTS)
	@for t in $(TESTS); do ./$$t || exit 1; done

test_debug_port: test_debug_port.c ../include/debug_port.h
	$(HOSTCC) $(CFLAGS) -o $@ $<

test_capability: test_capability.c ../include/capability.h ../../version.h
	$(HOSTCC) $(CFLAGS) -o $@ $<

clean:
	rm -f $(TESTS)
