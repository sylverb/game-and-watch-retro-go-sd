# Nintendo Entertainment System (fceumm-go, C) — standalone core.
# See cores/_template/Makefile and Core/Src/porting/core_common/CLAUDE.md.
#
# Layout quirk vs other cores: FCEUmm keeps a 48K mapper window at
# __RAM_EMU_START__ (loaded at runtime from
# /cores/nes_fceumm_mappers/mappers.pak).
# nes_core.ld reserves that window after the entry trampoline and also
# links every sidecar board .o as a VMA-overlapping overlay (LMA in
# MAPPER_STORE) so ines.o's bmap gets correct absolute Init addresses —
# same model as the old monolithic fceumm_mappers.ld. Sidecar blobs are
# then objcopy'd out into mapper_<stem>.bin and packed.
#
# Packaged output: ../nes_fceu.bin (+ ../nes_fceumm_mappers/mappers.pak and
# ../nes_fceumm_mappers/ines_correct.bin) — matches scripts/sd_cores_pack.py.

CORE_NAME  := nes
CORE_ENTRY := app_main_nes_fceu

CORE_FCEUMM := ../../external/fceumm-go
CORE_PORTING_NES := ../../Core/Src/porting/nes_fceu

# Engine + shared boards only (sidecars are MAPPER_C_SOURCES below).
CORE_C_SOURCES := \
$(CORE_PORTING_NES)/main_nes_fceu.c \
$(CORE_PORTING_NES)/nes_fceu_mappers.c \
$(CORE_FCEUMM)/src/cheat.c \
$(CORE_FCEUMM)/src/fceu-cart.c \
$(CORE_FCEUMM)/src/fceu-endian.c \
$(CORE_FCEUMM)/src/fceu-memory.c \
$(CORE_FCEUMM)/src/fceu-sound.c \
$(CORE_FCEUMM)/src/fceu-state.c \
$(CORE_FCEUMM)/src/fceu.c \
$(CORE_FCEUMM)/src/fds.c \
$(CORE_FCEUMM)/src/fds_apu.c \
$(CORE_FCEUMM)/src/filter.c \
$(CORE_FCEUMM)/src/general.c \
$(CORE_FCEUMM)/src/ines.c \
$(CORE_FCEUMM)/src/input.c \
$(CORE_FCEUMM)/src/md5.c \
$(CORE_FCEUMM)/src/nsf.c \
$(CORE_FCEUMM)/src/palette.c \
$(CORE_FCEUMM)/src/ppu.c \
$(CORE_FCEUMM)/src/video.c \
$(CORE_FCEUMM)/src/x6502.c \
$(CORE_FCEUMM)/src/boards/mmc3.c \
$(CORE_FCEUMM)/src/boards/latch.c \
$(CORE_FCEUMM)/src/boards/vrcirq.c \
$(CORE_FCEUMM)/src/boards/eeprom_93C66.c \
$(CORE_FCEUMM)/src/boards/fceu-emu2413.c

CORE_C_INCLUDES := \
-I$(CORE_FCEUMM)/src \
-I../../Core/Inc/porting/nes_fceu \
-I../../Core/Src/porting/lib/lzma

# FCEU_* / VIDEO_RGB565 / __LIBRETRO__: match Makefile.common C_DEFS used by
# the old monolithic nes_fceu objects. COVERFLOW+CHEAT_CODES must match the
# firmware layout (ACTIVE_FILE->cheat_* sits after COVERFLOW fields).
CORE_C_DEFS := \
-DTARGET_GNW \
-DFCEU_VERSION_NUMERIC=9813 \
-DFCEU_LOW_RAM \
-DFCEU_NO_MALLOC \
-DVIDEO_RGB565 \
-D__LIBRETRO__ \
-DCOVERFLOW=1 \
-DCHEAT_CODES=1 \
-DMAX_CHEAT_CODES=13

# nsf.c DrawNSF uses sin/cos/atan/sqrt once; self-contained libm (see cores/md).
CORE_LDLIBS := -lm

CORE_LDSCRIPT := nes_core.ld

include ../_template/Makefile

# Upstream fceumm has a few intentional paren/sequence-point patterns.
CFLAGS += -Wno-sequence-point -Wno-parentheses

# ---------------------------------------------------------------------------
# Mapper sidecars — compiled like core objects, linked as VMA overlays into
# nes_core.elf (see nes_core.ld + gen_nes_mapper_overlays_ld.py), then
# extracted to mapper_<stem>.bin for mappers.pak.
# ---------------------------------------------------------------------------
MAPPER_IGNORE := __% fceu-emu2413.c mmc3.c latch.c vrcirq.c eeprom_93C66.c
MAPPER_C_SOURCES := $(filter-out $(addprefix $(CORE_FCEUMM)/src/boards/,$(MAPPER_IGNORE)), \
	$(wildcard $(CORE_FCEUMM)/src/boards/*.c))

MAPPER_OBJECTS := $(addprefix $(BUILD_DIR)/,$(notdir $(MAPPER_C_SOURCES:.c=.o)))
MAPPER_STEMS := $(subst -,_,$(notdir $(basename $(MAPPER_C_SOURCES))))
MAPPERS_OUT := ../nes_fceumm_mappers
MAPPER_BINS := $(addprefix $(MAPPERS_OUT)/mapper_,$(addsuffix .bin,$(MAPPER_STEMS)))
MAPPERS_PACK := $(MAPPERS_OUT)/mappers.pak
INES_CORRECT := $(MAPPERS_OUT)/ines_correct.bin
MAPPER_OVERLAYS_LD := $(BUILD_DIR)/nes_mapper_overlays.ld

vpath %.c $(CORE_FCEUMM)/src/boards

$(MAPPERS_OUT):
	mkdir -p $(MAPPERS_OUT)

$(MAPPER_OVERLAYS_LD): ../../scripts/gen_nes_mapper_overlays_ld.py $(MAPPER_C_SOURCES) | $(BUILD_DIR)
	python3 ../../scripts/gen_nes_mapper_overlays_ld.py \
		--boards-dir $(CORE_FCEUMM)/src/boards \
		--objects-dir $(BUILD_DIR) \
		--output $@

# Mapper objects must be on the link line (linker script references them by
# path; ld will not auto-open .o files named only inside SECTIONS). Overlay
# ld fragment must exist before the link. Re-state the link recipe so
# MAPPER_OBJECTS are passed to $(CC) (template only links C/CXX/ASM_OBJECTS).
# (GNU Make warns about overriding the template recipe — expected.)
$(TARGET_ELF): $(C_OBJECTS) $(CXX_OBJECTS) $(ASM_OBJECTS) $(MAPPER_OBJECTS) $(MAPPER_OVERLAYS_LD) $(CORE_LDSCRIPT)
	$(CC) $(C_OBJECTS) $(CXX_OBJECTS) $(ASM_OBJECTS) $(MAPPER_OBJECTS) $(LDFLAGS) -o $@
	$(SZ) $@

define NES_MAPPER_BIN_RULE
$(MAPPERS_OUT)/mapper_$(subst -,_,$(notdir $(basename $(1)))).bin: $(TARGET_ELF) | $(MAPPERS_OUT)
	$$(CP) -O binary --only-section=.overlay_nes_mapper_$(subst -,_,$(notdir $(basename $(1)))) $$(TARGET_ELF) $$@
endef
$(foreach src,$(MAPPER_C_SOURCES),$(eval $(call NES_MAPPER_BIN_RULE,$(src))))

$(MAPPERS_PACK): $(MAPPER_BINS) ../../scripts/gen_mappers_pack.py ../../external/fceumm-go/gen_mappers_table.py | $(MAPPERS_OUT)
	python3 ../../scripts/gen_mappers_pack.py \
		--bins-dir $(MAPPERS_OUT) \
		--output $@ \
		--repo ../..

$(INES_CORRECT): ../../external/fceumm-go/gen_ines_database.py ../../external/fceumm-go/src/ines-correct.h | $(MAPPERS_OUT)
	cd ../.. && python3 external/fceumm-go/gen_ines_database.py cores/nes_fceumm_mappers/ines_correct.bin

PACKED_BIN := ../nes_fceu.bin

.PHONY: pack
pack: $(TARGET_BIN) $(MAPPERS_PACK) $(INES_CORRECT)
	python3 ../../tools/pack_core.py \
		--elf $(TARGET_ELF) --bin $(TARGET_BIN) \
		--system-name "Nintendo Entertainment System" --dirname nes \
		--extensions "nes fds nsf" \
		--pad-logo-c ../../Core/Src/retro-go/rg_logos.c:pad_nes \
		--header-logo-c ../../Core/Src/retro-go/rg_logos.c:header_nes \
		--core-name "FCEUmm" \
		--out $(PACKED_BIN)

all: pack

clean::
	rm -f $(PACKED_BIN)
	rm -rf $(MAPPERS_OUT)
