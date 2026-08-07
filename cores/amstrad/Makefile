# Amstrad CPC (caprice32-go, C) — standalone core (see cores/_template/Makefile
# and Core/Src/porting/core_common/CLAUDE.md).
#
# Disk images: .dsk / .cdk (LZMA-compressed disk; inflate via ABI). BIOS is
# embedded in Caprice under TARGET_GNW.

CORE_NAME  := amstrad
CORE_ENTRY := app_main_amstrad

CORE_AMSTRAD := ../../external/caprice32-go
CORE_PORTING_AMSTRAD := ../../Core/Src/porting/amstrad

CORE_C_SOURCES := \
$(CORE_AMSTRAD)/cap32/cap32.c \
$(CORE_AMSTRAD)/cap32/crtc.c \
$(CORE_AMSTRAD)/cap32/fdc.c \
$(CORE_AMSTRAD)/cap32/kbdauto.c \
$(CORE_AMSTRAD)/cap32/psg.c \
$(CORE_AMSTRAD)/cap32/slots.c \
$(CORE_AMSTRAD)/cap32/cap32_z80.c \
$(CORE_PORTING_AMSTRAD)/main_amstrad.c \
$(CORE_PORTING_AMSTRAD)/amstrad_catalog.c \
$(CORE_PORTING_AMSTRAD)/amstrad_format.c \
$(CORE_PORTING_AMSTRAD)/amstrad_loader.c \
$(CORE_PORTING_AMSTRAD)/amstrad_video8bpp.c

CORE_C_INCLUDES := \
-I$(CORE_AMSTRAD)/cap32 \
-I../../Core/Inc/porting/amstrad \
-I../../Core/Src/porting/lib/lzma

CORE_C_DEFS := -DTARGET_GNW

# psg.c Calculate_Level_Tables uses exp()/log() at init.
CORE_LDLIBS := -lm

include ../_template/Makefile

PACKED_BIN := ../amstrad.bin

.PHONY: pack
pack: $(TARGET_BIN)
	python3 ../../tools/pack_core.py \
		--elf $(TARGET_ELF) --bin $(TARGET_BIN) \
		--system name="Amstrad CPC",dirname=amstrad,ext="dsk cdk",parse=rom,pad_logo=../../Core/Src/retro-go/rg_logos.c:pad_amstrad,header_logo=../../Core/Src/retro-go/rg_logos.c:header_amstrad \
		--out $(PACKED_BIN)

all: pack

clean::
	rm -f $(PACKED_BIN)
