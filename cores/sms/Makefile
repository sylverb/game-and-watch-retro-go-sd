# Sega Master System family (smsplusgx-go, C) — standalone core (see
# cores/_template/Makefile and Core/Src/porting/core_common/CLAUDE.md).
#
# One binary (sms.bin) covers four launcher systems — SMS, Game Gear,
# SG-1000, Colecovision — matching scripts/sd_cores_pack.py's dirname map.
# Engine force flags are chosen at runtime from ACTIVE_FILE->ext (see
# main_smsplusgx.c). Coleco BIOS is expected at /bios/coleco/coleco.bin.

CORE_NAME  := sms
CORE_ENTRY := app_main_smsplusgx

CORE_SMS := ../../retro-go-stm32/smsplusgx-go/components/smsplus
CORE_PORTING_SMS := ../../Core/Src/porting/smsplusgx

CORE_C_SOURCES := \
$(CORE_SMS)/loadrom.c \
$(CORE_SMS)/render.c \
$(CORE_SMS)/sms.c \
$(CORE_SMS)/state.c \
$(CORE_SMS)/vdp.c \
$(CORE_SMS)/pio.c \
$(CORE_SMS)/tms.c \
$(CORE_SMS)/memz80.c \
$(CORE_SMS)/system.c \
$(CORE_SMS)/cpu/z80.c \
$(CORE_SMS)/sound/emu2413.c \
$(CORE_SMS)/sound/fmintf.c \
$(CORE_SMS)/sound/sn76489.c \
$(CORE_SMS)/sound/sms_sound.c \
$(CORE_SMS)/sound/ym2413.c \
$(CORE_PORTING_SMS)/main_smsplusgx.c

CORE_C_INCLUDES := \
-I$(CORE_SMS) \
-I$(CORE_SMS)/cpu \
-I$(CORE_SMS)/sound \
-I../../Core/Inc/porting/smsplusgx

CORE_C_DEFS := -DTARGET_GNW

include ../_template/Makefile

PACKED_BIN := ../sms.bin

.PHONY: pack
pack: $(TARGET_BIN)
	python3 ../../tools/pack_core.py \
		--elf $(TARGET_ELF) --bin $(TARGET_BIN) \
		--system name="Sega Master System",dirname=sms,ext=sms,parse=rom,pad_logo=../../Core/Src/retro-go/rg_logos.c:pad_sms,header_logo=../../Core/Src/retro-go/rg_logos.c:header_sms \
		--system name="Sega Game Gear",dirname=gg,ext=gg,parse=rom,pad_logo=../../Core/Src/retro-go/rg_logos.c:pad_gg,header_logo=../../Core/Src/retro-go/rg_logos.c:header_gg \
		--system name="Sega SG-1000",dirname=sg,ext=sg,parse=rom,pad_logo=../../Core/Src/retro-go/rg_logos.c:pad_sg1000,header_logo=../../Core/Src/retro-go/rg_logos.c:header_sg1000 \
		--system name="Colecovision",dirname=col,ext=col,parse=rom,pad_logo=../../Core/Src/retro-go/rg_logos.c:pad_col,header_logo=../../Core/Src/retro-go/rg_logos.c:header_col \
		--core-name "SMSPlus GX" \
		--out $(PACKED_BIN)

all: pack

clean::
	rm -f $(PACKED_BIN)
