# Atari 7800 (prosystem-go, C) — standalone core (see cores/_template/Makefile
# and Core/Src/porting/core_common/CLAUDE.md).
#
# ROM extensions: .a78 / .bin (and .lzma on non-SD compressed builds).

CORE_NAME  := a7800
CORE_ENTRY := app_main_a7800

CORE_PROSYSTEM := ../../external/prosystem-go
CORE_PORTING_A7800 := ../../Core/Src/porting/a7800

CORE_C_SOURCES := \
$(CORE_PROSYSTEM)/core/Bios.c \
$(CORE_PROSYSTEM)/core/Cartridge.c \
$(CORE_PROSYSTEM)/core/Database.c \
$(CORE_PROSYSTEM)/core/Hash.c \
$(CORE_PROSYSTEM)/core/Maria.c \
$(CORE_PROSYSTEM)/core/Memory.c \
$(CORE_PROSYSTEM)/core/Palette.c \
$(CORE_PROSYSTEM)/core/Pokey.c \
$(CORE_PROSYSTEM)/core/ProSystem.c \
$(CORE_PROSYSTEM)/core/Region.c \
$(CORE_PROSYSTEM)/core/Riot.c \
$(CORE_PROSYSTEM)/core/Sally.c \
$(CORE_PROSYSTEM)/core/Tia.c \
$(CORE_PORTING_A7800)/main_a7800.c

CORE_C_INCLUDES := \
-I$(CORE_PROSYSTEM)/core \
-I../../Core/Inc/porting/a7800 \
-I../../Core/Src/porting/lib/lzma

CORE_C_DEFS := -DTARGET_GNW

include ../_template/Makefile

PACKED_BIN := ../a7800.bin

.PHONY: pack
pack: $(TARGET_BIN)
	python3 ../../tools/pack_core.py \
		--elf $(TARGET_ELF) --bin $(TARGET_BIN) \
		--system name="Atari 7800",dirname=a7800,ext="a78 bin",parse=rom,pad_logo=../../Core/Src/retro-go/rg_logos.c:pad_a7800,header_logo=../../Core/Src/retro-go/rg_logos.c:header_a7800 \
		--out $(PACKED_BIN)

all: pack

clean::
	rm -f $(PACKED_BIN)
