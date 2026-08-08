# Pokémon Mini (PokeMini-go, C) — standalone core (see cores/_template/Makefile
# and Core/Src/porting/core_common/CLAUDE.md).
#
# ROMs live under roms/mini/ (.min). Packed blob is pkmini.bin (see
# scripts/sd_cores_pack.py). Optional BIOS: /bios/mini/bios.min.

CORE_NAME  := pkmini
CORE_ENTRY := app_main_pkmini

CORE_PKMINI := ../../external/PokeMini-go
CORE_PORTING_PKMINI := ../../Core/Src/porting/pkmini

CORE_C_SOURCES := \
$(CORE_PKMINI)/freebios/freebios.c \
$(CORE_PKMINI)/source/CommandLine.c \
$(CORE_PKMINI)/source/Hardware.c \
$(CORE_PKMINI)/source/Joystick.c \
$(CORE_PKMINI)/source/MinxAudio.c \
$(CORE_PKMINI)/source/MinxColorPRC.c \
$(CORE_PKMINI)/source/MinxCPU_CE.c \
$(CORE_PKMINI)/source/MinxCPU_CF.c \
$(CORE_PKMINI)/source/MinxCPU_SP.c \
$(CORE_PKMINI)/source/MinxCPU_XX.c \
$(CORE_PKMINI)/source/MinxCPU.c \
$(CORE_PKMINI)/source/MinxIO.c \
$(CORE_PKMINI)/source/MinxIRQ.c \
$(CORE_PKMINI)/source/MinxLCD.c \
$(CORE_PKMINI)/source/MinxPRC.c \
$(CORE_PKMINI)/source/MinxTimers.c \
$(CORE_PKMINI)/source/Multicart.c \
$(CORE_PKMINI)/source/PMCommon.c \
$(CORE_PKMINI)/source/PokeMini.c \
$(CORE_PKMINI)/source/Video_x3.c \
$(CORE_PKMINI)/source/Video.c \
$(CORE_PKMINI)/resource/PokeMini_ColorPal.c \
$(CORE_PORTING_PKMINI)/main_pkmini.c \
$(CORE_PORTING_PKMINI)/pkmini_i18n.c

CORE_C_INCLUDES := \
-I$(CORE_PKMINI)/source \
-I$(CORE_PKMINI)/resource \
-I$(CORE_PKMINI)/freebios \
-I$(CORE_PKMINI)/libretro/libretro-common/include \
-I../../Core/Inc/porting/pkmini

CORE_C_DEFS := -DTARGET_GNW

include ../_template/Makefile

PACKED_BIN := ../pkmini.bin

.PHONY: pack
pack: $(TARGET_BIN)
	python3 ../../tools/pack_core.py \
		--elf $(TARGET_ELF) --bin $(TARGET_BIN) \
		--system name="Pokémon Mini",dirname=mini,ext=min,parse=rom,pad_logo=../../Core/Src/retro-go/rg_logos.c:pad_pkmini,header_logo=../../Core/Src/retro-go/rg_logos.c:header_pkmini \
		--core-name "PokeMini" \
		--out $(PACKED_BIN)

all: pack

clean::
	rm -f $(PACKED_BIN)
