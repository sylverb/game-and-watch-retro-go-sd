# Atari Lynx (handy-go, C++) — standalone core (see
# cores/_template/Makefile and Core/Src/porting/core_common/CLAUDE.md for
# the general model this follows). Second C++ core built by this SDK after
# cores/gb_tgbdual — see cores/_template/Makefile's CORE_CXX_SOURCES doc
# comment and Core/Src/porting/core_common/gw_core_cxx_support.cpp for what
# building without libstdc++ requires.
#
# Produces build/lynx_core.bin: __RAM_EMU_START__-linked code+data, no libc,
# no libstdc++, talking to the firmware exclusively through
# gw_firmware_abi_t. Packaged into cores/lynx.bin (CORE header +
# gnw_core_meta_t) by tools/pack_core.py — see the top-level
# Makefile.common's `cores_lynx` target. lynx.bin matches the dirname->
# core-file map scripts/sd_cores_pack.py already carries for this system.

CORE_NAME  := lynx
CORE_ENTRY := app_main_lynx

# CORE_LYNX mirrors the (now-disabled for the monolithic build) variable of
# the same name in the root Makefile — external/handy-go is the Atari Lynx
# emulator core (git submodule).
CORE_LYNX := ../../external/handy-go
CORE_PORTING_LYNX := ../../Core/Src/porting/lynx

CORE_CXX_SOURCES := \
$(CORE_LYNX)/cart.cpp \
$(CORE_LYNX)/eeprom.cpp \
$(CORE_LYNX)/lynxdec.cpp \
$(CORE_LYNX)/mikie.cpp \
$(CORE_LYNX)/susie.cpp \
$(CORE_LYNX)/system.cpp \
$(CORE_PORTING_LYNX)/main_lynx.cpp

CORE_C_INCLUDES := \
-I$(CORE_LYNX) \
-I../../Core/Inc/porting/lynx

# TARGET_GNW: selects the G&W-specific code paths in external/handy-go
# (log_printf -> printf, LSS_FILE -> FILE, etc. — see system.h).
CORE_C_DEFS := -DTARGET_GNW

include ../_template/Makefile

# Packaged output consumed by the main build (see Makefile.common's
# `cores_lynx` target). Logos are extracted straight from rg_logos.c's
# existing C arrays — see tools/pack_core.py. Extensions match README.md
# (/roms/lynx/: .lnx, .lyx).
PACKED_BIN := ../lynx.bin

.PHONY: pack
pack: $(TARGET_BIN)
	python3 ../../tools/pack_core.py \
		--elf $(TARGET_ELF) --bin $(TARGET_BIN) \
		--system-name "Atari Lynx" --dirname lynx \
		--extensions "lnx lyx" \
		--pad-logo-c ../../Core/Src/retro-go/rg_logos.c:pad_lynx \
		--header-logo-c ../../Core/Src/retro-go/rg_logos.c:header_lynx \
		--core-name "Handy" \
		--out $(PACKED_BIN)

all: pack

clean::
	rm -f $(PACKED_BIN)
