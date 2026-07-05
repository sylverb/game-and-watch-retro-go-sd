# EB whole-frame PC-sampling profiler

Statistical profiler for the running EarthBound core over the ST-Link. Unlike the
in-core BGPROF (which only instruments `ppu_render.c` phases), this samples the
**entire** frame — libc, flash-shell, CPU-emu, audio — so it finds cost BGPROF is
blind to (e.g. it revealed `memset` = ~24% of the overworld frame).

## Usage

```bash
# 1. start the gdbserver (spawns OpenOCD: gdb:3333 / telnet:4444 / tcl:6666)
gnwmanager gdbserver &

# 2. drive EarthBound to the scene of interest on the device, keep it active

# 3. sample N PCs (+LR), then resolve against the map
python3 tools/eb_profile/eb_sample.py 1200 /tmp/pcs.txt
python3 tools/eb_profile/eb_resolve.py build/gw_retro_go.map /tmp/pcs.txt
```

## How it works

- `eb_sample.py` talks to OpenOCD's **tcl port 6666** (synchronous `halt`/`reg pc`/
  `reg lr`/`resume` + random 3–25 ms sleep). tcl framing = command + `\x1a`.
  `lr` is captured too: `memset`/`memcpy` are leaf functions, so their sampled `lr`
  is the caller's return address (→ who's clearing/copying).
- `eb_resolve.py` parses `build/gw_retro_go.map`, pairing each `.text.<fn>` section
  with its `build/earthbound/*.o`. In the `0x24xxxxxx` overlay window it keeps **only
  earthbound** objects, which kills the cross-core VMA aliasing (zelda3/smw link at the
  same VMA). Prints REGION / OBJECT / FUNCTION histograms plus a "callers of
  memset/memcpy" breakdown via `lr`.

Note: the debugger halt/resume overhead squeezes out the game's idle slack, so samples
approximate the distribution of **active** compute (the idle bucket ~vanishes) — good
for "where does compute go", not wall-clock %.
