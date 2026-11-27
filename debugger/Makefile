# SPDX-License-Identifier: GPL-3.0-only

LIBPS4	:=	../ps4-payload-sdk/libPS4
ZYDIR	:=	third_party/zydis
CXXRT	:=	third_party/cxxrt
KSDIR	:=	third_party/keystone
LZ4DIR	:=	third_party/lz4

CC		:=	gcc
CXX		:=	clang++-18
AS		:=	gcc
OBJCOPY	:=	objcopy
ODIR	:=	build
SDIR	:=	source
IDIRS	:=	-I$(LIBPS4)/include -I. -Iinclude -I$(ZYDIR) -I$(KSDIR)/include
LDIRS	:=	-L$(LIBPS4) -L. -Llib -L$(CXXRT)/lib -L$(KSDIR)/lib

ZYDEFS	:=	-DZYAN_NO_LIBC

# LZ4 without libc: the three memory helpers come from the compiler builtins.
# Only lz4.c and read_lz4.c include lz4.h (see the target-specific CFLAGS below).
LZ4DEFS	:=	-DLZ4_FREESTANDING=1 -DLZ4_memcpy=__builtin_memcpy \
		-DLZ4_memmove=__builtin_memmove -DLZ4_memset=__builtin_memset

ARCHFLAGS := -fPIE -nostartfiles -nostdlib -masm=intel -march=btver2 -mtune=btver2 -m64 -mabi=sysv -mcmodel=small -ffunction-sections -fdata-sections

CFLAGS	:=	$(IDIRS) -O2 -std=c11 -fno-builtin $(ARCHFLAGS) -Wall $(ZYDEFS)
SFLAGS	:=	-nostartfiles -nostdlib -march=btver2 -mtune=btver2

CXXFLAGS := -target x86_64-pc-freebsd12 -nostdinc++ -isystem $(CXXRT)/include/c++/v1 -isystem $(CXXRT)/cinc \
            -include $(CXXRT)/cinc/errno.h -O2 -std=c++17 -fno-exceptions -fno-rtti -fPIC \
            -ffunction-sections -fdata-sections -mcmodel=small -march=btver2 -w

CXXRTLIBS := -Wl,--start-group -lkeystone -lc++ -lc++abi -lunwind -Wl,--end-group

LFLAGS	:=	$(LDIRS) -pie -Xlinker -T $(LIBPS4)/linker.x -Wl,--build-id=none -Wl,--gc-sections \
		-Wl,--no-dynamic-linker -Wl,-z,noseparate-code -Wl,-z,norelro -Wl,-z,max-page-size=0x1000

CFILES	:=	$(wildcard $(SDIR)/*.c)
CXXFILES :=	$(wildcard $(SDIR)/*.cpp)
SFILES	:=	$(wildcard $(SDIR)/*.s)
OBJS	:=	$(patsubst $(SDIR)/%.c, $(ODIR)/%.o, $(CFILES)) \
		$(patsubst $(SDIR)/%.cpp, $(ODIR)/%.o, $(CXXFILES)) \
		$(patsubst $(SDIR)/%.s, $(ODIR)/%.o, $(SFILES)) $(ODIR)/Zydis.o $(ODIR)/lz4.o

LIBS	:= -lPS4

# Header dependencies: each C/C++ compile also writes build/<name>.d (-MMD),
# with an empty target per header (-MP) so a removed header does not break the
# build. Every object also depends on its own .d: an object without one (built
# before tracking existed) is rebuilt once instead of being trusted.
DEPFLAGS := -MMD -MP
DEPS	:= $(OBJS:.o=.d)

TARGET = debugger.bin

# Link exactly OBJS (sorted like the old build/*.o glob), so objects left
# in build/ by another branch or a removed source are not linked in.
$(TARGET): $(ODIR) $(OBJS)
	$(CC) $(LIBPS4)/crt0.s $(sort $(OBJS)) -o temp.t $(CFLAGS) $(LFLAGS) $(LIBS) $(CXXRTLIBS)
	$(OBJCOPY) -O binary temp.t $(TARGET)
	rm -f temp.t

$(ODIR)/%.o: $(SDIR)/%.c $(ODIR)/%.d
	$(CC) -c -o $@ $< $(CFLAGS) $(DEPFLAGS)

$(ODIR)/%.o: $(SDIR)/%.cpp $(ODIR)/%.d
	$(CXX) -c -o $@ $< $(CXXFLAGS) $(DEPFLAGS)

$(ODIR)/%.o: $(SDIR)/%.s
	$(AS) -c -o $@ $< $(SFLAGS)

$(ODIR)/Zydis.o: $(ZYDIR)/Zydis.c $(ZYDIR)/Zydis.h $(ODIR)/Zydis.d | $(ODIR)
	$(CC) -c -o $@ $< $(CFLAGS) -O3 -DNDEBUG -w $(DEPFLAGS)

$(ODIR)/lz4.o $(ODIR)/read_lz4.o: CFLAGS += -I$(LZ4DIR) $(LZ4DEFS)

$(ODIR)/lz4.o: $(LZ4DIR)/lz4.c $(LZ4DIR)/lz4.h $(ODIR)/lz4.d | $(ODIR)
	$(CC) -c -o $@ $< $(CFLAGS) $(DEPFLAGS)

# A missing .d only means its object must be rebuilt (see DEPFLAGS).
$(DEPS):

$(ODIR):
	@mkdir $@

.PHONY: clean test

test:
	$(MAKE) -C ../tests/unit

clean:
	rm -f $(TARGET) $(ODIR)/*.o $(ODIR)/*.d

-include $(wildcard $(DEPS))
