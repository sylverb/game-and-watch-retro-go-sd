# Game Boy / Game Boy Color (tgbdual-go, C++) — standalone core (see
# cores/_template/Makefile and Core/Src/porting/core_common/CLAUDE.md for
# the general model this follows). First C++ core built by this SDK — see
# cores/_template/Makefile's CORE_CXX_SOURCES doc comment and
# Core/Src/porting/core_common/gw_core_cxx_support.cpp for what building
# without libstdc++ requires.
#
# Produces build/gb_tgbdual_core.bin: __RAM_EMU_START__-linked code+data, no
# libc, no libstdc++, talking to the firmware exclusively through
# gw_firmware_abi_t. Packaged into cores/tgb.bin (CORE header +
# gnw_core_meta_t v3 covering 2 systems: gb + gbc, sharing this one core) by
# tools/pack_core.py — see the top-level Makefile.common's `cores_gb_tgbdual`
# target. tgb.bin (not gb_tgbdual.bin) matches the dirname->core-file map
# scripts/sd_cores_pack.py already carries for this system.

CORE_NAME  := gb_tgbdual
CORE_ENTRY := app_main_gb_tgbdual

# CORE_TGBDUAL mirrors the CORE_WSV/CORE_GWENESIS convention in
# cores/wsv/Makefile / cores/md/Makefile — external/tgbdual-go is the Game
# Boy / Game Boy Color emulator core (git submodule).
CORE_TGBDUAL := ../../external/tgbdual-go/gb_core
CORE_PORTING_GB := ../../Core/Src/porting/gb_tgbdual

CORE_CXX_SOURCES := \
$(CORE_TGBDUAL)/tgbdual_apu.cpp \
$(CORE_TGBDUAL)/tgbdual_cheat.cpp \
$(CORE_TGBDUAL)/tgbdual_cpu.cpp \
$(CORE_TGBDUAL)/tgbdual_gb.cpp \
$(CORE_TGBDUAL)/tgbdual_lcd.cpp \
$(CORE_TGBDUAL)/tgbdual_mbc.cpp \
$(CORE_TGBDUAL)/tgbdual_rom.cpp \
$(CORE_TGBDUAL)/tgbdual_sgb.cpp \
$(CORE_PORTING_GB)/gw_renderer.cpp \
$(CORE_PORTING_GB)/main_gb_tgbdual.cpp

# Soft scaling uses firmware ABI imlib_draw_image (bilinear.c stays in
# the firmware ELF, not this core).
CORE_C_SOURCES := \
$(CORE_PORTING_GB)/gb_i18n.c

CORE_C_INCLUDES := \
-I$(CORE_TGBDUAL) \
-I../../external/tgbdual-go \
-I../../Core/Inc/porting/gb_tgbdual

# TARGET_GNW: selects the fixed-capacity cheat-list/no-libstdc++ code paths
# in external/tgbdual-go (gb.h/tgbdual_cheat.cpp) instead of std::list — see
# gw_core_cxx_support.cpp's file header comment on why this target has no
# libstdc++ at all.
#
# COVERFLOW=1 CHEAT_CODES=1 MAX_CHEAT_CODES=13: main_gb_tgbdual.cpp reads
# ACTIVE_FILE->cheat_count/cheat_codes (Game Genie/GameShark) AND is the
# first core to need CHEAT_CODES with COVERFLOW also possibly enabled —
# retro_emulator_file_t (rg_emulators.h) places cheat_count/cheat_codes
# AFTER the COVERFLOW-conditional img_address/img_state fields, so both
# defines must match the firmware's actual build flags (Makefile.common's
# Docker release target: COVERFLOW=1 CHEAT_CODES=1) or every field after
# `size` reads at the wrong offset — see cores/_template/Makefile's
# CORE_COMMON_C_DEFS comment for the general hazard. COVERFLOW still shifts
# every field after `size`, cheat_count included, so it must be set here
# regardless).
# MAX_CHEAT_CODES=13 mirrors Makefile.common's release-build default (root
# Makefile's -DMAX_CHEAT_CODES=$(MAX_CHEAT_CODES)).
CORE_C_DEFS := -DTARGET_GNW -DCOVERFLOW=1 -DCHEAT_CODES=1 -DMAX_CHEAT_CODES=13

include ../_template/Makefile

# Packaged output consumed by the main build (see Makefile.common's
# `cores_gb_tgbdual` target). Two systems share this one core binary/segment
# (real hardware: same handheld, same emulator, only the cartridge's CGB
# flag differs) — dispatched at runtime by main_gb_tgbdual.cpp reading the
# ROM header, exactly like PC Engine's rom/cdrom split off ACTIVE_FILE->ext
PACKED_BIN := ../tgb.bin

.PHONY: pack
pack: $(TARGET_BIN)
	python3 ../../tools/pack_core.py \
		--elf $(TARGET_ELF) --bin $(TARGET_BIN) \
		--system name="Game Boy",dirname=gb,pad_logo=assets/pad.bmp,header_logo=assets/header_gb.bmp,ext=gb,parse=rom,cheat_ext=ggcodes \
		--system name="Game Boy Color",dirname=gbc,pad_logo=assets/pad.bmp,header_logo=assets/header_gbc.bmp,ext=gbc,parse=rom,cheat_ext=ggcodes \
		--core-name "TGB Dual" \
		--out $(PACKED_BIN)

all: pack

clean::
	rm -f $(PACKED_BIN)
