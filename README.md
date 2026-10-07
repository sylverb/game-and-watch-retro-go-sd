![](assets/gnw.gif)

# Nintendo® Game & Watch™ Retro-Go SD

Emulator collection for the Nintendo® Game & Watch™ with a microSD card: ROMs, emulator cores, homebrews, covers, and updates all live on the card.

Looking for the **flash-only** mod (no SD card)? See [game-and-watch-retro-go](https://github.com/sylverb/game-and-watch-retro-go).

**Retro-Go SD 2.x** keeps the same hardware mod and the same SD-based update flow. The big software change is that emulator systems are **standalone cores** under `/cores/*.bin` (and homebrews under `/homebrews/*.bin`), discovered at boot instead of being baked into the firmware ELF.

[Discord](https://discord.gg/vVcwrrHTNJ) · [Releases](https://github.com/sylverb/game-and-watch-retro-go-sd/releases/latest) · [Patreon](https://www.patreon.com/sylverb) (free to join) · Support via [PayPal](https://paypal.me/revlys)

---

<details>
<summary><strong>Installation</strong> — hardware mod, first install, updates, bootloader</summary>

<br>

### Pre-modded options

If you prefer professional installation, contact:

- **Europe**: Sylver ([u/Sylver7667](https://www.reddit.com/user/Sylver7667/) on Reddit, sylver__ on Discord)
- **USA**: hundshamer ([u/hundshamer](https://www.reddit.com/user/hundshamer/) on Reddit)

### Hardware requirements

- SD Card flex PCB adapter:

  <img src="assets/sm_black_top.png" height="150">

  - [Tim Schuerewegen / hundshamer Zelda v2 Gerber](https://github.com/sylverb/game-and-watch-retro-go-sd/raw/refs/heads/main/assets/GnW_SD_v2.zip) (recommended for Zelda)

  <img src="assets/MicroSD_Mario.png" height="200">
  <img src="assets/MicroSD_Zelda.png" height="200">

  - [PrimoAngelo Mario Gerber](https://github.com/sylverb/game-and-watch-retro-go-sd/raw/refs/heads/main/assets/MicroSD_Mario_Final.zip) (recommended for Mario)
  - [PrimoAngelo Zelda Gerber](https://github.com/sylverb/game-and-watch-retro-go-sd/raw/refs/heads/main/assets/MicroSD_Zelda_Final.zip)

- 1× MX25U51245GZ4I00 (64MB SPI flash 1.8V) or larger
- 1× 0402 100k resistor (0805 fits too)
- 2× 0402 1µF (0805 fits too)
- 1× RT9193-28GB LDO regulator
- 1× Micro SD card slot SMD 9Pin ([example](https://www.aliexpress.com/item/1005002829329826.html), [example](https://www.aliexpress.com/item/1005001331379046.html), [example](https://www.aliexpress.com/item/32802051702.html), …)

### Installation steps

1. **Original firmware backup / unlock**
   - Install [gnwmanager](https://github.com/BrianPugh/gnwmanager) ([installation tutorial](https://github.com/BrianPugh/gnwmanager/blob/main/tutorials/installation.md))
   - Connect your JTAG probe (ST-Link v2 or other supported device)
   - Run `gnwmanager unlock` and follow the on-screen instructions carefully
   - If you already have a backup: `gnwmanager unlock --no-backup`

2. **Flash chip installation**
   - Install the MX25U51245GZ4I00 (or larger) — [video guide](https://www.youtube.com/watch?v=mYvK7LyHh1Y) if needed

3. **Patched OFW / bootloader**
   - Dual boot (recommended):
     - Zelda:
       ```bash
       gnwmanager flash-patch zelda internal_flash_backup_zelda.bin flash_backup_zelda.bin --bootloader
       ```
     - Mario:
       ```bash
       gnwmanager flash-patch mario internal_flash_backup_mario.bin flash_backup_mario.bin --bootloader
       ```
   - Without dual boot (troubleshooting only):
     ```bash
     gnwmanager flash-bootloader bank1
     ```
   - Power on (press **GAME+Left** if patched OFW is installed) to enter the bootloader and confirm the flash chip:

     ![Bootloader screen showing flash chip information](assets/bootloader_flash_installed.png)

     > With the recommended 64MB chip you should see **MX25U51245G (64MB)**.

4. **SD card mod**
   - Install video by NaGa:

     [![Install](https://img.youtube.com/vi/dlssD4C8pJk/0.jpg)](https://www.youtube.com/watch?v=dlssD4C8pJk)

5. **Retro-Go-SD firmware**
   - Format the microSD as **exFAT** (recommended) or **FAT32**
   - Download the latest update from the [releases page](https://github.com/sylverb/game-and-watch-retro-go-sd/releases/latest)
   - Copy `retro-go_update.bin` (or the bank-specific file for your install — see **Updating** below) to the **root** of the SD card
   - Insert the card and power on

     ![](assets/firmware_update.png)

   - After install, fill the created folders with uncompressed ROMs (e.g. `/roms/gb`, `/roms/nes`, …) and any required `/cores/*.bin` packages from the release or core authors

### Shell cut / replacement

- Drill jig for the SD slot cut: [Printables — Zelda SD card drill jig](https://www.printables.com/model/1269910-zelda-game-and-watch-sd-card-drill-jig/files)
- Replacement back shell (Aradia, Discord) for **GnW_SD_v2** flex: [GnW_Zelda_back_shell.stl](https://github.com/sylverb/game-and-watch-retro-go-sd/raw/refs/heads/main/assets/GnW_Zelda_back_shell.stl)

### Updating Retro-Go-SD

Download **`retro-go_update.bin`** from the GitHub release (or the
[Pages mirror](https://sylverb.github.io/game-and-watch-retro-go-sd/)) and copy
it to the SD root. It is the bank-2 firmware updater only — cores and homebrews
(including any installer homebrew) are installed separately. Install zips and
debug ELFs for tools also live on Pages.

Then insert the card, power on, and wait until the update finishes.


### Updating the bootloader

The bootloader installs/updates Retro-Go from the SD update file. Updating it is usually safe, but a failure requires reflashing via JTAG (see Installation above).

1. Download `gnw_bootloader.bin` (no dual boot) and/or `gnw_bootloader_0x08032000.bin` (dual boot) from the [bootloader releases](https://github.com/sylverb/game-and-watch-bootloader/releases)
2. Copy them to the SD root together with a Retro-Go update file (v1.1.1+)
3. Power on and wait — Retro-Go and the matching bootloader are updated

If unsure which bootloader file you need, copy both; the updater picks the right one.

</details>

---

<details>
<summary><strong>Using Retro-Go-SD</strong> — SD layout, controls, covers, cheats, troubleshooting</summary>

<br>

### SD card layout (overview)

| Path | Contents |
|------|----------|
| `/roms/<system>/` | Uncompressed ROMs (and disc layouts where noted below) |
| `/cores/*.bin` | Emulator cores (one or more systems per file; discovered at boot) |
| `/homebrews/` | Standalone GWHB homebrews (`.bin`); subfolders without a GWHB core are hidden |
| `/covers/<system>/` | Cover thumbnails (`.img`) |
| `/bios/...` | System BIOS files when required |
| `/cheats/<system>/` | Cheat files (see Cheat codes) |
| `/data/` | Per-core settings / save-related data (managed by the firmware) |

Core tabs are listed alphabetically by `/cores/*.bin` filename. Favorites and Homebrew stay at the front of the launcher.

### Features

- Multiple systems via drop-in `/cores` packages
- SD storage for ROMs, cores, covers, bios, cheats
- 4 save-state slots per game
- Cover art (Coverflow)
- Firmware updates from the SD card
- Unicode UI (Latin, Cyrillic, CJK — Chinese / Japanese / Korean)
- Cheat codes on several systems
- Dual boot with original firmware

Limits: up to **1000** visible ROMs/disks per folder, create subfolders to access more then 1000 roms in a system.

### Controls

**Mappings**

- `GAME` → `START`
- `TIME` → `SELECT`
- `PAUSE/SET` → Emulator / system menu

**Macros**

| Combination | Action |
|-------------|--------|
| `PAUSE/SET` + `GAME` | Screenshot (disabled by default on 1MB flash builds) |
| `PAUSE/SET` + `TIME` | Toggle speedup (1× / 1.5×) |
| `PAUSE/SET` + `UP` / `DOWN` | Brightness |
| `PAUSE/SET` + `RIGHT` / `LEFT` | Volume |
| `PAUSE/SET` + `B` / `A` | Load / save state |
| `PAUSE/SET` + `POWER` | Power off without save-state |

### Cover art

CoverFlow needs optimized `.img` thumbnails (not full PNG/JPG on device).

- For `/roms/msx/Aleste.rom` → `/covers/msx/Aleste.img`
- **Recommended:** [CoverStudio](https://sylverb.github.io/CoverStudio/?target=gw) — browser toolkit to fetch/resize covers and write them to the SD layout
- Alternatives: `tools/gencovers.py` (see **Tools**), or [GameWatchCoverMaker](https://github.com/dadagm/GameWatchCoverMaker) (macOS/Windows)
- Mixed cover sizes for one system can misalign **CoverLight H**

### Cheat codes

If the core is supporting cheat codes, place your cheat code files in /cheats folder. A cheat code file for /roms/gb/mario.gb shall be called /cheats/gb/mario.ggcodes . Check cores pages details to get details about cheats support (file format/extension).

### Troubleshooting

- Hold **PAUSE/SET** at power-on for the bootloader diagnostic menu
- If a savestate crashes on boot, hold **TIME** at power-on to skip it and return to the game list
- Report issues on the [GitHub issues](https://github.com/sylverb/game-and-watch-retro-go-sd/issues) page or Discord

### FAQ

**Can you add [system]?**  
Anyone can now add a Core/Homebrew whithout the need to integrate it in the retro-go-sd project. Check Developper section.

</details>

---

<details>
<summary><strong>Supported systems & notes</strong></summary>

<br>

### Emulators

Now cores are provided outside of this project. You can find various projects by me or some other people :

- [Amstrad CPC 6128](https://github.com/sylverb/caprice32-retro-go-sd)
- [Atari 2600](https://github.com/sylverb/stella2014-retro-go-sd)
- [Atari 7800](https://github.com/sylverb/prosystem-retro-go-sd)
- [Atari Lynx](https://github.com/sylverb/lynx-retro-go-sd)
- [Bandai WonderSwan/WonderSwan Color](https://github.com/sylverb/oswan-retro-go-sd)
- [Doom](https://github.com/sylverb/doom-retro-go-sd)
- [Lexaloffle Pico-8](https://github.com/Macs75/pico8_gnw_distro)
- [Magnavox Odyssey²/Philips Videopac](https://github.com/sylverb/o2em-retro-go-sd)
- [MSX1/2/2+](https://github.com/sylverb/blueMSX-retro-go-sd)
- [NEC PC Engine / PC Engine CD / TurboGrafx-16 / SuperGrafx](https://github.com/sylverb/pce-go-retro-go-sd)
- [Nintendo Game Boy / Game Boy Color](https://github.com/sylverb/tgb-dual-retro-go-sd)
- [Nintendo Game Boy Advance](https://github.com/sylverb/gba-retro-go-sd)
- [Nintendo Game & Watch / LCD games](https://github.com/sylverb/LCD-Game-Emulator-retro-go-sd)
- [Nintendo Entertainment System/Famicom Disk System](https://github.com/sylverb/fceumm-retro-go-sd)
- [Nintendo Super Famicom/Super Nintendo](https://github.com/sylverb/snes-retro-go-sd)
- [Nintendo Pokémon Mini](https://github.com/sylverb/PokeMini-retro-go-sd)
- [Sega Game Gear / Master System / SG-1000 / Colecovision](https://github.com/sylverb/SMSPlusGX-retro-go-sd)
- [Sega Genesis / Mega Drive](https://github.com/sylverb/gwenesis-retro-go-sd)
- [SNK Neo Geo AES/MVS](https://github.com/sylverb/gngeo-retro-go-sd)
- [SNK Neo Geo Pocket/Pocket Color](https://github.com/sylverb/race-retro-go-sd)
- [Watara Supervision](https://github.com/sylverb/potator-retro-go-sd)
Some other existing emulators could be missing, don't hesitate to create a PR or contact me to add them here.

To install a new core, download release zip file from the project page and extract it on your sd card (core file(s) should then be installed in /cores folder)

### Homebrew

Now homebrews are provided outside of this project. You can find various projects by me or some other people :

- [Celeste Classic (Pico-8 port)](https://github.com/sylverb/ccleste-retro-go-sd)
- [Cave Story](https://github.com/sylverb/NXEngine-retro-go-sd)
- [Snake](https://github.com/sylverb/snake-retro-go-sd)
- [Mine Sweeper](https://github.com/sylverb/mine-sweeper-retro-go-sd)
- [Pong](https://github.com/sylverb/pong-retro-go-sd)
- [Durak](https://github.com/sylverb/durak-retro-go-sd)
- [Tamagotchi P1](https://github.com/sylverb/tama-retro-go-sd)
- [Super Mario World SNES port](https://github.com/sylverb/smw-retro-go-sd)
- [Zelda A Link To The Past SNES port](https://github.com/sylverb/zelda3-retro-go-sd)
- [Tomb Raider](https://github.com/sylverb/openlara-retro-go-sd)
Some other existing homebrews could be missing, don't hesitate to create a PR or contact me to add them here.

To install a new homebrew, download release zip file from the project page and extract it on your sd card (core file(s) should then be installed in /homebrews folder). Be sure to check project page as you could have to provide game's assets.
</details>

---

<details>
<summary><strong>Tools</strong> — cover generators</summary>

<br>

### CoverStudio (recommended)

Browser UI to generate G&W `.img` covers and place them on the SD card:

→ **[CoverStudio](https://sylverb.github.io/CoverStudio/?target=gw)**

### Command-line scripts

Install Python deps once: `python3 -m pip install -r requirements.txt`.

#### gencovers.py

Resize images to G&W-friendly JPEG `.img` thumbnails (fit within ~186×100).

```bash
python tools/gencovers.py --image /path/to/image.png
python tools/gencovers.py --src roms --dst covers
```

Useful options: `--width`, `--height`, `--jpg_quality` (default 85).

#### pico8covers.py

Extract PICO-8 cart labels to `.img` covers:

```bash
python3 tools/pico8covers.py --cart celeste.p8 --output covers/pico8/celeste.img
python3 tools/pico8covers.py --src roms/pico8 --dst covers/pico8
```

</details>

---

<details>
<summary><strong>For developers</strong> — build firmware, write cores / homebrews</summary>

<br>

### Model (2.x)

Emulators and homebrews are **not** linked into `gw_retro_go.elf` anymore. Each is a freestanding binary talking to the launcher only through `gw_firmware_abi_t`, packed as a `.bin`, and discovered at boot (`/cores/*.bin` or `/homebrews/*.bin`). Only one is resident in RAM at a time (~1 MB budget).

**To create a core or homebrew**, use the out-of-tree SDK template :

→ **[sylverb/retro-go-sd-templates](https://github.com/sylverb/retro-go-sd-templates)**

Firmware-side ABI / bridge notes (for contributors changing the launcher contract): [`Core/Src/porting/core_common/CLAUDE.md`](Core/Src/porting/core_common/CLAUDE.md).

### Build & flash this firmware

Needs `arm-none-eabi-gcc` v10+ (CI uses 15.2.rel1) and `python3 -m pip install -r requirements.txt` (`gnwmanager`).

```bash
# One-time: bootloader in bank 1 (Makefile defaults INTFLASH_BANK=2)
gnwmanager flash-bootloader bank1

# First SD bootstrap (creates /cores, /bios, /homebrews, … via update unpack)
make release_sdpush GNW_TARGET=mario   # or zelda

# Iteration once the tree exists
make flash create_sd_data GNW_TARGET=mario
gnwmanager sdpush --file path/to/core.bin --dest-path /cores/
```

Wait for the launcher after `release_sdpush` before other `gnwmanager` commands. If a CMSIS-DAP probe hits `BAD_DECOMPRESS`, lower speed: `make GNWMANAGER="gnwmanager --frequency 1000000"`. Run `make clean` after toggling `-D` flags (`CHEAT_CODES`, `COVERFLOW`, …).

### Docker build

Image: [sylverb/retro-go-sd-builder](https://hub.docker.com/repository/docker/sylverb/retro-go-sd-builder) (x86-64 / arm64). Or `make docker_build` locally.

```bash
git clone --recursive https://github.com/sylverb/game-and-watch-retro-go-sd
cd game-and-watch-retro-go-sd
make docker
```

Output under `release/` (`retro-go_update.bin` and bank-specific variants).

</details>

---

<details>
<summary><strong>License & community</strong></summary>

<br>

- [Discord](https://discord.gg/vVcwrrHTNJ) — support and discussion
- [Patreon](https://www.patreon.com/sylverb) — free to join (optional paid tiers)
- Donations: [PayPal](https://paypal.me/revlys)
- Font: Fusion Pixel Font (SIL OFL 1.1)
- Project license: **GPLv2** (some components MIT; respective copyrights apply)

</details>
