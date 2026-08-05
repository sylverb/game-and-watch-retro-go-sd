# core_common build template — shared by every standalone "core" binary.
#
# A concrete core's own Makefile (e.g. cores/wsv/Makefile) sets a handful of
# CORE_* variables and then does:
#
#   include ../_template/Makefile
#
# This file provides everything that does NOT vary between cores: toolchain
# resolution, the MCU flags (must match the main firmware's — see
# CLAUDE.md), the common include/define set, the objcopy --redefine-syms
# step (Core/Src/porting/core_common/gw_core_bridge_redefine_syms.txt), and
# the final link (no libc — every core routes through gw_firmware_abi_t
# instead, see Core/Src/porting/core_common/gw_core_bridge.h).
#
# Variables a concrete core's Makefile must set before including this file:
#   CORE_NAME        short id, e.g. wsv. Output: build/$(CORE_NAME)_core.elf/.bin
#   CORE_ENTRY       entry symbol, e.g. app_main_wsv (must exist in CORE_C_SOURCES)
#   CORE_C_SOURCES   .c files to build, repo-root-relative paths. Object
#                    files land in a single flat build/ dir (vpath-based,
#                    like the root Makefile) — every CORE_C_SOURCES entry
#                    must have a basename unique across the whole list.
#
# Optional:
#   CORE_C_INCLUDES  extra -I flags, on top of the common set below
#   CORE_C_DEFS      extra -D flags, on top of the common set below
#   CORE_LDSCRIPT    defaults to $(TEMPLATE_DIR)/core_ram_emu.ld. Point this
#                    at your own script (INCLUDE-ing ld/gnw_itcm_core.ld /
#                    ld/gnw_ahb_core.ld as needed) if you use CORE_EXTRA_SEGMENTS
#                    — see cores/pce/pce_core.ld for an ITCM example.
#   CORE_LDLIBS      extra -l flags, e.g. -lm (see cores/md/Makefile: gwenesis's
#                    ym2612.c calls sin()/log() once at init time to build FM
#                    tables — self-contained libm code with no firmware calls,
#                    so it's simplest to link it in directly rather than route
#                    it through gw_firmware_abi_t like everything else here)
#   CORE_EXTRA_SEGMENTS  space-separated "region:section" pairs for code/data
#                    that must land in a fixed memory region other than
#                    RAM_EMU (segment 0, always produced by TARGET_BIN below)
#                    — e.g. "itcm:core_itcm" extracts the `.core_itcm` output
#                    section (defined by your own CORE_LDSCRIPT) into
#                    build/$(CORE_NAME)_core_itcm.bin via objcopy
#                    --only-section. Each pair needs a matching --segment
#                    argument in the core's own `pack` target (see
#                    tools/pack_core.py and cores/pce/Makefile).
#   GCC_PATH         toolchain bin dir, same convention as the root Makefile
#   OPT              optimization level, default -Os (code-size matters more
#                     here than for the always-resident launcher: this is the
#                     entire per-core RAM_EMU budget, ~724K minus the TLSF
#                     pool a classic core carves out of its own BSS/heap)

ifeq ($(CORE_NAME),)
$(error CORE_NAME must be set before including cores/_template/Makefile)
endif
ifeq ($(CORE_ENTRY),)
$(error CORE_ENTRY must be set before including cores/_template/Makefile)
endif

TEMPLATE_DIR := $(abspath $(dir $(lastword $(MAKEFILE_LIST))))
ROOT_DIR     := $(abspath $(TEMPLATE_DIR)/../..)
BUILD_DIR    := build

CORE_LDSCRIPT ?= $(TEMPLATE_DIR)/core_ram_emu.ld

#######################################
# toolchain (mirrors Makefile.common)
#######################################
PREFIX = arm-none-eabi-
ifdef GCC_PATH
CC   = $(GCC_PATH)/$(PREFIX)gcc
AS   = $(GCC_PATH)/$(PREFIX)gcc -x assembler-with-cpp
CP   = $(GCC_PATH)/$(PREFIX)objcopy
NM   = $(GCC_PATH)/$(PREFIX)nm
SZ   = $(GCC_PATH)/$(PREFIX)size
else
CC   = $(PREFIX)gcc
AS   = $(PREFIX)gcc -x assembler-with-cpp
CP   = $(PREFIX)objcopy
NM   = $(PREFIX)nm
SZ   = $(PREFIX)size
endif

#######################################
# MCU flags — MUST match the main firmware build (Makefile.common) or the
# ABI calling convention (float args/returns in particular) breaks across
# the firmware<->core boundary.
#######################################
CPU       = -mcpu=cortex-m7 -mtune=cortex-m7
FPU       = -mfpu=fpv5-d16
FLOAT_ABI = -mfloat-abi=hard
MCU       = $(CPU) -mthumb -mno-unaligned-access $(FPU) $(FLOAT_ABI)

OPT ?= -Os

#######################################
# common defines / includes
#
# COVERFLOW=0 / CHEAT_CODES=0 here only need to match the firmware's own
# build for struct fields a core actually reads. retro_emulator_file_t
# (rg_emulators.h) puts every COVERFLOW/CHEAT_CODES-conditional field AFTER
# path/size/ext, so ACTIVE_FILE->{path,size,ext} (all a classic core like
# WSV touches) are at fixed offsets regardless of a mismatch. A future core
# reading ->region, ->system or the cheat fields would need to pass the
# same values the firmware was built with.
#######################################
CORE_COMMON_C_DEFS := \
-DUSE_HAL_DRIVER \
-DSTM32H7B0xx \
-DSD_CARD=1 \
-DGNW_DISABLE_COMPRESSION \
-DCOVERFLOW=0 \
-DCHEAT_CODES=0 \
-DGNW_CORE_ENTRY_TARGET=$(CORE_ENTRY)

CORE_COMMON_C_INCLUDES := \
-I$(ROOT_DIR)/Core/Inc \
-I$(ROOT_DIR)/Core/Inc/retro-go \
-I$(ROOT_DIR)/Core/Inc/porting \
-I$(ROOT_DIR)/Core/Src/porting/core_common \
-I$(ROOT_DIR)/Core/Src/porting/lib \
-I$(ROOT_DIR)/Core/Src/porting/lib/FatFs \
-I$(ROOT_DIR)/retro-go-stm32/components/odroid \
-I$(ROOT_DIR)/Drivers/STM32H7xx_HAL_Driver/Inc \
-I$(ROOT_DIR)/Drivers/STM32H7xx_HAL_Driver/Inc/Legacy \
-I$(ROOT_DIR)/Drivers/CMSIS/Device/ST/STM32H7xx/Include \
-I$(ROOT_DIR)/Drivers/CMSIS/Include

CFLAGS = $(MCU) $(OPT) -Wall -fdata-sections -ffunction-sections \
-fno-common -MMD -MP -MF"$(@:%.o=%.d)" \
$(CORE_COMMON_C_DEFS) $(CORE_C_DEFS) \
$(CORE_COMMON_C_INCLUDES) $(CORE_C_INCLUDES)

ASFLAGS = $(MCU) $(OPT) $(CORE_COMMON_C_DEFS) $(CORE_C_DEFS)

# -nostdlib: no newlib, no crt0/crti/crtn — this image has no _start (the
# firmware jumps straight into RAM at offset 0, see gw_core_entry.S) and no
# libc symbol may sneak in un-redefined (see gw_core_bridge_redefine_syms.txt);
# -lgcc is added back explicitly as a safety net for any libgcc runtime
# helper (64-bit divmod, etc.) that core_common doesn't bridge yet — see
# the "NOT implemented here" note in gw_core_bridge.c. It is entirely
# self-contained (no firmware calls), so pulling it in costs code size but
# never breaks the ABI boundary.
LDFLAGS = $(MCU) -nostdlib -lgcc $(CORE_LDLIBS) -T$(CORE_LDSCRIPT) -L$(ROOT_DIR)/ld \
-Wl,-Map=$(BUILD_DIR)/$(CORE_NAME)_core.map,--cref -Wl,--gc-sections \
-Wl,--no-check-sections

#######################################
# object lists
#######################################
BRIDGE_C_SOURCES := $(ROOT_DIR)/Core/Src/porting/core_common/gw_core_bridge.c
BRIDGE_ASM_SOURCES := $(ROOT_DIR)/Core/Src/porting/core_common/gw_core_entry.S
REDEFINE_SYMS_FILE := $(ROOT_DIR)/Core/Src/porting/core_common/gw_core_bridge_redefine_syms.txt

ALL_C_SOURCES := $(CORE_C_SOURCES) $(BRIDGE_C_SOURCES)
C_OBJECTS   := $(addprefix $(BUILD_DIR)/,$(notdir $(ALL_C_SOURCES:.c=.o)))
ASM_OBJECTS := $(addprefix $(BUILD_DIR)/,$(notdir $(BRIDGE_ASM_SOURCES:.S=.o)))
BRIDGE_OBJECTS := $(addprefix $(BUILD_DIR)/,$(notdir $(BRIDGE_C_SOURCES:.c=.o)))

vpath %.c $(sort $(dir $(ALL_C_SOURCES)))
vpath %.S $(sort $(dir $(BRIDGE_ASM_SOURCES)))

TARGET_ELF := $(BUILD_DIR)/$(CORE_NAME)_core.elf
TARGET_BIN := $(BUILD_DIR)/$(CORE_NAME)_core.bin

CORE_EXTRA_SEGMENTS ?=
# "region:section" -> just the region ("itcm:core_itcm" -> "itcm").
CORE_EXTRA_SEGMENT_REGIONS := $(foreach seg,$(CORE_EXTRA_SEGMENTS),$(word 1,$(subst :, ,$(seg))))
CORE_EXTRA_SEGMENT_BINS := $(foreach r,$(CORE_EXTRA_SEGMENT_REGIONS),$(BUILD_DIR)/$(CORE_NAME)_core_$(r).bin)

.PHONY: all clean
all: $(TARGET_BIN) $(CORE_EXTRA_SEGMENT_BINS)

$(BUILD_DIR):
	mkdir -p $(BUILD_DIR)

# gw_core_bridge.c defines the real memcpy/memset/memmove/__aeabi_mem*
# symbols (see its own comment) instead of routing them through the ABI —
# -fno-builtin-mem*: without this, GCC's loop-idiom recognition can decide
# our own byte/word-copy loops in memcpy()/memset() "look like" memcpy/
# memset and rewrite their bodies into a call to... memcpy/memset,
# infinitely recursing. Scoped to this one object so every OTHER core
# object still gets normal builtin recognition (compile-time-constant-size
# memcpy(a,b,4) calls inlined to a plain load/store instead of a real call).
$(BRIDGE_OBJECTS): CFLAGS += -fno-builtin-memcpy -fno-builtin-memmove -fno-builtin-memset

# Compile, then rename every libc/firmware symbol EXCEPT in the bridge
# object itself (which defines the core_<name> targets those renames point
# to — see gw_core_bridge_redefine_syms.txt header comment).
$(BUILD_DIR)/%.o: %.c $(REDEFINE_SYMS_FILE) | $(BUILD_DIR)
	$(CC) -c $(CFLAGS) $< -o $@
	if [ "$@" != "$(BRIDGE_OBJECTS)" ]; then \
		$(CP) --redefine-syms=$(REDEFINE_SYMS_FILE) $@ ; \
	fi

$(BUILD_DIR)/%.o: %.S | $(BUILD_DIR)
	$(AS) -c $(ASFLAGS) $< -o $@

$(TARGET_ELF): $(C_OBJECTS) $(ASM_OBJECTS) $(CORE_LDSCRIPT)
	$(CC) $(C_OBJECTS) $(ASM_OBJECTS) $(LDFLAGS) -o $@
	$(SZ) $@

# .bss is NOLOAD (see core_ram_emu.ld) so extracting just the loaded
# sections naturally emits exactly code_size bytes (__RAM_EMU_START__ ..
# __CORE_CODE_END__) with no BSS padding — tools/pack_core.py re-derives the
# same length from the ELF symbols directly rather than trusting the file
# size, but both must agree.
#
# Explicit --only-section=.core_entry --only-section=.data (segment 0's own
# output sections, see core_ram_emu.ld) instead of a whole-ELF `-O binary -S`
# dump: a plain whole-ELF dump lays out every loadable section by absolute
# address across ALL memory regions in one blob, so it silently produces a
# multi-hundred-KB file full of zero padding (or worse, corrupts pack_core.py's
# code_size math) the moment a second region (ITCM/AHB, via
# CORE_EXTRA_SEGMENTS) exists anywhere in the link — explicit sections don't
# have that problem since only sections placed in RAM_EMU are named here.
$(TARGET_BIN): $(TARGET_ELF)
	$(CP) -O binary --only-section=.core_entry --only-section=.data $< $@

# One rule per CORE_EXTRA_SEGMENTS entry: build/$(CORE_NAME)_core_<region>.bin
# from the `.{section}` output section your own CORE_LDSCRIPT defines for
# that region (e.g. "itcm:core_itcm" -> --only-section=.core_itcm). .bss for
# an extra segment must equally be NOLOAD in your linker script — same
# reasoning as segment 0 above.
define CORE_EXTRA_SEGMENT_RULE
$(BUILD_DIR)/$(CORE_NAME)_core_$(word 1,$(subst :, ,$(1))).bin: $(TARGET_ELF)
	$$(CP) -O binary --only-section=.$(word 2,$(subst :, ,$(1))) $$< $$@
endef
$(foreach seg,$(CORE_EXTRA_SEGMENTS),$(eval $(call CORE_EXTRA_SEGMENT_RULE,$(seg))))

# Double-colon: lets a concrete core's Makefile (e.g. cores/wsv/Makefile,
# after `include`ing this file) add its own `clean::` rule for anything it
# produces outside $(BUILD_DIR) (e.g. the packaged ../<core>.bin) without
# overriding this recipe (GNU Make runs every `clean::` rule in the order
# they were defined instead of only the last one, unlike plain `clean:`).
clean::
	rm -rf $(BUILD_DIR)

-include $(wildcard $(BUILD_DIR)/*.d)
