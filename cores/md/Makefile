# Mega Drive / Genesis (gwenesis) — standalone core (see cores/_template/Makefile
# and docs/PICO8_EXTERNAL_MODULE.md for the general model this follows).
#
# Produces build/md_core.bin: __RAM_EMU_START__-linked code+data, no libc,
# talking to the firmware exclusively through gw_firmware_abi_t. Packaged
# into cores/md.bin (CORE header + gnw_core_meta_t + logos + this payload)
# by tools/pack_core.py — see the top-level Makefile.common's `cores_md` target.

CORE_NAME  := md
CORE_ENTRY := app_main_gwenesis

# CORE_GWENESIS mirrors the (now-unused for the monolithic build) variable
# of the same name in the root Makefile — external/gwenesis is the Mega
# Drive / Genesis emulator core (git submodule).
CORE_GWENESIS := ../../external/gwenesis

CORE_C_SOURCES := \
$(CORE_GWENESIS)/src/cpus/M68K/m68kcpu.c \
$(CORE_GWENESIS)/src/cpus/Z80/Z80.c \
$(CORE_GWENESIS)/src/sound/z80inst.c \
$(CORE_GWENESIS)/src/sound/ym2612.c \
$(CORE_GWENESIS)/src/sound/gwenesis_sn76489.c \
$(CORE_GWENESIS)/src/bus/gwenesis_bus.c \
$(CORE_GWENESIS)/src/bus/gwenesis_sram.c \
$(CORE_GWENESIS)/src/bus/gwenesis_eeprom.c \
$(CORE_GWENESIS)/src/io/gwenesis_io.c \
$(CORE_GWENESIS)/src/vdp/gwenesis_vdp_mem.c \
$(CORE_GWENESIS)/src/vdp/gwenesis_vdp_gfx.c \
$(CORE_GWENESIS)/src/savestate/gwenesis_savestate.c \
../../Core/Src/porting/gwenesis/main_gwenesis.c

CORE_C_INCLUDES := \
-I$(CORE_GWENESIS)/src/cpus/M68K \
-I$(CORE_GWENESIS)/src/cpus/Z80 \
-I$(CORE_GWENESIS)/src/sound \
-I$(CORE_GWENESIS)/src/bus \
-I$(CORE_GWENESIS)/src/vdp \
-I$(CORE_GWENESIS)/src/io \
-I$(CORE_GWENESIS)/src/savestate

# LSB_FIRST/TABLES_FULL: mirrors the (disabled) MD_C_DEFS in the root
# Makefile — required by the M68K core (macros.h/m68kops.h) and Z80.h.
# TARGET_GNW: mirrors the main firmware's global -DTARGET_GNW
# (Makefile.common) — selects every G&W-specific branch throughout
# external/gwenesis (accurate-timing YM2612/SN76489 signatures, the
# ROM_DATA/M68K_RAM allocation strategy in gwenesis_bus.c, ...).
CORE_C_DEFS := -DLSB_FIRST -DTABLES_FULL -DTARGET_GNW

# ym2612.c (YM2612Init) computes its sine/log FM tables with libm sin()/log()
# once at startup — self-contained, no firmware calls, so link it directly.
CORE_LDLIBS := -lm

include ../_template/Makefile

# Packaged output consumed by the main build (see Makefile.common's
# `cores_md` target): CORE header + gnw_core_meta_t + pad/header logos
# (pixel-identical to the ones previously compiled into rg_logos.c) +
# payload. Logos are extracted straight from rg_logos.c's existing C
# arrays rather than re-authored as image assets — see tools/pack_core.py.
PACKED_BIN := ../md.bin

.PHONY: pack
pack: $(TARGET_BIN)
	python3 ../../tools/pack_core.py \
		--elf $(TARGET_ELF) --bin $(TARGET_BIN) \
		--system-name "Sega Genesis" --dirname md \
		--extensions "md gen bin" \
		--pad-logo-c ../../Core/Src/retro-go/rg_logos.c:pad_gen \
		--header-logo-c ../../Core/Src/retro-go/rg_logos.c:header_gen \
		--core-name "Gwenesis" \
		--out $(PACKED_BIN)

all: pack

clean::
	rm -f $(PACKED_BIN)
