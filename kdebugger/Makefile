# SPDX-License-Identifier: GPL-3.0-only

KSDK := ../ps4-ksdk/

LFILE = ./kdebugger.x

CC		:=	gcc
AS		:=	gcc
OBJCOPY	:=	objcopy
ODIR	:=	build
SDIR	:=	source
IDIRS	:=	-I$(KSDK)/include -I. -Iinclude
LDIRS	:=	-L$(KSDK) -L. -Llib
CFLAGS	:=	$(IDIRS) -O3 -std=gnu11 -fno-builtin -fno-exceptions -fno-asynchronous-unwind-tables -nostartfiles -nostdlib -Wall -masm=intel -march=btver2 -mtune=btver2 -m64 -mabi=sysv -mcmodel=small -fPIE
SFLAGS	:=	-nostartfiles -nostdlib -masm=intel -march=btver2 -mtune=btver2 -m64 -mabi=sysv -mcmodel=small
LFLAGS	:=	$(LDIRS) -Xlinker -T $(LFILE) -Wl,--build-id=none -pie
CFILES	:=	$(wildcard $(SDIR)/*.c)
SFILES	:=	$(wildcard $(SDIR)/*.s)
OBJS	:=	$(patsubst $(SDIR)/%.c, $(ODIR)/%.o, $(CFILES)) $(patsubst $(SDIR)/%.s, $(ODIR)/%.o, $(SFILES))

LIBS	:= -lKSDK

# Header dependencies: each C compile also writes build/<name>.d (-MMD), with
# an empty target per header (-MP) so a removed header does not break the
# build. Every object also depends on its own .d: an object without one (built
# before tracking existed) is rebuilt once instead of being trusted.
DEPFLAGS := -MMD -MP
DEPS	:= $(OBJS:.o=.d)

TARGET = kdebugger.elf

# Link exactly OBJS (sorted like the old build/*.o glob), so objects left
# in build/ by another branch or a removed source are not linked in.
$(TARGET): $(ODIR) $(OBJS)
	$(CC) crt0.s $(sort $(OBJS)) -o $(TARGET) $(CFLAGS) $(LFLAGS) $(LIBS)
	elfedit --output-type=DYN $(TARGET)
	strip $(TARGET)

$(ODIR)/%.o: $(SDIR)/%.c $(ODIR)/%.d
	$(CC) -c -o $@ $< $(CFLAGS) $(DEPFLAGS)

$(ODIR)/%.o: $(SDIR)/%.s
	$(AS) -c -o $@ $< $(SFLAGS)

# A missing .d only means its object must be rebuilt (see DEPFLAGS).
$(DEPS):

$(ODIR):
	@mkdir $@

.PHONY: clean

clean:
	rm -f $(TARGET) $(ODIR)/*.o $(ODIR)/*.d

-include $(wildcard $(DEPS))
