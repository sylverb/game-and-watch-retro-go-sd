# Game Boy Advance (gpSP) — standalone core.
# See cores/_template/Makefile and Core/Src/porting/core_common/CLAUDE.md.
#
# Produces:
#   build/gba_core.bin       RAM_EMU segment
#   build/gba_core_itcm.bin  ITCM segment (cpu.o)
#   build/gba_core_ahb.bin   AHB segment (empty payload; BSS-only, code_size 0)
#   ../gba.bin               packed CORE header + segments
#   ../gba.xip               cold text + rodata sidecar (cached to QSPI at runtime)
#
# Linker layout: cores/gba/gba_core.ld (ITCM + AHB + XIP @ 0xDEC00000).

CORE_NAME  := gba
CORE_ENTRY := app_main_gba

CORE_GPSP := ../../external/gpsp
CORE_PORTING_GBA := ../../Core/Src/porting/gba
CORE_GBA_M4A := ../../tools/gba_m4a

CORE_C_SOURCES := \
$(CORE_GPSP)/gba_memory.c \
$(CORE_GPSP)/sound.c \
$(CORE_GPSP)/main.c \
$(CORE_GPSP)/savestate.c \
$(CORE_GPSP)/input.c \
$(CORE_GPSP)/cheats.c \
$(CORE_GPSP)/serial.c \
$(CORE_GPSP)/serial_proto.c \
$(CORE_GPSP)/gbp.c \
$(CORE_GPSP)/rfu.c \
$(CORE_PORTING_GBA)/gba_frontend.c \
$(CORE_PORTING_GBA)/gba_idle_loop.c \
$(CORE_PORTING_GBA)/gba_audio_filter.c \
$(CORE_PORTING_GBA)/main_gba.c \
$(CORE_PORTING_GBA)/gba_bios_hle.c \
$(CORE_GBA_M4A)/m4a_hle.c \
$(CORE_GBA_M4A)/m4a_gpsp.c \
../../Core/Src/bilinear.c

CORE_CXX_SOURCES := \
$(CORE_GPSP)/cpu.cc \
$(CORE_GPSP)/video.cc

CORE_ASM_SOURCES := \
$(CORE_PORTING_GBA)/gba_bios.S

CORE_C_INCLUDES := \
-I$(CORE_GPSP) \
-I$(CORE_GPSP)/libretro/libretro-common/include \
-I$(CORE_GBA_M4A) \
-I../../Core/Inc/porting/gba \
-I../..

# ROM_BUFFER_SIZE=0    cart XIP from QSPI — not buffered in RAM.
# OBJ_PER_LINE_MAX=32  caps sprite-line BSS (~120 KB saved vs gpSP default).
# BUFFER_SIZE=4096     audio ring (gpSP default is 128 KB).
# GBA_SOUND_FREQUENCY  matches SAI — no resample.
# CHEAT_CODES=1        ACTIVE_FILE->cheat_* layout matches release firmware.
CORE_C_DEFS := \
-DROM_BUFFER_SIZE=0 \
-DOBJ_PER_LINE_MAX=32 \
-DBUFFER_SIZE=4096 \
-DGBA_SOUND_FREQUENCY=48000 \
-DGBA_M4A_HLE \
-DGBA_BIOS_HLE \
-DCHEAT_CODES=1 \
-DMAX_CHEAT_CODES=20

# .incbin "open_gba_bios.bin" — search path for the stock BIOS replacement.
CORE_ASFLAGS := -I../../external/gpsp/bios

# Per-object gpsp symbol renames (after the shared bridge redefine-syms pass).
CORE_EXTRA_REDEFINE_SYMS := ../../gba_redefines

CORE_LDSCRIPT := gba_core.ld
# ITCM only via template CORE_EXTRA_SEGMENTS. AHB is BSS-only (code_size 0)
# — handled by a dedicated rule below so we don't fight the template's
# generic objcopy of .core_ahb (NOLOAD → empty/missing file).
CORE_EXTRA_SEGMENTS := itcm:core_itcm

include ../_template/Makefile

# gpSP's main.c shares a basename with Core/Src/main.c. bilinear.c puts
# ../../Core/Src/ on vpath, so the pattern rule would compile the firmware
# main.c into build/main.o — pin the gpsp source explicitly (same reason
# Makefile.common kept GBA out of vpath).
$(BUILD_DIR)/main.o: $(CORE_GPSP)/main.c $(REDEFINE_SYMS_FILE) | $(BUILD_DIR)
	$(CC) -c $(CFLAGS) $< -o $@
	$(CP) --redefine-syms=$(REDEFINE_SYMS_FILE) $@
	$(call CORE_APPLY_EXTRA_REDEFINE,$@)

# gpSP warn suppressions (mirrors Makefile.common GBA_WARN_OFF).
GBA_WARN_OFF := -Wno-parentheses -Wno-unknown-pragmas -Wno-strict-aliasing -Wno-comment \
	-Wno-unused-function -Wno-stack-usage -Wno-unused-variable \
	-Wno-unused-but-set-variable -Wno-sign-compare -Wno-format

CFLAGS   += $(GBA_WARN_OFF)
CXXFLAGS += $(GBA_WARN_OFF)

# Hot paths: -O3 like the classic Makefile.common gba_is_hot / gba_is_interp rules.
$(BUILD_DIR)/gba_memory.o: CFLAGS := $(filter-out -Os,$(CFLAGS)) -O3
$(BUILD_DIR)/cpu.o: CXXFLAGS := $(filter-out -Os,$(CXXFLAGS)) -O3

PACKED_BIN := ../gba.bin
XIP_BIN    := ../gba.xip

# AHB segment is BSS-only (code_size 0). objcopy of a NOLOAD-only section may
# produce nothing; pack_core.py needs a 0-byte file matching code_size.
$(BUILD_DIR)/gba_core_ahb.bin: $(TARGET_ELF)
	@$(CP) -O binary --only-section=.core_ahb $< $@ 2>/dev/null || true
	@: > $@

$(XIP_BIN): $(TARGET_ELF)
	$(CP) -O binary --only-section=.xip_gba --only-section=.rodata_gba $< $@
	$(SZ) --target=binary $@

.PHONY: pack
pack: $(TARGET_BIN) $(BUILD_DIR)/gba_core_itcm.bin $(BUILD_DIR)/gba_core_ahb.bin $(XIP_BIN)
	python3 ../../tools/pack_core.py \
		--elf $(TARGET_ELF) --bin $(TARGET_BIN) \
		--system name="Nintendo Gameboy Advance",dirname=gba,ext=gba,parse=rom,pad_logo=../../Core/Src/retro-go/rg_logos.c:pad_gba,header_logo=../../Core/Src/retro-go/rg_logos.c:header_gba \
		--segment itcm:__ITCM_CORE_START__:__CORE_ITCM_CODE_END__:__CORE_ITCM_BSS_END__:$(BUILD_DIR)/gba_core_itcm.bin \
		--segment ahb:__AHB_CORE_START__:__CORE_AHB_CODE_END__:__CORE_AHB_BSS_END__:$(BUILD_DIR)/gba_core_ahb.bin \
		--out $(PACKED_BIN)

all: pack

clean::
	rm -f $(PACKED_BIN) $(XIP_BIN)
