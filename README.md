![](assets/gnw.gif)

# Nintendo® Game & Watch™ Retro-Go SD

A comprehensive emulator collection for the Nintendo® Game & Watch™ with SD Card support, allowing you to play your favorite retro games on the go!

If you are looking for the mod without SD Card (Flash mod only), check https://github.com/sylverb/game-and-watch-retro-go

## Table of Contents
- [Nintendo® Game \& Watch™ Retro-Go SD](#nintendo-game--watch-retro-go-sd)
  - [Table of Contents](#table-of-contents)
  - [Support Development](#support-development)
  - [Features](#features)
  - [Installation](#installation)
    - [Pre-modded Options](#pre-modded-options)
    - [Hardware Requirements](#hardware-requirements)
    - [Installation Steps](#installation-steps)
    - [Cutting the shell for SD Card slot](#cutting-the-shell-for-sd-card-slot)
    - [Shell Replacement](#shell-replacement)
    - [Retro-Go-SD Update Steps](#retro-go-sd-update-steps)
    - [Bootloader Update Steps](#bootloader-update-steps)
  - [Tools](#tools)
    - [Cover Art Generator (gencovers.py)](#cover-art-generator-gencoverspy)
      - [Usage](#usage)
      - [Options](#options)
    - [Pico-8 Cover Art  Generator ( pico8covers.py )](#pico-8-cover-art--generator--pico8coverspy-)
      - [Usage:](#usage-1)
  - [Supported Systems](#supported-systems)
    - [Emulators](#emulators)
    - [SNES Ports](#snes-ports)
    - [Homebrew Ports](#homebrew-ports)
  - [Notes for specific systems](#notes-for-specific-systems)
    - [Game Boy Advance](#game-boy-advance)
    - [PC Engine CD / TurboGrafx-CD](#pc-engine-cd--turbografx-cd)
    - [Atari Lynx](#atari-lynx)
  - [Controls](#controls)
    - [Button Mappings](#button-mappings)
    - [Macros](#macros)
  - [Troubleshooting](#troubleshooting)
  - [FAQ](#faq)
  - [Cheat codes](#cheat-codes)
    - [Cheat codes on NES System](#cheat-codes-on-nes-system)
    - [Cheat codes on GB System](#cheat-codes-on-gb-system)
    - [Cheat codes on PCE System](#cheat-codes-on-pce-system)
  - [NES Emulator](#nes-emulator)
- [Pokémon Mini Emulator](#pokémon-mini-emulator)
  - [Homebrew ports](#homebrew-ports-1)
  - [Pico-8](#pico-8)
    - [Compatibility and performance](#compatibility-and-performance)
    - [Installation](#installation-1)
    - [Loading carts](#loading-carts)
    - [Covers](#covers)
  - [Developer info](#developer-info)
    - [Build and flash using Docker](#build-and-flash-using-docker)
    - [Creating a new emulator core from scratch](#creating-a-new-emulator-core-from-scratch)
  - [Discord, support and discussion](#discord-support-and-discussion)
  - [License](#license)

## Support Development

You can support the development by donating via [PayPal](https://paypal.me/revlys)

## Features

- 🎮 Support for multiple retro gaming systems
- 💾 SD Card storage for ROMs and games
- 💾 4 save state slots per game
- 🎨 Cover art support
- 🔄 Easy firmware updates
- 🌐 Unicode support (Latin and Cyrillic, more to come)
- 🎯 Cheat code support for multiple systems
- 🔄 Dual boot capability (original firmware preservation)

Current limitations :
- Up to 1000 roms/disks will be visible for each system
- CJK characters not yet visible

## Installation

### Pre-modded Options

If you prefer professional installation, contact:
- **Europe**: Sylver ([u/Sylver7667](https://www.reddit.com/user/Sylver7667/) on Reddit, sylver__ on Discord)
- **USA**: hundshamer ([u/hundshamer](https://www.reddit.com/user/hundshamer/) on Reddit)


### Hardware Requirements

To install the hardware mod, you need:

- SD Card flex PCB adapter: 

  <img src="assets/sm_black_top.png" height="150">

  - [Tim Schuerewegen / hundshamer Zelda v2 Gerber file](https://github.com/sylverb/game-and-watch-retro-go-sd/raw/refs/heads/main/assets/GnW_SD_v2.zip) (recommended for Zelda install)

  <img src="assets/MicroSD_Mario.png" height="200">
  <img src="assets/MicroSD_Zelda.png" height="200">
  
  - [PrimoAngelo Mario Gerber file](https://github.com/sylverb/game-and-watch-retro-go-sd/raw/refs/heads/main/assets/MicroSD_Mario_Final.zip) (recommended for Mario install)
  - [PrimoAngelo Zelda Gerber file](https://github.com/sylverb/game-and-watch-retro-go-sd/raw/refs/heads/main/assets/MicroSD_Zelda_Final.zip)  

- 1x MX25U51245GZ4I00 (64MB SPI flash 1.8v) or bigger
- 1x 0402 100k resistor (0805 will fit too)
- 2x 0402 1uf (0805 will fit too)
- 1x RT9193-28GB LDO regulator
- 1x Micro SD card slot SMD 9Pin ([here](https://www.aliexpress.com/item/1005002829329826.html), [here](https://www.aliexpress.com/item/1005001331379046.html), [here](https://www.aliexpress.com/item/32802051702.html), ...)

### Installation Steps

1. **Original Firmware Backup / Unlock**
   - Install [gnwmanager](https://github.com/BrianPugh/gnwmanager), follow [installation instructions](https://github.com/BrianPugh/gnwmanager/blob/main/tutorials/installation.md)
   - Connect your JTAG device (ST-Link v2 or others supported devices)
   - Run `gnwmanager unlock` to backup and unlock the console. Follow instructions on computer's screen with attention.
   - If you already have a backup, use `gnwmanager unlock --no-backup` to skip the backup steps

2. **Flash Chip Installation**
   - Install the MX25U51245GZ4I00 flash chip, follow instructions in [this video](https://www.youtube.com/watch?v=mYvK7LyHh1Y) if needed

3. **Patched OFW / Bootloader Installation**
   - For dual boot (recommended):
     * Zelda model :
       ```bash
       gnwmanager flash-patch zelda internal_flash_backup_zelda.bin flash_backup_zelda.bin --bootloader
       ```
     * Mario model :
       ```bash
       gnwmanager flash-patch mario internal_flash_backup_mario.bin flash_backup_mario.bin --bootloader
       ```
   - Without dual boot (not recommended but could help troubleshooting issues if dual boot fails to install):
     ```bash
     gnwmanager flash-bootloader bank1
     ```

   - You can now use bootloader to check that your flash chip is correctly installed. Power on the console (and press GAME+Left if patched OFW is installed) to run the bootloader. You should see a screen like this:

      ![Bootloader screen showing flash chip information](assets/bootloader_flash_installed.png)

      > Note: If you installed the recommended 64MB chip, you should see "MX25U51245G (64MB)" on the screen.

4. **SD Card Mod Installation**   
   Check this great install video by NaGa :
   
   [![Install](https://img.youtube.com/vi/dlssD4C8pJk/0.jpg)](https://www.youtube.com/watch?v=dlssD4C8pJk)

5. **Retro-Go-SD Installation**
   - Format micro SD card as exFAT (recommended) or FAT32
   - Download latest `retro-go_update.bin` from [releases page](https://github.com/sylverb/game-and-watch-retro-go-sd/releases/latest)
   - Place the file in the root folder of your SD card
   - Insert SD card and start the console

      ![](assets/firmware_update.png)
   - You can start filling the created folders on your sd card with uncompressed roms (in /roms/gb, roms/gbc, roms/nes, ...)
  
6. **Pico-8**
   - Requires a separate package installation, please refer to the [Pico-8](#pico-8) section for more details

### Cutting the shell for SD Card slot
   To help to properly cut the shell, facelesstech designed some drill jig, they can be found [here](https://www.printables.com/model/1269910-zelda-game-and-watch-sd-card-drill-jig/files)

### Shell Replacement
   For people who do not want to cut their original shell, Aradia (on Discord) has designed a replacement back shell with SD card access.
   Note that this shell is designed for the GnW_SD_v2.zip version of the flex cable (the one provided earlier on this page)
   The 3D printable file can be downloaded [here](https://github.com/sylverb/game-and-watch-retro-go-sd/raw/refs/heads/main/assets/GnW_Zelda_back_shell.stl).

### Retro-Go-SD Update Steps

   When there is a new Retro-Go-SD release available on github, to install it, just proceed as described :
   - Download latest `retro-go_update.bin` from [releases page](https://github.com/sylverb/game-and-watch-retro-go-sd/releases/latest)
   - Place the file in the root folder of your SD card
   - Insert SD card and start the console
   - Turn on the Game & Watch and wait for the installation to complete. Once the update is done, you are ready to use new version

### Bootloader Update Steps
   The bootloader is the application which is allowing to install/update retro-go from update file as described in [Retro-Go-SD Update Steps](#retro-go-sd-update-steps).

   It is possible to update the bootloader (check bootloader changelog to check if you have latest version and if it could be useful for you to update).
   
   Even if the operation should be safe, be aware that in case of problem during the bootloader update, you'll have to reprogram the bootloader using JTAG as described in [Installation] chapter.

   To perform a bootloader update, perform the following steps :
   - Download latest gnw_bootloader.bin (no dual boot) / gnw_bootloader_0x08032000.bin (dual boot) files from [Bootloader releases page](https://github.com/sylverb/game-and-watch-bootloader/releases)
   - Copy these files to the root folder of the SD Card.
   - Copy the `retro-go_update.bin` (v1.1.1 above) file to the root directory of your micro SD card (check previous chapter for instructions)
   - Insert the micro SD card into your Game & Watch.
   - Turn on the Game & Watch and wait for the installation to complete. It will update Retro-Go and also update the bootloader.

   Note that the update process will detect if you have dual boot or not and will try to install gnw_bootloader.bin if you don't have dual boot and gnw_bootloader_0x08032000.bin if you have dual boot. If you are not sure of what is the right file for you, just put both files and the correct one will be used.

## Tools

### Cover Art Generator (gencovers.py + download_covers.py)

Due to memory and power constrains of the Game & Watch hardware, it's not possible to use full size png/jpg/bmp images for cover art.
**Automation:** When building with `COVERFLOW=1`, covers are automatically downloaded and processed.
- **Scraping**: `tools/download_covers.py` fetches raw images into `roms/`. GitHub limits unauthenticated requests to **60/hr**. Large collections may require multiple runs or a token (`GITHUB_TOKEN=xxx` via make, or manually with `--token`). Use `DOWNLOAD_COVERS=0` to disable.
- **Thumbnailing**: `tools/gencovers.py` converts images into optimized `.img` files in `sd_content/covers/`.

Due to current implementation of the covers management, having covers with different sizes for a given system can cause incorrect alignement of images in "CoverLight H" view

User dadagm wrote a macos/windows tool to convert covers in an easy way ! Check https://github.com/dadagm/GameWatchCoverMaker to get his application !

The `tools/gencovers.py` script helps you generate cover art thumbnails for your games. It can process individual images or batch process all images in a directory.

Note that you will have to run `python3 -m pip install -r requirements.txt` once to install dependencies required by the script.

#### Usage

**Process a single image:**
```bash
python tools/gencovers.py --image /path/to/image.png
# Creates image.img in the same directory

python tools/gencovers.py --image /path/to/image.png --output /path/to/output.img
# Creates the .img file at the specified location
```

**Batch process all images in a directory:**
```bash
python tools/gencovers.py --src roms --dst covers
# Processes all images in the 'roms' directory and creates thumbnails in 'covers'
```

#### Options

- `--image`: Path to a single image file to process (PNG, JPG, JPEG, BMP)
- `--output`: Output path for the single image (only used with --image)
- `--src`: Source directory for batch processing (default: "roms")
- `--dst`: Destination directory for batch processing (default: "covers")
- `--width`: Thumbnail width (default: 128)
- `--height`: Thumbnail height (default: None, uses width-based scaling)
- `--jpg_quality`: JPEG quality 0-100 (default: 85)

The script automatically resizes images to fit within 186x100 pixels while maintaining aspect ratio, and saves them as optimized JPEG files with the `.img` extension.

The `.img` files have to be stored in the /covers folder of your sd card : for the game `/roms/msx/Aleste.rom`, the cover file should be `/covers/msx/Aleste.img`.

### Pico-8 Cover Art  Generator ( pico8covers.py ) 

Extract PICO-8 cart labels and generate G&W cover art (.img JPEG files).

Reads .p8 (text) or .p8.png (PNG) carts, extracts the 128x128 label,
renders it with the PICO-8 palette, and saves as a JPEG cover.

#### Usage:
```bash
  # Single cart:
  python3 pico8covers.py --cart celeste.p8 --output covers/pico8/celeste.img

  # All carts in a directory:
  python3 pico8covers.py --src roms/pico8 --dst covers/pico8

  # Custom size/quality:
  python3 pico8covers.py --src roms/pico8 --dst covers/pico8 --width 128 --jpg_quality 90
```

## Supported Systems

### Emulators
- Atari 2600 (external `/cores/a2600.bin`)
- Atari 7800 (external `/cores/a7800.bin`)
- Atari Lynx (experimental)
- ColecoVision
- Gameboy / Gameboy Color (external `/cores/tgb.bin`)
- Game Boy Advance (experimental, SD card only)
- Game & Watch / LCD Games (external `/cores/gw.bin`)
- MSX1/2/2+ (external `/cores/msx.bin`)
- Nintendo Entertainment System
- Pico-8
- PC Engine / TurboGrafx-16
- PC Engine CD / TurboGrafx-CD (beta, SD card only)
- Pokémon Mini (external `/cores/pkmini.bin`)
- Sega Game Gear
- Sega Genesis / Megadrive
- Sega Master System
- Sega SG-1000
- Watara Supervision (external `/cores/wsv.bin`)

### Homebrew Ports
- External GWHB homebrews (e.g. Celeste Classic, Zelda 3 / SMW ports) — place under `/homebrews/` on the SD card

## Notes for specific systems

### PC Engine CD / TurboGrafx-CD

PC Engine CD support is currently **beta** and available on **SD-card builds only** (not on flash-only builds).

- Put disc images under: `/roms/pcecd/`
  - Flat layout: `.cue` (+ referenced tracks) directly in `/roms/pcecd/`
  - Or one folder per game: `/roms/pcecd/<game>/…`
- HuCard games stay in `/roms/pce/` as usual

A Super System Card 3.0 dump is **required**:

- Path: `/bios/pce/syscard3.pce` (or `/bios/pce/syscard3.bin`)
- Expected MD5: `38179df8f4ac870017db21ebcbf53114`

Without this BIOS file, CD games will not boot.

### Atari Lynx

Lynx support is currently **experimental**.

- Put ROMs in: `/roms/lynx/` (extensions: `.lnx`, `.lyx`)
- No external BIOS file is required (the port uses an internal HLE BIOS)

## Controls

### Button Mappings
- `GAME` → `START`
- `TIME` → `SELECT`
- `PAUSE/SET` → Emulator menu

### Macros
| Button combination    | Action                                                                 |
| --------------------- | ---------------------------------------------------------------------- |
| `PAUSE/SET` + `GAME`  | Store a screenshot (Disabled by default on 1MB flash builds)          |
| `PAUSE/SET` + `TIME`  | Toggle speedup (1x/1.5x)                                               |
| `PAUSE/SET` + `UP`    | Brightness up                                                          |
| `PAUSE/SET` + `DOWN`  | Brightness down                                                        |
| `PAUSE/SET` + `RIGHT` | Volume up                                                              |
| `PAUSE/SET` + `LEFT`  | Volume down                                                            |
| `PAUSE/SET` + `B`     | Load state                                                             |
| `PAUSE/SET` + `A`     | Save state                                                             |
| `PAUSE/SET` + `POWER` | Poweroff without save-state                                            |

## Troubleshooting

- Bootloader as a diagnostic menu, you can show it by booting with PAUSE/SET button pressed at startup.
- If a savestate is causing crash when power on the console, boot with TIME button pressed to force system to boot in games list menu.

## FAQ

- Can you add [new system name] support ?

   Maybe ... Probably not ! G&W system is very limited, it has only about 1MB of RAM free for code + dynamic ressources for each emulator. Most of the time emulators have to be deeply optimized to reduce their memory use so they can fit. Each emulator port is a challenge and some have failed already (fake-08, picodrive, ...).

## Cheat codes

Note: Currently cheat codes are only working with GB, GBC, NES and PCE games.

To enable, add CHEAT_CODES=1 to your make command. If you have already compiled without CHEAT_CODES=1, I recommend running make clean first.
To enable or disable cheats, select a game then select "Cheat Codes". You will be able to select cheats you want to enable/disable. Then you can start/resume a game and selected cheats will be applied.
On GB and GBC systems, you can enable/disable cheats during game.

### Cheat codes on NES System

To add Game Genie codes, create a file ending in .ggcodes in the /cheats/nes/ directory with the same name as your rom. For instance, for
"/roms/nes/Super Mario Bros.nes" make a file called "/cheats/nes/Super Mario Bros.ggcodes". In that file, each line can have up to 3 Game Genie codes and a maximum
of 16 lines of active codes (for a max of 3 x 16 = 48 codes). Each line can also have a description (up to 25 characters long).
You can comment out a line by prefixing with # or //. For example:
```
SXIOPO, Inf lives
APZLGG+APZLTG+GAZUAG, Mega jump
YSAOPE+YEAOZA+YEAPYA, Start on World 8-1
YSAOPE+YEAOZA+LXAPYA, Start on World -1
GOZSXX, Invincibility
# TVVOAE, Circus music
```
You can enable / disable each of your codes in the game selection screen.

A collection of codes can be found [here](https://github.com/martaaay/game-and-watch-retro-go-game-genie-codes).

### Cheat codes on GB System

To add Game Genie/Game Shark codes, create a file ending in .ggcodes in the /cheats/gb/ or /cheats/gbc/ directory with the same name as your rom. For instance, for
"/roms/gb/Wario Land 3.gb" make a file called "/cheats/gb/Wario Land 3.ggcodes". In that file, each line can have several Game Genie / Game Shark codes
(separate them using a +) and a maximum of 16 lines of active codes. Each line can also have a description (up to 25 characters long).
You can comment out a line by prefixing with # or //. For example:
```
SXIOPO, Inf lives
APZLGG+APZLTG+GAZUAG, Mega jump
YSAOPE+YEAOZA+YEAPYA, Start on World 8-1
YSAOPE+YEAOZA+LXAPYA, Start on World -1
GOZSXX, Invincibility
# TVVOAE, Circus music
```
You can enable / disable each of your codes in the game selection screen or during game.

### Cheat codes on PCE System

Now you can define rom patch for PCE Roms. You can found patch info from [Here](https://krikzz.com/forum/index.php?topic=1004.0).
To add PCE rom patcher, create a file ending in .pceplus in the /cheats/pce/ directory with the same name as your rom. For instance, for
"/roms/pce/1943 Kai (J).pce" make a file called "/cheats/pce/1943 Kai (J).pceplus".
A collection of codes file can be found [here](https://github.com/olderzeus/game-genie-codes-nes/tree/master/pceplus).

Each line of pceplus is defined as:
```
01822fbd,018330bd,0188fcbd,	Hacked Version
[patchcommand],[...], patch desc

```

Each patch command is a hex string defined as:
```
01822fbd
_
|how much byte to patched
 _____
   |patch start address, subtract pce rom header size if had.
      __...
       |bytes data to patched from start address

```
## NES Emulator

NES emulation uses **fceumm** (FCEUmm). It has very good compatibility but uses significant CPU: typically about 65–85% depending on games; FDS titles can reach about 95%.

Mappers compatibility is basically the same as fceumm version from 01/01/2023. Testing all mappers is not possible, so some mappers that could try to allocate too much ram will probably crash. If you find any mapper that crash, please report on discord support, or by opening a ticket on github.

As Game & Watch CPU is not able to emulate YM2413 at 48kHz, mapper 85 (VRC-7) sound will play at 18kHz instead of 48kHz.

FDS support requires you to put the FDS firmware in `/bios/nes/disksys.rom` file

## MSX Emulator

MSX is provided as an external core (`/cores/msx.bin`) built outside this
firmware tree (blueMSX-based). Place the core and BIOS under `/bios/msx/`
on the SD card; see that core's documentation for details.

# Pokémon Mini Emulator

Pokémon Mini is provided as an external core (`/cores/pkmini.bin`). Optional
BIOS: `/bios/mini/bios.min` (integrated open-source BIOS used if missing).

## Homebrew ports

Standalone SNES reimplementations (Zelda 3 / Super Mario World), Celeste
Classic, and other GWHB payloads are built outside this firmware tree and
discovered at runtime from `/homebrews/*.bin` (plus any sibling assets they
load). Copy the prebuilt files from those projects onto the SD card — they
are no longer compiled or linked into the firmware.

## Pico-8

An original implementation of the [PICO-8](https://www.lexaloffle.com/pico-8.php) fantasy console engine for Game & Watch hardware, developed by [macs75](https://github.com/Macs75). The engine core is written from scratch in C/C++ and tailored to the STM32H7B0's tight memory budget, while a modified version of [Z8lua](https://github.com/samhocevar/z8lua) by Sam Hocevar serves as the Lua interpreter — providing the 16.16 fixed-point math and PICO-8 dialect extensions that real cartridges expect.

### Compatibility and performance

The G&W hardware (STM32H7B0VBT6, 1.4 MB SRAM) is significantly more constrained than a desktop PICO-8 host, so behavior varies between cartridges:

- Most simple and mid-complexity carts run at full 30/60 FPS.
- More demanding carts may drop frames, especially those with heavy per-frame Lua workloads or large draw call counts.
- A small number of carts will exceed the available memory budget and fail to load.

**Enabling overclock in the retro-go settings is strongly recommended** — it noticeably improves frame pacing and broadens the set of carts that run at full speed.

### Installation

The PICO-8 binaries are **not bundled** with the retro-go G&W firmware and must be installed separately.

1. Download the latest release from the [pico8_gnw_distro releases page](https://github.com/Macs75/pico8_gnw_distro/releases).
2. Unzip the archive at the **root** of your SD card.
3. Three files will be placed in the `cores/` directory:
   - `pico8.bin` — main engine binary
   - `pico8.ro` — read-only data segment
   - `pico8_itcm.bin` — performance-critical code loaded into ITCM RAM

Once installed, PICO-8 carts placed in the appropriate roms folder will be picked up by the retro-go launcher.

### Loading carts

Place `.p8` or `.p8.png` cartridge files in your SD card's PICO-8 roms directory and select them from the retro-go menu like any other system.
If the game requires to load other carts put them in a ".multicarts" subfolder. The code will be able to find them and they will not appear in the game list.

### Covers 

If you want to have the official carts covers use the specific python toolpico8covers.py. More details in the [Tools](#tools) section.

## Developer info

### Local build, flash, and SD-card workflow

For development on macOS/Linux without Docker you need `arm-none-eabi-gcc` v10+ (tested against 15.2.rel1) and the Python `requirements.txt` (`python3 -m pip install -r requirements.txt`, which installs `gnwmanager`).

#### One-time per-device setup

The device needs SylverB's bootloader in bank 1, with Retro-Go-SD in bank 2. Without it, the firmware-update flow that creates the SD card directory tree (`/cores`, `/bios`, `/homebrews`, …) cannot run.

```bash
# Install the bootloader to bank 1 (one-time):
gnwmanager flash-bootloader bank1
```

The `Makefile` defaults `INTFLASH_BANK=2` to match that bootloader. Pass `make INTFLASH_BANK=1` only if you installed without it.

#### First-time SD-card bootstrap

A freshly formatted (exFAT or FAT32) SD card lacks the directories `sdpush` needs as parents; the bootloader creates them when it unpacks `retro-go_update.bin`:

```bash
make release_sdpush GNW_TARGET=mario
```

Wait for the launcher menu to appear on the device before running any other `gnwmanager` command, or the extraction is interrupted and aborted.

#### Fast-iteration workflow

Once the directory tree exists, flash the firmware and regenerate the SD content locally:

```bash
make flash create_sd_data GNW_TARGET=mario
```

`create_sd_data` writes the SD files under `sd_content/`. Push only the ones you changed instead of re-sending every core, e.g.:

```bash
gnwmanager sdpush --file path/to/example.bin --dest-path /cores/
```

#### Common gotchas

- **Debug-probe `BAD_DECOMPRESS`.** Some CMSIS-DAP probes drop LZMA chunks at gnwmanager's default speed. Lower it with `make GNWMANAGER="gnwmanager --frequency 1000000"`.
- **Stale objects after toggling a `-D` flag.** Make doesn't track `-D` changes, so toggling `CHEAT_CODES`/`COVERFLOW`/etc. between builds mixes definitions and causes link errors. Run `make clean` when you change one.

### Build and flash using Docker

<details>
  <summary>
    If you are familiar with Docker and prefer a solution where you don't have to manually install toolchains and so on, expand this section and read on.
  </summary>

  To reduce the number of potential pitfalls in installation of various software, a Dockerfile is provided containing everything needed to compile and flash retro-go to your Nintendo® Game & Watch™ (Mario/Zelda) system. This Dockerfile is written tageting an x86-64 and arm64 machine running Linux or macOS.

  Steps to build and flash from a docker container (running on Linux/macOS, e.g. Archlinux, Ubuntu or macOS):

  ```bash
  # Clone this repo
  git clone --recursive https://github.com/sylverb/game-and-watch-retro-go-sd

  # cd into it
  cd game-and-watch-retro-go-sd

  # Optional : Build the docker image (takes a while)
  # You can generate the docker image locally but it's
  # not needed as generated image is available on
  # https://hub.docker.com/repository/docker/sylverb/retro-go-sd-builder
  # for x86-64 and arm64 architectures.
  make docker_build

  # Run the container.
  # The current directory will be mounted into the container and the current user/group will be used.
  make docker
  ```
  The install/update file will be available in release/retro-go_update.bin

</details>

### Creating a new emulator core from scratch

<details>
  <summary>
    Want to port a new emulator/system to Retro-Go-SD? Expand this section for a step-by-step guide to the "standalone core" model.
  </summary>

Emulator cores (NES, MSX, Watara Supervision, PICO-8, ...) are **not** linked into the main firmware ELF. Each one is built as its own small, freestanding binary, communicates with the firmware exclusively through a stable, versioned function table (the "ABI"), and is discovered automatically at boot by scanning `/cores/*.bin` on the SD card — no compile-time list of systems lives in the launcher. This keeps the firmware small and lets every core use the entire ~1MB "free RAM" budget for itself (only one core is ever resident in memory at a time).

Use [`cores/_template/`](cores/_template/) as the build starting point. The full technical guide (ABI extension checklist, linker script internals, every gotcha hit while writing the SDK) lives in [`Core/Src/porting/core_common/CLAUDE.md`](Core/Src/porting/core_common/CLAUDE.md) — this section is a shorter overview to get you oriented before diving into that file.

**1. Understand the moving pieces**

| Piece | Path | Role |
| ----- | ---- | ---- |
| ABI (function table) | `Core/Inc/retro-go/gw_firmware_abi.h` | Every libc/hardware/retro-go function a core is allowed to call, exposed as function pointers at a fixed address. Append-only and versioned so old cores keep working on newer firmware. |
| Bridge SDK | `Core/Src/porting/core_common/` | Generic trampolines that forward calls through the ABI, plus the symbol-renaming list and entry-point assembly shared by every core. |
| Build template | `cores/_template/` | The Makefile and linker script every core's own `cores/<name>/Makefile` includes — handles toolchain flags, the ABI symbol-renaming step, and linking at the fixed RAM address cores load into. |
| Packaging tool | `tools/pack_core.py` | Turns a linked core ELF into `cores/<name>.bin`: a small header (system name, ROM folder, supported extensions, required ABI version, code/BSS size) followed by optional pad/console logo images and the core's code. |
| Metadata struct | `Core/Inc/retro-go/gnw_core_meta.h` | Defines that header (`gnw_core_meta_t`) so the launcher can read it back. |
| Discovery + launch | `Core/Src/retro-go/rg_emulators.c` (`emulators_scan_cores`, `run_dynamic_core`) | At boot, probes every `/cores/*.bin`, registers one tab per valid core; at launch, loads the core's code into RAM, zeroes its BSS, and jumps to it. |

**2. Write the porting layer**

Create `Core/Src/porting/<system>/main_<system>.c` (or adapt an existing one). This is where ROM loading, input mapping, video/audio bridging, and savestate hooks live. Two things are specific to the standalone-core model:

- `#include "gw_core_bridge.h"` **after** the normal firmware headers (`common.h`, `rom_manager.h`, `gw_malloc.h`, ...). It turns shared globals like `common_emu_state`, `ACTIVE_FILE`, and `ram_start` into live reads/writes through the ABI instead of direct firmware symbols.
- There's no access to the launcher's translated strings (i18n) yet from a standalone core, so any menu text your port needs has to be a hardcoded English string for now.

**3. Set up the build**

Create `cores/<system>/Makefile` modeled on the variables documented in [`cores/_template/Makefile`](cores/_template/Makefile):

```makefile
CORE_NAME  := <system>
CORE_ENTRY := app_main_<system>   # must be a real function defined in your sources

CORE_C_SOURCES := \
external/<engine>/some_file.c \
../../Core/Src/porting/<system>/main_<system>.c

include ../_template/Makefile
```

Then build it standalone:

```bash
cd cores/<system>
make
```

If the link fails with an undefined reference to a libc/hardware/retro-go function, that function needs to be added to the ABI (or, if it's already in `gw_firmware_abi_t`, just needs a trampoline + rename entry in `Core/Src/porting/core_common/`). Both cases are documented step by step in the "Extending the ABI" and "Porting a new core: checklist" sections of [`Core/Src/porting/core_common/CLAUDE.md`](Core/Src/porting/core_common/CLAUDE.md).

A successful `make` produces `cores/<system>.bin`, packaged automatically via `tools/pack_core.py`.

**4. Ship the core**

External cores: copy `cores/<system>.bin` to `/cores/` on the SD card (the launcher discovers it at boot). If this firmware tree still builds a core in-tree, wire a `Makefile.common` phony/`sdpush` block for it — see the checklist in [`Core/Src/porting/core_common/CLAUDE.md`](Core/Src/porting/core_common/CLAUDE.md).

**5. Test**

`make release` (or `make flash create_sd_data`, see [Fast-iteration workflow](#fast-iteration-workflow) above) builds the firmware and your new core together. Push the SD content, boot the console, and your system's tab should appear in the launcher automatically — no other firmware change required.

</details>

## Discord, support and discussion 

Please join the [Discord](https://discord.gg/vVcwrrHTNJ).

## License

This project uses Fusion Pixel Font (SIL Open Font License 1.1)

This project is licensed under the GPLv2. Some components are available under the MIT license. Respective copyrights apply to each component.
