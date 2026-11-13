# SPDX-License-Identifier: GPL-3.0-only

LIBPS4	:=	../ps4-payload-sdk/libPS4
ZYDIR	:=	third_party/zydis
CXXRT	:=	third_party/cxxrt
KSDIR	:=	third_party/keystone

CC		:=	gcc
CXX		:=	clang++-18
AS		:=	gcc
OBJCOPY	:=	objcopy
ODIR	:=	build
SDIR	:=	source
IDIRS	:=	-I$(LIBPS4)/include -I. -Iinclude -I$(ZYDIR) -I$(KSDIR)/include
LDIRS	:=	-L$(LIBPS4) -L. -Llib -L$(CXXRT)/lib -L$(KSDIR)/lib

ZYDEFS	:=	-DZYAN_NO_LIBC

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
		$(patsubst $(SDIR)/%.s, $(ODIR)/%.o, $(SFILES)) $(ODIR)/Zydis.o

LIBS	:= -lPS4

TARGET = debugger.bin

$(TARGET): $(ODIR) $(OBJS)
	$(CC) $(LIBPS4)/crt0.s $(ODIR)/*.o -o temp.t $(CFLAGS) $(LFLAGS) $(LIBS) $(CXXRTLIBS)
	$(OBJCOPY) -O binary temp.t $(TARGET)
	rm -f temp.t

$(ODIR)/%.o: $(SDIR)/%.c
	$(CC) -c -o $@ $< $(CFLAGS)

$(ODIR)/%.o: $(SDIR)/%.cpp
	$(CXX) -c -o $@ $< $(CXXFLAGS)

$(ODIR)/%.o: $(SDIR)/%.s
	$(AS) -c -o $@ $< $(SFLAGS)

$(ODIR)/Zydis.o: $(ZYDIR)/Zydis.c $(ZYDIR)/Zydis.h | $(ODIR)
	$(CC) -c -o $@ $< $(CFLAGS) -O3 -DNDEBUG -w

$(ODIR):
	@mkdir $@

.PHONY: clean test

test:
	$(MAKE) -C ../tests/unit

clean:
	rm -f $(TARGET) $(ODIR)/*.o
