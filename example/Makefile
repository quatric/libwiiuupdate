# Builds example.rpx against the library in the folder above (make it first).
ifeq ($(strip $(DEVKITPRO)),)
$(error "Please set DEVKITPRO in your environment. export DEVKITPRO=<path to>/devkitpro")
endif

include $(DEVKITPRO)/wut/share/wut_rules

TARGET  := example
DEPSDIR := $(CURDIR)
LIBDIR  := $(CURDIR)/..
CFLAGS  := -O2 -Wall $(MACHDEP) -I$(LIBDIR)/include -I$(WUT_ROOT)/include -D__WIIU__ -D__WUT__
LDFLAGS := $(ARCH) $(RPXSPECS)
LIBS    := -L$(LIBDIR)/lib -L$(WUT_ROOT)/lib -lwiiuupdate -lwut -lm

all: $(TARGET).rpx

$(TARGET).elf: main.o $(LIBDIR)/lib/libwiiuupdate.a
	$(CC) $(LDFLAGS) main.o $(LIBS) -o $@

$(LIBDIR)/lib/libwiiuupdate.a:
	@$(MAKE) --no-print-directory -C $(LIBDIR)

clean:
	rm -f *.o *.d *.elf *.rpx

.PHONY: all clean
