# Watara Supervision — standalone core (see cores/_template/Makefile and
# docs/PICO8_EXTERNAL_MODULE.md for the general model this follows).
#
# Produces build/wsv_core.bin: __RAM_EMU_START__-linked code+data, no libc,
# talking to the firmware exclusively through gw_firmware_abi_t. Packaged
# into cores/wsv.bin (CORE header + gnw_core_meta_t + logos + this payload)
# by tools/pack_core.py — see the top-level Makefile's `cores` target.

CORE_NAME  := wsv
CORE_ENTRY := app_main_wsv

# CORE_WSV mirrors the (now-unused for the monolithic build) variable of
# the same name in the root Makefile — external/potator is the Watara
# Supervision emulator core (git submodule).
CORE_WSV := ../../external/potator

CORE_C_SOURCES := \
$(CORE_WSV)/common/controls.c \
$(CORE_WSV)/common/gpu.c \
$(CORE_WSV)/common/m6502/m6502.c \
$(CORE_WSV)/common/memorymap.c \
$(CORE_WSV)/common/timer.c \
$(CORE_WSV)/common/watara.c \
$(CORE_WSV)/common/wsv_sound.c \
../../Core/Src/porting/wsv/main_wsv.c \
../../Core/Src/porting/wsv/wsv_i18n.c

CORE_C_INCLUDES := -I$(CORE_WSV)/common -I../../Core/Inc/porting/wsv

include ../_template/Makefile

# Packaged output consumed by the main build (see Makefile.common's
# `cores` target): CORE header + gnw_core_meta_t + pad/header logos
# (pixel-identical to the ones previously compiled into rg_logos.c) +
# payload. Logos are extracted straight from rg_logos.c's existing C
# arrays rather than re-authored as image assets — see tools/pack_core.py.
PACKED_BIN := ../wsv.bin

.PHONY: pack
pack: $(TARGET_BIN)
	python3 ../../tools/pack_core.py \
		--elf $(TARGET_ELF) --bin $(TARGET_BIN) \
		--system-name "Watara Supervision" --dirname wsv \
		--extensions "wsv sv bin lzma" \
		--pad-logo-c ../../Core/Src/retro-go/rg_logos.c:pad_wsv \
		--header-logo-c ../../Core/Src/retro-go/rg_logos.c:header_wsv \
		--core-name "Potator" \
		--out $(PACKED_BIN)

all: pack

clean::
	rm -f $(PACKED_BIN)
