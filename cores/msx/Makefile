# MSX (blueMSX-go, C) — standalone core (see cores/_template/Makefile and
# Core/Src/porting/core_common/CLAUDE.md).
#
# BIOS ROMs live under /bios/msx/ (prepared by prepare_msx_bios_files in
# Makefile.common). msxromdb.bin is generated there from msxromdb.xml.
# Compressed ROM paths (#ifndef GNW_DISABLE_COMPRESSION) are kept in
# main_msx.c / msx_database.c; SD builds define GNW_DISABLE_COMPRESSION.

CORE_NAME  := msx
CORE_ENTRY := app_main_msx

CORE_MSX := ../../external/blueMSX-go
LIBRETRO_COMM_DIR := $(CORE_MSX)/libretro-common
CORE_PORTING_MSX := ../../Core/Src/porting/msx

CORE_C_SOURCES := \
$(CORE_MSX)/Src/Libretro/Timer.c \
$(CORE_MSX)/Src/Libretro/Emulator.c \
$(CORE_MSX)/Src/Bios/Patch.c \
$(CORE_MSX)/Src/Memory/DeviceManager.c \
$(CORE_MSX)/Src/Memory/IoPort.c \
$(CORE_MSX)/Src/Memory/MegaromCartridge.c \
$(CORE_MSX)/Src/Memory/ramNormal.c \
$(CORE_MSX)/Src/Memory/ramMapper.c \
$(CORE_MSX)/Src/Memory/ramMapperIo.c \
$(CORE_MSX)/Src/Memory/RomLoader.c \
$(CORE_MSX)/Src/Memory/romMapperASCII8.c \
$(CORE_MSX)/Src/Memory/romMapperASCII16.c \
$(CORE_MSX)/Src/Memory/romMapperASCII16X.c \
$(CORE_MSX)/Src/Memory/romMapperNEO16.c \
$(CORE_MSX)/Src/Memory/romMapperASCII16nf.c \
$(CORE_MSX)/Src/Memory/romMapperBasic.c \
$(CORE_MSX)/Src/Memory/romMapperCasette.c \
$(CORE_MSX)/Src/Memory/romMapperDRAM.c \
$(CORE_MSX)/Src/Memory/romMapperF4device.c \
$(CORE_MSX)/Src/Memory/romMapperKoei.c \
$(CORE_MSX)/Src/Memory/romMapperKonami4.c \
$(CORE_MSX)/Src/Memory/romMapperKonami4nf.c \
$(CORE_MSX)/Src/Memory/romMapperKonami5.c \
$(CORE_MSX)/Src/Memory/romMapperLodeRunner.c \
$(CORE_MSX)/Src/Memory/romMapperMsxDos2.c \
$(CORE_MSX)/Src/Memory/romMapperMsxMusic.c \
$(CORE_MSX)/Src/Memory/romMapperNormal.c \
$(CORE_MSX)/Src/Memory/romMapperPlain.c \
$(CORE_MSX)/Src/Memory/romMapperRType.c \
$(CORE_MSX)/Src/Memory/romMapperStandard.c \
$(CORE_MSX)/Src/Memory/romMapperSunriseIDE.c \
$(CORE_MSX)/Src/Memory/romMapperSCCplus.c \
$(CORE_MSX)/Src/Memory/romMapperTC8566AF.c \
$(CORE_MSX)/Src/Memory/SlotManager.c \
$(CORE_MSX)/Src/VideoChips/VDP_YJK_gnw.c \
$(CORE_MSX)/Src/VideoChips/VDP_MSX.c \
$(CORE_MSX)/Src/VideoChips/V9938.c \
$(CORE_MSX)/Src/VideoChips/VideoManager.c \
$(CORE_MSX)/Src/Z80/R800.c \
$(CORE_MSX)/Src/Z80/R800SaveState.c \
$(CORE_MSX)/Src/Input/JoystickPort.c \
$(CORE_MSX)/Src/Input/MsxJoystick.c \
$(CORE_MSX)/Src/IoDevice/Disk.c \
$(CORE_MSX)/Src/IoDevice/HarddiskIDE.c \
$(CORE_MSX)/Src/IoDevice/I8255.c \
$(CORE_MSX)/Src/IoDevice/MsxPPI.c \
$(CORE_MSX)/Src/IoDevice/RTC.c \
$(CORE_MSX)/Src/IoDevice/SunriseIDE.c \
$(CORE_MSX)/Src/IoDevice/TC8566AF.c \
$(CORE_MSX)/Src/SoundChips/AudioMixer.c \
$(CORE_MSX)/Src/SoundChips/AY8910.c \
$(CORE_MSX)/Src/SoundChips/SCC.c \
$(CORE_MSX)/Src/SoundChips/MsxPsg.c \
$(CORE_MSX)/Src/SoundChips/YM2413_msx.c \
$(CORE_MSX)/Src/SoundChips/emu2413_msx.c \
$(CORE_MSX)/Src/Emulator/AppConfig.c \
$(CORE_MSX)/Src/Emulator/LaunchFile.c \
$(CORE_MSX)/Src/Emulator/Properties.c \
$(CORE_MSX)/Src/Utils/IsFileExtension.c \
$(CORE_MSX)/Src/Utils/StrcmpNoCase.c \
$(CORE_MSX)/Src/Utils/TokenExtract.c \
$(CORE_MSX)/Src/Board/Board.c \
$(CORE_MSX)/Src/Board/Machine.c \
$(CORE_MSX)/Src/Board/MSX.c \
$(CORE_MSX)/Src/Input/InputEvent.c \
$(CORE_PORTING_MSX)/main_msx.c \
$(CORE_PORTING_MSX)/msx_i18n.c \
$(CORE_PORTING_MSX)/msx_database.c \
$(CORE_PORTING_MSX)/save_msx.c

CORE_C_INCLUDES := \
-I$(CORE_MSX) \
-I$(LIBRETRO_COMM_DIR)/include \
-I$(CORE_MSX)/Src/Arch \
-I$(CORE_MSX)/Src/Bios \
-I$(CORE_MSX)/Src/Board \
-I$(CORE_MSX)/Src/BuildInfo \
-I$(CORE_MSX)/Src/Common \
-I$(CORE_MSX)/Src/Debugger \
-I$(CORE_MSX)/Src/Emulator \
-I$(CORE_MSX)/Src/IoDevice \
-I$(CORE_MSX)/Src/Language \
-I$(CORE_MSX)/Src/Media \
-I$(CORE_MSX)/Src/Memory \
-I$(CORE_MSX)/Src/Resources \
-I$(CORE_MSX)/Src/SoundChips \
-I$(CORE_MSX)/Src/TinyXML \
-I$(CORE_MSX)/Src/Utils \
-I$(CORE_MSX)/Src/VideoChips \
-I$(CORE_MSX)/Src/VideoRender \
-I$(CORE_MSX)/Src/Z80 \
-I$(CORE_MSX)/Src/Input \
-I$(CORE_MSX)/Src/Libretro \
-I../../Core/Inc/porting/msx \
-I../../Core/Src/porting/lib/lzma

# COVERFLOW+CHEAT_CODES must match the firmware layout (ACTIVE_FILE->cheat_*
# sits after COVERFLOW fields). blueMSX SlotManager references MAX_CHEAT_CODES.
CORE_C_DEFS := \
-DTARGET_GNW \
-D__LIBRETRO__ \
-DZ80_CUSTOM_CONFIGURATION \
-DNO_EMBEDDED_SAMPLES \
-DMSX_NO_ZIP \
-DMSX_NO_FILESYSTEM \
-DMSX_NO_MALLOC \
-DMAX_VIDEO_WIDTH_320 \
-DPIXEL_WIDTH=8 \
-DMSX_NO_STEREO \
-DMSX_USE_BANK_2=0 \
-DCOVERFLOW=1 \
-DCHEAT_CODES=1 \
-DMAX_CHEAT_CODES=13

# emu2413 rate converter builds a sinc table with sin/cos at init.
CORE_LDLIBS := -lm

# ITCM for R800 + SlotManager; former itc_malloc heap → DTCM arena. blueMSX
# calls dtcm_arena_malloc()/dtcm_arena_calloc() by name for that traffic
# (see external/blueMSX-go), no core_itc_* override needed.
CORE_LDSCRIPT := msx_core.ld
CORE_EXTRA_SEGMENTS := itcm:core_itcm

MSX_CFLAGS_O2 = $(filter-out -Os,$(CFLAGS)) -O2

include ../_template/Makefile

$(BUILD_DIR)/R800.o:              CFLAGS := $(MSX_CFLAGS_O2)
$(BUILD_DIR)/SlotManager.o:       CFLAGS := $(MSX_CFLAGS_O2)
$(BUILD_DIR)/IoPort.o:            CFLAGS := $(MSX_CFLAGS_O2)
$(BUILD_DIR)/DeviceManager.o:     CFLAGS := $(MSX_CFLAGS_O2)
$(BUILD_DIR)/ramNormal.o:         CFLAGS := $(MSX_CFLAGS_O2)
$(BUILD_DIR)/ramMapper.o:         CFLAGS := $(MSX_CFLAGS_O2)
$(BUILD_DIR)/romMapperNormal.o:   CFLAGS := $(MSX_CFLAGS_O2)
$(BUILD_DIR)/romMapperASCII8.o:   CFLAGS := $(MSX_CFLAGS_O2)
$(BUILD_DIR)/romMapperASCII16.o:  CFLAGS := $(MSX_CFLAGS_O2)
$(BUILD_DIR)/romMapperKonami4.o:  CFLAGS := $(MSX_CFLAGS_O2)
$(BUILD_DIR)/romMapperKonami5.o:  CFLAGS := $(MSX_CFLAGS_O2)
$(BUILD_DIR)/romMapperSCCplus.o:  CFLAGS := $(MSX_CFLAGS_O2)
$(BUILD_DIR)/VDP_MSX.o:           CFLAGS := $(MSX_CFLAGS_O2)
$(BUILD_DIR)/V9938.o:             CFLAGS := $(MSX_CFLAGS_O2)
$(BUILD_DIR)/VDP_YJK_gnw.o:       CFLAGS := $(MSX_CFLAGS_O2)
$(BUILD_DIR)/AudioMixer.o:        CFLAGS := $(MSX_CFLAGS_O2)
$(BUILD_DIR)/AY8910.o:            CFLAGS := $(MSX_CFLAGS_O2)
$(BUILD_DIR)/SCC.o:               CFLAGS := $(MSX_CFLAGS_O2)
$(BUILD_DIR)/MsxPsg.o:            CFLAGS := $(MSX_CFLAGS_O2)
$(BUILD_DIR)/YM2413_msx.o:        CFLAGS := $(MSX_CFLAGS_O2)
$(BUILD_DIR)/emu2413_msx.o:       CFLAGS := $(MSX_CFLAGS_O2)
$(BUILD_DIR)/main_msx.o:          CFLAGS := $(MSX_CFLAGS_O2)

PACKED_BIN := ../msx.bin

.PHONY: pack
pack: $(TARGET_BIN) $(BUILD_DIR)/msx_core_itcm.bin
	python3 ../../tools/pack_core.py \
		--elf $(TARGET_ELF) --bin $(TARGET_BIN) \
		--system name="MSX",dirname=msx,ext="dsk rom mx1 mx2 cdk lzma",parse=rom,pad_logo=../../Core/Src/retro-go/rg_logos.c:pad_msx,header_logo=../../Core/Src/retro-go/rg_logos.c:header_msx \
		--segment itcm:__ITCM_CORE_START__:__CORE_ITCM_CODE_END__:__CORE_ITCM_BSS_END__:$(BUILD_DIR)/msx_core_itcm.bin \
		--core-name "blueMSX" \
		--out $(PACKED_BIN)

all: pack

clean::
	rm -f $(PACKED_BIN)
