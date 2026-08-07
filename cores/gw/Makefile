# LCD-Game-Emulator (Nintendo Game & Watch handhelds) — standalone core.
# See cores/_template/Makefile and Core/Src/porting/core_common/CLAUDE.md.
#
# Compression (LZ4 + LZMA) and hardware JPEG stay in gw_romloader.c
# (submodule); those calls route through ABI trampolines. ram_start is
# seeded by run_dynamic_core() — cores must not reset it.

CORE_NAME  := gw
CORE_ENTRY := app_main_gw

CORE_GW := ../../external/LCD-Game-Emulator
CORE_PORTING_GW := ../../Core/Src/porting/gw

CORE_C_SOURCES := \
$(CORE_GW)/src/cpus/sm500op.c \
$(CORE_GW)/src/cpus/sm510op.c \
$(CORE_GW)/src/cpus/sm500core.c \
$(CORE_GW)/src/cpus/sm5acore.c \
$(CORE_GW)/src/cpus/sm510core.c \
$(CORE_GW)/src/cpus/sm511core.c \
$(CORE_GW)/src/cpus/sm510base.c \
$(CORE_GW)/src/gw_sys/gw_romloader.c \
$(CORE_GW)/src/gw_sys/gw_graphic.c \
$(CORE_GW)/src/gw_sys/gw_system.c \
$(CORE_PORTING_GW)/main_gw.c

CORE_C_INCLUDES := \
-I$(CORE_GW)/src \
-I$(CORE_GW)/src/cpus \
-I$(CORE_GW)/src/gw_sys \
-I../../Core/Inc/porting/gw \
-I../../Core/Src/porting/lib/lzma

CORE_C_DEFS := -DTARGET_GNW

CORE_LDSCRIPT := gw_core.ld

include ../_template/Makefile

# LCD-Game-Emulator uses C99-style `inline` without `static`/`extern` in
# sm5xx cores. GCC defaults to C17 inline (no standalone emission); without
# gnu89 semantics the linker loses sm510_div_timer / sm500_div_timer_cb / …
CFLAGS += -fgnu89-inline

PACKED_BIN := ../gw.bin

.PHONY: pack
pack: $(TARGET_BIN)
	python3 ../../tools/pack_core.py \
		--elf $(TARGET_ELF) --bin $(TARGET_BIN) \
		--system name="Game & Watch",dirname=gw,ext=gw,parse=rom,pad_logo=../../Core/Src/retro-go/rg_logos.c:pad_gw,header_logo=../../Core/Src/retro-go/rg_logos.c:header_gw \
		--out $(PACKED_BIN)

all: pack

clean::
	rm -f $(PACKED_BIN)
