# Curated projects

Emulator cores and homebrew that are known to work with this firmware and that
publish under the [GWRG distribution spec](https://github.com/slash-proc/gwrg-dist-spec).

CI parses this file into `dist/<tag>/projects.json`, published alongside the
firmware manifest. An installer reads that file and follows each project's
`versions.json` from there; nothing here is fetched by the firmware itself.

**Origins are development repositories** and will be repointed at the upstream
projects before release.

Each project slug links to its repository. CI derives the project's
`versions.json` from that link:
`https://slash-proc.github.io/<repo-name>/dist/versions.json`.

## Emulator cores

| Project | Title | Systems |
|---|---|---|
| [`nes-fceu`](https://github.com/slash-proc/fceumm-retro-go-sd) | FCEUmm | Nintendo Entertainment System |
| [`tgb`](https://github.com/slash-proc/tgb-dual-retro-go-sd) | TGB Dual | Game Boy, Game Boy Color |
| [`sms`](https://github.com/slash-proc/SMSPlusGX-retro-go-sd) | SMS Plus GX | Master System, Game Gear, SG-1000, ColecoVision |
| [`pce`](https://github.com/slash-proc/pce-go-retro-go-sd) | PCE-GO | PC Engine, PC Engine CD |
| [`md`](https://github.com/slash-proc/gwenesis-retro-go-sd) | Gwenesis | Sega Genesis |
| [`snes`](https://github.com/slash-proc/snes-retro-go-sd) | lakesnes | Super Nintendo |
| [`gba`](https://github.com/slash-proc/gba-retro-go-sd) | gpSP | Game Boy Advance |
| [`msx`](https://github.com/slash-proc/blueMSX-retro-go-sd) | blueMSX | MSX |
| [`lynx`](https://github.com/slash-proc/lynx-retro-go-sd) | Handy | Atari Lynx |
| [`a2600`](https://github.com/slash-proc/stella2014-retro-go-sd) | Stella 2014 | Atari 2600 |
| [`a7800`](https://github.com/slash-proc/prosystem-retro-go-sd) | ProSystem | Atari 7800 |
| [`videopac`](https://github.com/slash-proc/o2em-retro-go-sd) | O2EM | Videopac, Magnavox Odyssey² |
| [`wsv`](https://github.com/slash-proc/potator-retro-go-sd) | Potator | Watara Supervision |
| [`amstrad`](https://github.com/slash-proc/caprice32-retro-go-sd) | Caprice32 | Amstrad CPC |
| [`pkmini`](https://github.com/slash-proc/PokeMini-retro-go-sd) | PokeMini | Pokémon Mini |
| [`gw`](https://github.com/slash-proc/LCD-Game-Emulator-retro-go-sd) | LCD Game Emulator | Game & Watch |
| [`doom`](https://github.com/slash-proc/doom-retro-go-sd) | Doom | Doom I & II |

## Homebrew

| Project | Title | From |
|---|---|---|
| [`zelda3`](https://github.com/slash-proc/zelda3-retro-go-sd) | The Legend of Zelda: A Link to the Past | snes |
| [`ccleste`](https://github.com/slash-proc/ccleste-retro-go-sd) | Celeste Classic | pico8 |
| [`tama`](https://github.com/slash-proc/tama-retro-go-sd) | Tamagotchi P1 | tamagotchi |
| [`openlara`](https://github.com/slash-proc/openlara-retro-go-sd) | Tomb Raider | dos |
| [`music`](https://github.com/slash-proc/music-retro-go-sd) | Music | — |
| [`snake`](https://github.com/slash-proc/snake-retro-go-sd) | Snake | — |
| [`pong`](https://github.com/slash-proc/pong-retro-go-sd) | Pong | — |
| [`cupcake`](https://github.com/slash-proc/cupcake-crisis-retro-go-sd) | Cupcake Crisis | — |
| [`durak`](https://github.com/slash-proc/durak-retro-go-sd) | Duren | — |
| [`smw`](https://github.com/slash-proc/smw-retro-go-sd) | Super Mario World | snes |
| [`minesweeper`](https://github.com/slash-proc/mine-sweeper-retro-go-sd) | Mine Sweeper | — |

## Not listed

- `pico8` — the PICO-8 core has no standalone project repository yet.
