TARGET = gw_retro_go

DEBUG = 0

OPT = -O2 -ggdb3

# Default to bank 2 because sylverb's bootloader is installed at 0x08000000
# (bank 1) on this checkout. Override on the command line for non-bootloader
# setups, e.g. `make INTFLASH_BANK=1`.
INTFLASH_BANK ?= 2

GNWMANAGER ?= gnwmanager

# To enable verbose, append VERBOSE=1 to make, e.g.:
# make VERBOSE=1
ifneq ($(strip $(VERBOSE)),1)
V = @
endif

######################################
# source
######################################
# C sources
C_SOURCES =  \
Core/Src/porting/lib/lz4_depack.c \
Core/Src/porting/lib/lzma/LzmaDec.c \
Core/Src/porting/lib/lzma/lzma.c \
Core/Src/bilinear.c \
Core/Src/cpp_init_array.c \
Core/Src/gw_buttons.c \
Core/Src/gw_lcd.c \
Core/Src/gw_audio.c \
Core/Src/gw_malloc.c \
Core/Src/gw_flash.c \
Core/Src/gw_ofw.c \
Core/Src/error_screens.c \
Core/Src/main.c \
Core/Src/syscalls.c \
Core/Src/sha256.c \
Core/Src/bq24072.c \
Core/Src/porting/lib/hw_jpeg_decoder.c \
Core/Src/porting/lib/hw_sha1.c \
Core/Src/porting/common.c \
Core/Src/porting/odroid_audio.c \
Core/Src/porting/odroid_display.c \
Core/Src/porting/odroid_input.c \
Core/Src/porting/odroid_netplay.c \
Core/Src/porting/odroid_overlay.c \
Core/Src/porting/odroid_sdcard.c \
Core/Src/porting/odroid_system.c \
Core/Src/porting/crc32.c \
Core/Src/stm32h7xx_hal_msp.c \
Core/Src/stm32h7xx_it.c \
Core/Src/system_stm32h7xx.c

FATFS_DIR = Core/Src/porting/lib/FatFs
FATFS_C_SOURCES = \
$(FATFS_DIR)/user_diskio.c \
$(FATFS_DIR)/ff.c \
$(FATFS_DIR)/ffsystem.c \
$(FATFS_DIR)/ffunicode.c \
$(FATFS_DIR)/user_diskio_spi.c \
$(FATFS_DIR)/user_diskio_softspi.c

FROGFS_DIR = Core/Src/porting/lib/frogfs
FROGFS_C_SOURCES = \
Core/Src/retro-go/rg_frogfs.c \
$(FROGFS_DIR)/src/frogfs.c \
$(FROGFS_DIR)/src/decomp_raw.c

LITTLEFS_DIR = Core/Src/porting/lib/littlefs
LITTLEFS_C_SOURCES = \
$(LITTLEFS_DIR)/lfs.c \
$(LITTLEFS_DIR)/lfs_util.c

TAMP_DIR = Core/Src/porting/lib/tamp/tamp/_c_src/
TAMP_C_SOURCES = \
$(TAMP_DIR)/tamp/common.c \
$(TAMP_DIR)/tamp/compressor.c \
$(TAMP_DIR)/tamp/decompressor.c

# Add common C++ sources here
CXX_SOURCES = \
Core/Src/heap.cpp \

C_INCLUDES +=  \
-ICore/Inc \
-ICore/Src/porting/lib \
-ICore/Src/porting/lib/lzma \
-ICore/Src/porting/lib/FatFs \
-I./

FATFS_INCLUDES += \
-ICore/Src/porting/lib/FatFs

TAMP_C_INCLUDES += -I$(TAMP_DIR)

include Makefile.common


$(BUILD_DIR)/$(TARGET)_extflash.bin: $(BUILD_DIR)/$(TARGET).elf | $(BUILD_DIR)
	$(V)$(ECHO) [ BIN ] $(notdir $@)
	$(V)$(BIN) -j ._itcram_hot -j ._ram_exec -j ._extflash $< $(BUILD_DIR)/$(TARGET)_extflash.bin

$(BUILD_DIR)/$(TARGET)_intflash.bin: $(BUILD_DIR)/$(TARGET).elf | $(BUILD_DIR)
	$(V)$(ECHO) [ BIN ] $(notdir $@)
	$(V)$(BIN) -j .isr_vector -j .firmware_abi -j .text -j .rodata -j .ARM.extab -j .preinit_array -j .init_array -j .fini_array -j .data $< $(BUILD_DIR)/$(TARGET)_intflash.bin

$(BUILD_DIR)/$(TARGET)_intflash2.bin: $(BUILD_DIR)/$(TARGET).elf | $(BUILD_DIR)
	$(V)$(ECHO) [ BIN ] $(notdir $@)
	$(V)$(BIN) -j .flash2 $< $(BUILD_DIR)/$(TARGET)_intflash2.bin
