# Handy commands (EarthBound dev + on-hardware debugging)

Toolchain + device assumptions used below:

```bash
export GCC_PATH=/Applications/ArmGNUToolchain/15.2.rel1/arm-none-eabi/bin
export CHECK_DIRTY_SUBMODULE=0      # submodule is intentionally dirty while iterating
# This device dual-boots retro-go-sd from bank 2. A plain `make flash` (bank 1)
# silently no-ops here — ALWAYS pass INTFLASH_BANK=2.
```

## Build + flash EarthBound (fast path)

Flashes intflash (bank 2) + regenerates the SD payload, then pushes ONLY the two
EarthBound files. Much faster than `make flash_sd` (which pushes the whole payload,
several minutes). Push BOTH `.bin` and `.ro` — a stale `.ro` silently corrupts
strings ([[eb-rodata-cache-staleness]]).

```bash
make GNW_TARGET=mario INTFLASH_BANK=2 GCC_PATH=$GCC_PATH -j15 flash create_sd_data \
 && gnwmanager sdpush \
      --file "sd_content/roms/homebrew/EarthBound.bin" --dest-path /roms/homebrew/ \
   -- sdpush \
      --file "sd_content/roms/homebrew/earthbound.ro"  --dest-path /roms/homebrew/ \
      -f 1mhz
```

Build only (verify green, no flash):

```bash
make GNW_TARGET=mario INTFLASH_BANK=2 GCC_PATH=$GCC_PATH -j15 build/gw_retro_go.elf
# compile-check one file: make GNW_TARGET=mario ... build/core/main.o
```

## GDB over the debug probe (ST-Link v2 or pico-probe)

`gnwmanager gdbserver` spawns OpenOCD (auto-detects the probe via `interface/*.cfg`)
with a GDB server on `:3333`. Leave it running in one terminal:

```bash
gnwmanager gdbserver                 # OpenOCD + gdbserver on localhost:3333
```

Then attach (another terminal). `make gdb` is the shortcut; the raw form:

```bash
arm-none-eabi-gdb build/gw_retro_go.elf -ex "target extended-remote :3333"
```

Notes / gotchas:
- Use **`hbreak`, not `break`, for flash addresses** (`0x08xxxxxx`) — a software
  breakpoint silently fails to write flash. RAM/overlay addrs (`0x24xxxxxx`) take either.
- This gdb build has **no Python**; use native `-ex printf` / `x`.
- A **power-cycle drops SWD** (gdb dies, reconnect). A debugger-driven reset keeps it.
- `compare-sections .text` confirms flash == `build/gw_retro_go.elf`.

### Catch a crash with full fault context

Break in the fault handler; when the crash happens it dumps everything (far more than
the on-screen BSOD). Reproduce on the device while this waits.

```bash
arm-none-eabi-gdb build/gw_retro_go.elf -batch \
  -ex "set pagination off" -ex "target extended-remote :3333" \
  -ex "hbreak common_fault_handler_c" -ex "continue" \
  -ex "printf \"type=%d\n\", type" \
  -ex "print/x *frame" \
  -ex "info registers r4 r5 r6 r7 r8 r9 r10 r11" \
  -ex "printf \"CFSR=%08x HFSR=%08x MMFAR=%08x BFAR=%08x ABFSR=%08x\n\", \
       *(unsigned*)0xE000ED28, *(unsigned*)0xE000ED2C, *(unsigned*)0xE000ED34, \
       *(unsigned*)0xE000ED38, *(unsigned*)0xE000EFA8" \
  -ex "x/12i frame->return_address - 16"
```

`frame` (a `sContextStateFrame*`) is the stacked exception frame: r0-r3, r12, lr,
return_address (PC), xpsr. r4-r11 are the live callee-saved regs (preserved through
exception entry). For an **imprecise BusFault** (`CFSR` bit10, `CFSR=0x…400`) the PC
is drain-time noise — read **ABFSR** (`0xE000EFA8`): bit2 AHBP = peripheral space
`0x40000000-0x5FFFFFFF`, bit3 AXIM = RAM/flash, bit0/1 ITCM/DTCM.

If it's already in the BSOD wait-loop, just attach and read the sticky regs live —
`CFSR/BFAR/MMFAR/ABFSR` survive, and the stacked frame is still on the stack.

### Break on a specific call and read its arguments

Example: halt on `scroll_window_up`'s `memmove` (overlay addr in RAM) only when its
size arg (`r2`) is absurd, and dump the args + the `WindowInfo` (`w` = `r5`):

```bash
arm-none-eabi-gdb build/gw_retro_go.elf -batch \
  -ex "set pagination off" -ex "target extended-remote :3333" \
  -ex "hbreak *0x2408d114 if \$r2 >= 0x10000" -ex "continue" \
  -ex "printf \"dest=%08x src=%08x n=%08x  w=%08x\n\", \$r0, \$r1, \$r2, \$r5"
```

Find such an overlay address with the map (gdb/addr2line alias across cores, so
resolve via the specific core's objects):

```bash
arm-none-eabi-nm -n build/gw_retro_go.elf | grep ' scroll_window_up'   # symbol
# or which .o owns an addr:
grep -nE "0x2408d1" build/gw_retro_go.map | grep build/earthbound
arm-none-eabi-objdump -dr --disassemble=scroll_window_up.part.0 build/earthbound/window.o
```

## Backup / restore save states

```bash
make GNW_TARGET=mario INTFLASH_BANK=2 flash_saves_backup    # device -> backup/
make GNW_TARGET=mario INTFLASH_BANK=2 flash_saves_restore   # backup/ -> device
```

## Debug-rig gotchas (learned the hard way)

- **`make flash` says "No rule to make target 'flash'" while OpenOCD holds the
  probe** — the Makefile only defines flash targets when adapter detection
  succeeds. Kill the debugger first:
  `pkill -f "gnwmanager gdbserver"; pkill -f openocd`.
  (The same error also appears if make runs from the wrong cwd, e.g. inside
  `external/earthbound`.)
- **`arm-none-eabi-gdb -batch ... -ex detach` can leave the target HALTED**
  (game frozen, counters stop). Verify/recover via the OpenOCD tcl port 6666:
  `targets` shows the state, `resume` fixes it. For plain memory reads prefer
  tcl `halt` / `mdw` / `resume` — it never wedges.
- **Never pipe a build through `head`** — when `head` exits it SIGPIPE-kills
  make mid-link, and an `&&`-chained `sdpush` then pushes stale binaries.
  Redirect to a log file and grep/tail that instead.
