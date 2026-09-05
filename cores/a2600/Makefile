# Atari 2600 (stella2014-go, C++) — standalone core (see
# cores/_template/Makefile and Core/Src/porting/core_common/CLAUDE.md).
#
# Stella uses std::string extensively (bspf.hxx), so unlike gb_tgbdual /
# lynx this core links -lstdc++ (CORE_LDLIBS below). Operator new/delete
# still come from gw_core_cxx_support.cpp; libstdc++ only supplies the
# string/locale bits Stella needs.
#
# Also produces ../a2600_defprops.bin (ROM properties database) via
# convert_defprops.py — loaded at runtime from /cores/a2600_defprops.bin
# (not linked into the core ELF). scripts/sd_cores_pack.py already expects
# both a2600.bin and a2600_defprops.bin for the "a2600" dirname.

CORE_NAME  := a2600
CORE_ENTRY := app_main_a2600

CORE_A2600 := ../../external/stella2014-go
CORE_PORTING_A2600 := ../../Core/Src/porting/a2600

CORE_C_SOURCES := \
$(CORE_A2600)/stella/src/emucore/DefPropsBin.c

CORE_CXX_SOURCES := \
$(CORE_PORTING_A2600)/main_a2600.cxx \
$(CORE_A2600)/stella/src/common/StellaSound.cxx \
$(CORE_A2600)/stella/src/emucore/Booster.cxx \
$(CORE_A2600)/stella/src/emucore/StellaCart.cxx \
$(CORE_A2600)/stella/src/emucore/Cart0840.cxx \
$(CORE_A2600)/stella/src/emucore/Cart2K.cxx \
$(CORE_A2600)/stella/src/emucore/Cart3E.cxx \
$(CORE_A2600)/stella/src/emucore/Cart3F.cxx \
$(CORE_A2600)/stella/src/emucore/Cart4A50.cxx \
$(CORE_A2600)/stella/src/emucore/Cart4K.cxx \
$(CORE_A2600)/stella/src/emucore/Cart4KSC.cxx \
$(CORE_A2600)/stella/src/emucore/CartAR.cxx \
$(CORE_A2600)/stella/src/emucore/CartBF.cxx \
$(CORE_A2600)/stella/src/emucore/CartBFSC.cxx \
$(CORE_A2600)/stella/src/emucore/CartCM.cxx \
$(CORE_A2600)/stella/src/emucore/CartCTY.cxx \
$(CORE_A2600)/stella/src/emucore/CartCV.cxx \
$(CORE_A2600)/stella/src/emucore/CartDF.cxx \
$(CORE_A2600)/stella/src/emucore/CartDFSC.cxx \
$(CORE_A2600)/stella/src/emucore/CartDPC.cxx \
$(CORE_A2600)/stella/src/emucore/CartDPCPlus.cxx \
$(CORE_A2600)/stella/src/emucore/CartE0.cxx \
$(CORE_A2600)/stella/src/emucore/CartE7.cxx \
$(CORE_A2600)/stella/src/emucore/CartEF.cxx \
$(CORE_A2600)/stella/src/emucore/CartEFSC.cxx \
$(CORE_A2600)/stella/src/emucore/CartF0.cxx \
$(CORE_A2600)/stella/src/emucore/CartF4.cxx \
$(CORE_A2600)/stella/src/emucore/CartF4SC.cxx \
$(CORE_A2600)/stella/src/emucore/CartF6.cxx \
$(CORE_A2600)/stella/src/emucore/CartF6SC.cxx \
$(CORE_A2600)/stella/src/emucore/CartF8.cxx \
$(CORE_A2600)/stella/src/emucore/CartF8SC.cxx \
$(CORE_A2600)/stella/src/emucore/CartFA.cxx \
$(CORE_A2600)/stella/src/emucore/CartFA2.cxx \
$(CORE_A2600)/stella/src/emucore/CartFE.cxx \
$(CORE_A2600)/stella/src/emucore/CartMC.cxx \
$(CORE_A2600)/stella/src/emucore/CartSB.cxx \
$(CORE_A2600)/stella/src/emucore/CartUA.cxx \
$(CORE_A2600)/stella/src/emucore/CartX07.cxx \
$(CORE_A2600)/stella/src/emucore/StellaConsole.cxx \
$(CORE_A2600)/stella/src/emucore/StellaControl.cxx \
$(CORE_A2600)/stella/src/emucore/StellaJoystick.cxx \
$(CORE_A2600)/stella/src/emucore/StellaM6502.cxx \
$(CORE_A2600)/stella/src/emucore/StellaM6532.cxx \
$(CORE_A2600)/stella/src/emucore/NullDev.cxx \
$(CORE_A2600)/stella/src/emucore/Random.cxx \
$(CORE_A2600)/stella/src/emucore/Serializer.cxx \
$(CORE_A2600)/stella/src/emucore/StateManager.cxx \
$(CORE_A2600)/stella/src/emucore/StellaMD5.cxx \
$(CORE_A2600)/stella/src/emucore/StellaSettings.cxx \
$(CORE_A2600)/stella/src/emucore/StellaSwitches.cxx \
$(CORE_A2600)/stella/src/emucore/StellaSystem.cxx \
$(CORE_A2600)/stella/src/emucore/StellaTIA.cxx \
$(CORE_A2600)/stella/src/emucore/TIATables.cxx \
$(CORE_A2600)/stella/src/emucore/TIASnd.cxx \
$(CORE_A2600)/stella/src/emucore/Driving.cxx \
$(CORE_A2600)/stella/src/emucore/MindLink.cxx \
$(CORE_A2600)/stella/src/emucore/Paddles.cxx \
$(CORE_A2600)/stella/src/emucore/TrackBall.cxx \
$(CORE_A2600)/stella/src/emucore/StellaGenesis.cxx \
$(CORE_A2600)/stella/src/emucore/StellaKeyboard.cxx

CORE_C_INCLUDES := \
-I$(CORE_A2600)/stella \
-I$(CORE_A2600)/stella/src \
-I$(CORE_A2600)/stella/stubs \
-I$(CORE_A2600)/stella/src/emucore \
-I$(CORE_A2600)/stella/src/common \
-I$(CORE_A2600)/stella/src/gui \
-I$(CORE_A2600)/libretro-common/include \
-I../../Core/Inc/porting/a2600

CORE_C_DEFS := -DTARGET_GNW

# Stella needs std::string (see bspf.hxx) — every other C++ core avoids this.
CORE_LDLIBS := -lstdc++

include ../_template/Makefile

PACKED_BIN := ../a2600.bin
DEFPROPS_BIN := ../a2600_defprops.bin
DEFPROPS_SRC := $(CORE_A2600)/stella/src/emucore/DefProps.hxx
CONVERT_DEFPROPS := $(CORE_A2600)/convert_defprops.py

.PHONY: pack
pack: $(TARGET_BIN) $(DEFPROPS_BIN)
	python3 ../../tools/pack_core.py \
		--elf $(TARGET_ELF) --bin $(TARGET_BIN) \
		--system-name "Atari 2600" --dirname a2600 \
		--extensions "a26 bin" \
		--pad-logo-c ../../Core/Src/retro-go/rg_logos.c:pad_a2600 \
		--header-logo-c ../../Core/Src/retro-go/rg_logos.c:header_a2600 \
		--core-name "Stella 2014" \
		--out $(PACKED_BIN)

# ROM properties database (sidecar, not part of the CORE payload) — same
# convert_defprops.py the old monolithic build used to feed create_sd_data.
$(DEFPROPS_BIN): $(DEFPROPS_SRC) $(CONVERT_DEFPROPS)
	python3 $(CONVERT_DEFPROPS) $(DEFPROPS_SRC) $@

all: pack

clean::
	rm -f $(PACKED_BIN) $(DEFPROPS_BIN)
