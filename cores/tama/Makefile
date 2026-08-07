# Tamagotchi P1 (tamalib, C) — standalone core (see cores/_template/Makefile
# and Core/Src/porting/core_common/CLAUDE.md).
#
# ROMs live under roms/tama/ (.b). Packed blob is tama.bin (see
# scripts/sd_cores_pack.py).

CORE_NAME  := tama
CORE_ENTRY := app_main_tama

CORE_TAMA := ../../external/tamalib
CORE_PORTING_TAMA := ../../Core/Src/porting/tama

CORE_C_SOURCES := \
$(CORE_TAMA)/tamalib_cpu.c \
$(CORE_TAMA)/tamalib_hw.c \
$(CORE_TAMA)/tamalib.c \
$(CORE_PORTING_TAMA)/state_tama.c \
$(CORE_PORTING_TAMA)/main_tama.c \
../../Core/Src/retro-go/sha256.c

CORE_C_INCLUDES := \
-I$(CORE_TAMA) \
-I../../Core/Inc/porting/tama

include ../_template/Makefile

PACKED_BIN := ../tama.bin

.PHONY: pack
pack: $(TARGET_BIN)
	python3 ../../tools/pack_core.py \
		--elf $(TARGET_ELF) --bin $(TARGET_BIN) \
		--system name="Tamagotchi",dirname=tama,ext=b,parse=rom,pad_logo=../../Core/Src/retro-go/rg_logos.c:pad_tama,header_logo=../../Core/Src/retro-go/rg_logos.c:header_tama \
		--out $(PACKED_BIN)

all: pack

clean::
	rm -f $(PACKED_BIN)
