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
#   CORE_ENTRY       entry symbol, e.g. app_main_wsv (must exist in CORE_C_SOURCES
#                    or CORE_CXX_SOURCES — a C++ entry point needs `extern "C"`,
#                    see cores/gb_tgbdual/Makefile)
#   CORE_C_SOURCES   .c files to build, repo-root-relative paths. Object
#                    files land in a single flat build/ dir (vpath-based,
#                    like the root Makefile) — every CORE_C_SOURCES/
#                    CORE_CXX_SOURCES entry must have a basename unique
#                    across the whole combined list.
#
# Optional:
#   CORE_C_INCLUDES  extra -I flags, on top of the common set below
#   CORE_C_DEFS      extra -D flags, on top of the common set below
#   CORE_CXX_SOURCES .cpp/.cxx/.cc files to build (e.g. cores/gb_tgbdual/Makefile,
#                    cores/a2600/Makefile, cores/gba/Makefile's cpu.cc) —
#                    compiled with $(CXX)/$(CXXFLAGS) instead of
#                    $(CC)/$(CFLAGS), everything else (redefine-syms pass,
#                    vpath, flat build/ dir) identical to CORE_C_SOURCES.
#                    Setting this non-empty also pulls in
#                    Core/Src/porting/core_common/gw_core_cxx_support.cpp
#                    (operator new/delete, __cxa_pure_virtual, the
#                    heap_alloc_mem() family — see that file's header
#                    comment) automatically, same way BRIDGE_C_SOURCES is
#                    always pulled in below. Most C++ cores build WITHOUT
#                    libstdc++ (no -lstdc++ in LDFLAGS) — stick to classes/
#                    virtual dispatch/new/delete; avoid STL containers,
#                    exceptions (CXXFLAGS below passes -fno-exceptions,
#                    so `throw`/`try`/`catch` are hard compile errors, not
#                    silent runtime breakage) and RTTI (-fno-rtti, `dynamic_cast`/
#                    `typeid` likewise won't compile). Exception: Stella
#                    (cores/a2600) needs std::string and sets
#                    CORE_LDLIBS += -lstdc++.
#   CORE_ASM_SOURCES .S files to build (e.g. cores/gba/Makefile's gba_bios.S)
#                    — assembled with $(AS)/$(ASFLAGS), then the same
#                    redefine-syms pass as C/CXX objects (bridge entry
#                    gw_core_entry.S is excluded; it is always linked in
#                    separately via BRIDGE_ASM_SOURCES).
#   CORE_ASFLAGS     extra assembler flags (e.g. -I for .incbin search paths).
#   CORE_EXTRA_REDEFINE_SYMS  optional second redefine-syms file (e.g.
#                    gba_redefines). Applied after REDEFINE_SYMS_FILE on
#                    every non-bridge object (.c/.cpp/.cxx/.cc/.S).
#   CORE_LDSCRIPT    defaults to $(TEMPLATE_DIR)/core_ram_emu.ld. Point this
#                    at your own script (INCLUDE-ing ld/gnw_itcm_core.ld /
#                    ld/gnw_ahb_core.ld as needed) if you use CORE_EXTRA_SEGMENTS
#                    — see cores/pce/pce_core.ld for an ITCM example.
#   CORE_LDLIBS      extra -l flags, e.g. -lm (cores/md) or -lstdc++
#                    (cores/a2600: Stella's std::string). Linked after -lgcc.
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

# Relative to the core's cwd (always cores/<name>/ via `make -C`). Do NOT
# use $(abspath): a host build would bake /Users/... into Make prerequisites
# and into gcc -MMD .d files; Docker remounts the same tree at /opt/workdir,
# and the next incremental `make docker` then fails with
# "No rule to make target '/Users/.../gw_core_bridge.c'".
TEMPLATE_DIR := $(patsubst %/,%,$(dir $(lastword $(MAKEFILE_LIST))))
ROOT_DIR     := $(TEMPLATE_DIR)/../..
BUILD_DIR    := build

CORE_LDSCRIPT ?= $(TEMPLATE_DIR)/core_ram_emu.ld
CORE_ASFLAGS ?=
CORE_EXTRA_REDEFINE_SYMS ?=

#######################################
# toolchain (mirrors Makefile.common)
#######################################
PREFIX = arm-none-eabi-
ifdef GCC_PATH
CC   = $(GCC_PATH)/$(PREFIX)gcc
CXX  = $(GCC_PATH)/$(PREFIX)g++
AS   = $(GCC_PATH)/$(PREFIX)gcc -x assembler-with-cpp
CP   = $(GCC_PATH)/$(PREFIX)objcopy
NM   = $(GCC_PATH)/$(PREFIX)nm
SZ   = $(GCC_PATH)/$(PREFIX)size
else
CC   = $(PREFIX)gcc
CXX  = $(PREFIX)g++
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
# COVERFLOW / CHEAT_CODES: default 0 unless the concrete core's CORE_C_DEFS
# already sets them (e.g. NES reads ACTIVE_FILE->cheat_* after COVERFLOW
# fields and must match the firmware's layout). Avoids a double -D and the
# "redefined" warning / fragile CFLAGS rewrite after include.
CORE_COMMON_C_DEFS := \
-DUSE_HAL_DRIVER \
-DSTM32H7B0xx \
-DSD_CARD=1 \
-DGNW_DISABLE_COMPRESSION \
-DIS_LITTLE_ENDIAN \
$(if $(filter -DCOVERFLOW=%,$(CORE_C_DEFS)),,-DCOVERFLOW=0) \
$(if $(filter -DCHEAT_CODES=%,$(CORE_C_DEFS)),,-DCHEAT_CODES=0) \
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

ASFLAGS = $(MCU) $(OPT) $(CORE_COMMON_C_DEFS) $(CORE_C_DEFS) $(CORE_ASFLAGS)

# -fno-exceptions -fno-rtti: no libstdc++ is linked (see CORE_CXX_SOURCES
# comment above) — nothing here could catch a thrown exception or unwind
# through one anyway (no personality routine, no .ARM.exidx, both
# discarded by the linker script), so make that a compile-time error
# instead of a link-time or (worse) runtime one.
# -fno-threadsafe-statics: skips emitting a guard-variable + __cxa_guard_*
# acquire/release pair around function-local static initialization — this
# core has no threads (one core runs at a time) so the guard is pure
# overhead, and it would otherwise be one more libstdc++ runtime symbol
# core_common would need to stub out.
# -fno-use-cxa-atexit: global object destructors would normally register
# via __cxa_atexit(dtor, obj, __dso_handle) for a well-defined C++ runtime
# shutdown order — this image never "exits" (see core_ram_emu.ld's
# .fini_array discard), so that bookkeeping is pure overhead too; falls
# back to the plain atexit() model instead (also unused here in practice,
# but doesn't require a real __cxa_atexit implementation).
CXXFLAGS = $(CFLAGS) -fno-exceptions -fno-rtti -fno-threadsafe-statics -fno-use-cxa-atexit

# -nostdlib: no newlib, no crt0/crti/crtn — this image has no _start (the
# firmware jumps straight into RAM at offset 0, see gw_core_entry.S) and no
# libc symbol may sneak in un-redefined (see gw_core_bridge_redefine_syms.txt);
# -lgcc is added back explicitly as a safety net for any libgcc runtime
# helper (64-bit divmod, etc.) that core_common doesn't bridge yet — see
# the "NOT implemented here" note in gw_core_bridge.c. It is entirely
# self-contained (no firmware calls), so pulling it in costs code size but
# never breaks the ABI boundary.
# CORE_LDLIBS (e.g. -lstdc++) before -lgcc so EH helpers resolve against our
# stubs in gw_core_cxx_support.cpp / libgcc rather than leaving dangling
# libstdc++→libgcc unwind refs unsatisfied.
LDFLAGS = $(MCU) -nostdlib $(CORE_LDLIBS) -lgcc -T$(CORE_LDSCRIPT) -L$(ROOT_DIR)/ld \
-Wl,-Map=$(BUILD_DIR)/$(CORE_NAME)_core.map,--cref -Wl,--gc-sections \
-Wl,--no-check-sections

#######################################
# object lists
#######################################
BRIDGE_C_SOURCES := $(ROOT_DIR)/Core/Src/porting/core_common/gw_core_bridge.c
BRIDGE_ASM_SOURCES := $(ROOT_DIR)/Core/Src/porting/core_common/gw_core_entry.S
REDEFINE_SYMS_FILE := $(ROOT_DIR)/Core/Src/porting/core_common/gw_core_bridge_redefine_syms.txt

# See CORE_CXX_SOURCES' doc comment above: setting it non-empty auto-pulls
# in the shared C++ runtime support file.
ifneq ($(CORE_CXX_SOURCES),)
CORE_CXX_SOURCES += $(ROOT_DIR)/Core/Src/porting/core_common/gw_core_cxx_support.cpp
endif

ALL_C_SOURCES   := $(CORE_C_SOURCES) $(BRIDGE_C_SOURCES)
ALL_CXX_SOURCES := $(CORE_CXX_SOURCES)
ALL_ASM_SOURCES := $(CORE_ASM_SOURCES) $(BRIDGE_ASM_SOURCES)
C_OBJECTS   := $(addprefix $(BUILD_DIR)/,$(notdir $(ALL_C_SOURCES:.c=.o)))
# Accept .cpp (handy-go / tgbdual), .cxx (Stella / a2600), and .cc (gpSP / gba).
CXX_OBJECTS := $(addprefix $(BUILD_DIR)/,$(notdir $(patsubst %.cc,%.o,$(patsubst %.cxx,%.o,$(patsubst %.cpp,%.o,$(ALL_CXX_SOURCES))))))
ASM_OBJECTS := $(addprefix $(BUILD_DIR)/,$(notdir $(ALL_ASM_SOURCES:.S=.o)))
BRIDGE_OBJECTS := $(addprefix $(BUILD_DIR)/,$(notdir $(BRIDGE_C_SOURCES:.c=.o)))
BRIDGE_ASM_OBJECTS := $(addprefix $(BUILD_DIR)/,$(notdir $(BRIDGE_ASM_SOURCES:.S=.o)))

# After the bridge redefine-syms pass, optionally apply CORE_EXTRA_REDEFINE_SYMS.
define CORE_APPLY_EXTRA_REDEFINE
	if [ -n "$(CORE_EXTRA_REDEFINE_SYMS)" ]; then \
		$(CP) --redefine-syms=$(CORE_EXTRA_REDEFINE_SYMS) $1 ; \
	fi
endef

vpath %.c $(sort $(dir $(ALL_C_SOURCES)))
vpath %.cpp $(sort $(dir $(ALL_CXX_SOURCES)))
vpath %.cxx $(sort $(dir $(ALL_CXX_SOURCES)))
vpath %.cc $(sort $(dir $(ALL_CXX_SOURCES)))
vpath %.S $(sort $(dir $(ALL_ASM_SOURCES)))

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
# to — see gw_core_bridge_redefine_syms.txt header comment). Optional
# CORE_EXTRA_REDEFINE_SYMS (e.g. gba_redefines) runs after the bridge pass.
$(BUILD_DIR)/%.o: %.c $(REDEFINE_SYMS_FILE) | $(BUILD_DIR)
	$(CC) -c $(CFLAGS) $< -o $@
	if [ "$@" != "$(BRIDGE_OBJECTS)" ]; then \
		$(CP) --redefine-syms=$(REDEFINE_SYMS_FILE) $@ ; \
		$(call CORE_APPLY_EXTRA_REDEFINE,$@) \
	fi

# Bridge entry (gw_core_entry.S) is assembled as-is. Other CORE_ASM_SOURCES
# go through the same redefine-syms passes as C/CXX objects.
$(BUILD_DIR)/%.o: %.S $(REDEFINE_SYMS_FILE) | $(BUILD_DIR)
	$(AS) -c $(ASFLAGS) $< -o $@
	if [ "$@" != "$(BRIDGE_ASM_OBJECTS)" ]; then \
		$(CP) --redefine-syms=$(REDEFINE_SYMS_FILE) $@ ; \
		$(call CORE_APPLY_EXTRA_REDEFINE,$@) \
	fi

# .cpp objects always go through the redefine-syms pass (unlike the .c rule
# above, there's no "except the bridge object" case here — gw_core_bridge.c
# itself is plain C; gw_core_cxx_support.cpp, the one C++ file that also
# defines core_common-style trampolines/helpers, still needs its OWN
# ram_malloc()/itc_malloc()/ahb_calloc() calls renamed to core_ram_malloc()/
# etc. to resolve against the C bridge, so it's a normal core source file
# from this rule's point of view).
$(BUILD_DIR)/%.o: %.cpp $(REDEFINE_SYMS_FILE) | $(BUILD_DIR)
	$(CXX) -c $(CXXFLAGS) $< -o $@
	$(CP) --redefine-syms=$(REDEFINE_SYMS_FILE) $@
	$(call CORE_APPLY_EXTRA_REDEFINE,$@)

# Stella (and a few other older C++ ports) use .cxx rather than .cpp —
# identical compile/redefine-syms path.
$(BUILD_DIR)/%.o: %.cxx $(REDEFINE_SYMS_FILE) | $(BUILD_DIR)
	$(CXX) -c $(CXXFLAGS) $< -o $@
	$(CP) --redefine-syms=$(REDEFINE_SYMS_FILE) $@
	$(call CORE_APPLY_EXTRA_REDEFINE,$@)

# gpSP (cores/gba) uses .cc — identical compile/redefine-syms path.
$(BUILD_DIR)/%.o: %.cc $(REDEFINE_SYMS_FILE) | $(BUILD_DIR)
	$(CXX) -c $(CXXFLAGS) $< -o $@
	$(CP) --redefine-syms=$(REDEFINE_SYMS_FILE) $@
	$(call CORE_APPLY_EXTRA_REDEFINE,$@)

$(TARGET_ELF): $(C_OBJECTS) $(CXX_OBJECTS) $(ASM_OBJECTS) $(CORE_LDSCRIPT)
	$(CC) $(C_OBJECTS) $(CXX_OBJECTS) $(ASM_OBJECTS) $(LDFLAGS) -o $@
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

# Host↔Docker remount: old builds used $(abspath) so .d files list
# /Users/... or /opt/workdir/... prerequisites. If those absolute paths
# don't belong to *this* checkout root, drop every .d (objects stay) so
# Make falls back to the pattern rules instead of "No rule to make
# target '/Users/.../gw_core_bridge.c'".
$(shell \
	if ls $(BUILD_DIR)/*.d >/dev/null 2>&1; then \
		root_abs=$$(cd $(ROOT_DIR) && pwd); \
		if grep -E -l -- ' /|:[ ]+/' $(BUILD_DIR)/*.d >/dev/null 2>&1 && \
		   ! grep -F -q -- "$$root_abs" $(BUILD_DIR)/*.d; then \
			rm -f $(BUILD_DIR)/*.d; \
		fi; \
	fi \
)

-include $(wildcard $(BUILD_DIR)/*.d)
