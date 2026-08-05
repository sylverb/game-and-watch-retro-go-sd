# PC Engine / PC Engine CD (pce-go + Core/Src/porting/pce) — standalone core.
# See cores/_template/Makefile and docs/PICO8_EXTERNAL_MODULE.md for the
# general model this follows, and cores/pce/pce_core.ld for why this core
# needs its own linker script (a second, ITCM, memory-region segment for
# CPU-hot code) instead of the template's single-region default.
#
# Produces build/pce_core.bin (RAM_EMU segment) + build/pce_core_itcm.bin
# (ITCM segment), packaged into cores/pce.bin (CORE header + gnw_core_meta_t
# v3 + 2 systems' pad/header logos + both segments' payloads) by
# tools/pack_core.py — see the top-level Makefile.common's `cores_pce` target.
#
# One core binary, two launcher tabs: HuCard ROMs (dirname=pce, parse=rom)
# and CD-ROM² .cue images (dirname=pcecd, parse=cdrom) — dispatched at
# runtime by main_pce.c off ACTIVE_FILE->ext, exactly like the disabled
# classic build did.

CORE_NAME  := pce
CORE_ENTRY := app_main_pce

CORE_PCE_GO := ../../retro-go-stm32/pce-go/components/pce-go
CORE_PORTING_PCE := ../../Core/Src/porting/pce

CORE_C_SOURCES := \
$(CORE_PCE_GO)/gfx.c \
$(CORE_PCE_GO)/h6280.c \
$(CORE_PCE_GO)/pce.c \
$(CORE_PORTING_PCE)/sound_pce.c \
$(CORE_PORTING_PCE)/pce_cd.c \
$(CORE_PORTING_PCE)/pce_scsi.c \
$(CORE_PORTING_PCE)/pce_adpcm.c \
$(CORE_PORTING_PCE)/main_pce.c

CORE_C_INCLUDES := \
-I../../Core/Inc/porting/pce \
-I$(CORE_PCE_GO)

# CHEAT_CODES=1: main_pce.c's Game Genie support (ACTIVE_FILE->cheat_count /
# odroid_settings_ActiveGameGenieCodes_is_enabled) matches the release
# build's retro_emulator_file_t layout — see cores/_template/Makefile's
# CORE_COMMON_C_DEFS comment on COVERFLOW/CHEAT_CODES needing to agree with
# whichever ACTIVE_FILE fields a core actually reads.
# MAX_CHEAT_CODES=13: mirrors Makefile.common's release-build default
# (root Makefile's -DMAX_CHEAT_CODES=$(MAX_CHEAT_CODES)) — main_pce.c loops
# up to this many Game Genie entries per ROM.
CORE_C_DEFS := -DCHEAT_CODES=1 -DMAX_CHEAT_CODES=13

CORE_LDSCRIPT := pce_core.ld
CORE_EXTRA_SEGMENTS := itcm:core_itcm

include ../_template/Makefile

# Packaged output consumed by the main build (see Makefile.common's
# `cores_pce` target). Logos are extracted straight from rg_logos.c's
# existing C arrays — see tools/pack_core.py.
PACKED_BIN := ../pce.bin

.PHONY: pack
pack: $(TARGET_BIN) $(BUILD_DIR)/pce_core_itcm.bin
	python3 ../../tools/pack_core.py \
		--elf $(TARGET_ELF) --bin $(TARGET_BIN) \
		--system name="PC Engine",dirname=pce,ext=pce,parse=rom,pad_logo=../../Core/Src/retro-go/rg_logos.c:pad_pce,header_logo=../../Core/Src/retro-go/rg_logos.c:header_pce \
		--system name="PC Engine CD",dirname=pcecd,ext=cue,parse=cdrom,pad_logo=../../Core/Src/retro-go/rg_logos.c:pad_pce,header_logo=../../Core/Src/retro-go/rg_logos.c:header_pcecd \
		--segment itcm:__ITCM_CORE_START__:__CORE_ITCM_CODE_END__:__CORE_ITCM_BSS_END__:$(BUILD_DIR)/pce_core_itcm.bin \
		--out $(PACKED_BIN)

all: pack

clean::
	rm -f $(PACKED_BIN)
