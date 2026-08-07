#include <odroid_system.h>

#include <string.h>
#include "gw_lcd.h"
#include "gw_buttons.h"
#include "rom_manager.h"
#include "common.h"
#include "gw_malloc.h"
#include "gw_flash_alloc.h"
#include "rg_storage.h"
#include "odroid_overlay.h"
#include "appid.h"
#include "bilinear.h"
#include "error_screens.h"

/* Everything above is a normal firmware header (see gw_core_bridge.h's file
 * comment: include AFTER those so macro-substitution rewrites later *uses*,
 * not the extern declarations this file relies on for type checking). */
#include "gw_core_bridge.h"

/* Linker symbols from cores/gba/gba_core.ld (sentinel patch range). */
extern uint8_t _GBA_MAIN_CODE_END[];
extern uint8_t _OVERLAY_GBA_BSS_START[];

/* ABI: host CPU clock after overclock (see gw_firmware_abi_t). */
extern uint32_t get_SystemCoreClock(void);

/* gpsp. The core's own headers pull in libretro types and register-name macros
 * that collide with CMSIS, so we declare the handful of entry points we use. */
#include "gba_savestate_abi.h"
#include "gba_idle_loop.h"
#include "gba_audio_filter.h"

extern uint32_t  idle_loop_target_pc; /* gpSP's; gba_frontend.c owns the storage */
extern uint32_t  idle_loop_cond;      /* 0 = always burn; 1 = only while the branch loops */
extern uint16_t *gba_screen_pixels; /* the core renders straight into this, RGB565 */
extern uint32_t  execute_cycles;    /* cycles the core wants to run before the next event */
extern uint32_t  skip_next_frame;   /* set and the PPU evaluates but does not draw */
extern uint8_t   bios_rom[16 * 1024];
extern uint8_t   gamepak_backup[128 * 1024];
extern const uint8_t open_gba_bios_rom[];

void     init_main(void);
void     init_memory(void);
void     init_sound(void);
void     init_gamepak_buffer(void);
void     reset_gba(void);
void     execute_arm(uint32_t cycles);
void     gba_set_xip_rom(uint8_t *base, uint32_t size);
void     gba_set_keys(uint32_t keys);
uint32_t load_gamepak(const void *info, const char *name, int rtc, int rumble, int serial);
/* load_gamepak's last three arguments. Spelled out here rather than included: gpSP's
 * headers collide with CMSIS (see the note at the top of this file).
 *
 * The two scales are NOT the same, which is the trap. rtc and rumble are a tri-state
 * where 0 means DISABLE and -1 means "ask the cart" (gba_memory.h:25-27). serial is a
 * MODE, where 0 means disabled and "auto" is 6 (serial.h:20-26) — so a -1 there is not
 * "no opinion", it is a serial mode that does not exist. */
#define FEAT_AUTODETECT       (-1)
#define FEAT_DISABLE            0
#define FEAT_ENABLE             1
#define SERIAL_MODE_DISABLED    0
#define SERIAL_MODE_AUTO        6
uint32_t sound_read_samples(int16_t *out, uint32_t frames);
uint32_t sound_fifo_rate_hz(void);
#if CHEAT_CODES == 1
/* gpsp's cheat engine: GameShark / CodeBreaker / Action Replay, 20 slots. */
int  cheat_parse(unsigned index, const char *code);
void cheat_clear(void);
#define GBA_MAX_CHEAT_SLOTS 20
#endif

#define GBA_WIDTH   240
#define GBA_HEIGHT  160
/* The GBA's real frame: 280,896 cycles of a 16.777216 MHz clock. That is 59.7275 fps,
 * and every rate below is derived from it rather than from a rounded 60 — a 0.45%
 * error is a sample buffer lapping itself every nine seconds. */
#define GBA_FRAME_CYCLES  280896.0f
#define GBA_CPU_HZ        16777216.0f
#define GBA_FPS     60          /* only for anything that must name a whole number */
#define LCD_WIDTH   320
#define LCD_HEIGHT  240

/* The mixer runs at the rate the SAI is already set to, so nothing has to be
 * resampled on a budget that has no room for it (gpsp's own default is 65536Hz).
 * The device has one speaker, so the core's stereo pair is folded to mono on the
 * way out — half the samples to touch, and nothing is lost that could be heard. */
#define GBA_SAMPLE_RATE          48000

/* Samples per frame — and NOT 48000/60.
 *
 * A GBA frame is 280,896 cycles of a 16.777216 MHz clock: 16.7427 ms, which is
 * 59.7275 fps, not 60. At 48 kHz that is 803.65 samples a frame, and gpSP produces
 * exactly that many. Asking for 800 left 3.65 of them behind every frame, and gpSP's
 * ring holds 2048 — so it filled and lapped itself about every nine seconds, after
 * which sound_read_samples() returned a nonsense count and the code below filled the
 * rest of the buffer with ZEROES. A waveform yanked to zero and back is a click. That
 * was the crackle, and it was never the speaker.
 *
 * 804 is a hair more than is produced, which is the safe side to err on: the ring
 * drains to its floor and stays there instead of lapping, and the shortfall is about
 * a third of a sample per frame — held, not zeroed, below. */
#define GBA_AUDIO_FRAMES         804
static int16_t gba_audio_stereo[GBA_AUDIO_FRAMES * 2];

/* The GBA framebuffer. The LCD's own buffers live outside RAM_EMU, but the core
 * renders a 240x160 image that then has to be scaled, so it needs a source.
 *
 * It comes from AHB SRAM, not from the overlay: 75 KB of a 724 KB pool that this
 * core has already very nearly spent, against 120 KB of AHB that nothing else is
 * using while a game runs. The scaler reads it once per frame and the DMA never
 * touches it, so the slower bus costs nothing that shows. */
#define GBA_FRAMEBUFFER_BYTES  (GBA_WIDTH * GBA_HEIGHT * sizeof(uint16_t))
static uint16_t *gba_framebuffer;

static odroid_video_frame_t video_frame = {GBA_WIDTH, GBA_HEIGHT, GBA_WIDTH * 2, 2, 0xFF, -1, NULL, NULL, 0, {}};

static void blit_emulator(void);

/* ------------------------------------------------------------------- diag ---
 * Where the frame actually goes.
 *
 * The overlay says FPS 40 and CPU 69% — enough to know we are waiting on the audio
 * tick, not enough to know WHAT is late. Emulating the ARM7 and drawing the screen
 * are the only two candidates and they want opposite fixes: the interpreter wants
 * clock and ITCM, the renderer wants to be out of flash. Guessing which cost two
 * builds already. So measure both, and put the answer in the pause menu.
 *
 * Averaged over a second so a single heavy frame does not read as a trend. */
typedef struct {
    uint32_t cycles;   /* accumulated since the last publish */
    uint32_t frames;
    uint32_t us;       /* published: microseconds per frame */
} gba_diag_t;

/* execute_arm() is TWO things. gpSP renders the picture from inside it — the PPU runs
 * per scanline, as the ARM7 executes — so "Emulate" is the interpreter AND the
 * renderer added together, and the two want opposite fixes.
 *
 * A skipped frame is what tells them apart: skip_next_frame makes the PPU evaluate the
 * scanline but not draw it. So time execute_arm() separately on frames that drew and
 * frames that did not, and the difference IS the renderer.
 *
 *   Emu+ppu   execute_arm() on a frame that rendered
 *   Emu only  execute_arm() on a frame that skipped rendering
 *   PPU       the difference — what drawing the picture actually costs
 */
static gba_diag_t diag_emu_draw;
static gba_diag_t diag_emu_skip;
static gba_diag_t diag_scale;
static gba_diag_t diag_overlay;
static gba_diag_t diag_wait;
static uint32_t   diag_last_tick;
static char       diag_emu_draw_str[16] = "-";
static char       diag_emu_skip_str[16] = "-";
static char       diag_ppu_str[16]      = "-";
static char       diag_scale_str[16]    = "-";
static char       diag_overlay_str[16]  = "-";
static char       diag_wait_str[16]     = "-";
/* Which of blit_emulator()'s branches actually ran. Scaling and filtering are user
 * settings, and SOFT does not go anywhere near the nearest-neighbour scaler — it
 * clears the whole 153 KB buffer and then runs a bilinear resample. */
static char       diag_path_str[16]    = "-";

static inline void gba_diag_add(gba_diag_t *d, uint32_t cycles)
{
    d->cycles += cycles;
    d->frames++;
}

static void gba_diag_format(gba_diag_t *d, char *out, size_t out_len)
{
    if (d->frames == 0) {
        snprintf(out, out_len, "-");
    } else {
        /* get_SystemCoreClock() is whatever the overclock left us at, so this stays true
         * across the OC levels rather than assuming 280MHz. */
        uint32_t per_frame = d->cycles / d->frames;
        d->us = (uint32_t)((uint64_t)per_frame * 1000000u / get_SystemCoreClock());
        snprintf(out, out_len, "%lu.%02lu ms",
                 (unsigned long)(d->us / 1000), (unsigned long)((d->us % 1000) / 10));
    }
    d->cycles = 0;
    d->frames = 0;
}

static void gba_diag_publish(void)
{
    uint32_t now = HAL_GetTick();
    if (now - diag_last_tick < 1000)
        return;
    diag_last_tick = now;

    gba_diag_format(&diag_emu_draw, diag_emu_draw_str, sizeof(diag_emu_draw_str));
    gba_diag_format(&diag_emu_skip, diag_emu_skip_str, sizeof(diag_emu_skip_str));
    gba_diag_format(&diag_scale, diag_scale_str, sizeof(diag_scale_str));
    gba_diag_format(&diag_overlay, diag_overlay_str, sizeof(diag_overlay_str));
    gba_diag_format(&diag_wait, diag_wait_str, sizeof(diag_wait_str));

    /* What the picture costs: the same emulation, once with the PPU drawing and once
     * without. Only meaningful when frameskip is actually skipping something. */
    if (diag_emu_draw.us > diag_emu_skip.us && diag_emu_skip.us > 0) {
        uint32_t ppu = diag_emu_draw.us - diag_emu_skip.us;
        snprintf(diag_ppu_str, sizeof(diag_ppu_str), "%lu.%02lu ms",
                 (unsigned long)(ppu / 1000), (unsigned long)((ppu % 1000) / 10));
    } else {
        snprintf(diag_ppu_str, sizeof(diag_ppu_str), "no skips");
    }
}

/* ------------------------------------------------------------------ fatal --- */
/* Say which failure it was, and stay on screen while it is read.
 *
 * Not an alert-and-return-to-the-launcher: the two ways loading fails here (no
 * room left in the flash cache for the cart, a cart the core will not take) are
 * different problems with different fixes, and a single "load failed" toast that
 * vanishes tells the player neither. Not a sleep either — deep sleep comes back
 * through gw_sleep.c, whose sdcard_init() then fails, and the last thing left on
 * screen is "No SD CARD found": a message about a card that was never the problem
 * (this cost half a day once, on Super Metroid). */
static void __attribute__((noreturn)) gba_fatal(const char *line_1, const char *line_2)
{
    printf("gba: FATAL %s / %s\n", line_1, line_2 ? line_2 : "");
    lcd_backlight_set(180);
    draw_error_screen("GAME BOY ADVANCE", line_1, line_2);

    while (true) {
        wdog_refresh();
        lcd_sync();
        lcd_swap();
        HAL_Delay(20);
    }
}

/* -------------------------------------------------------------------- XIP ---
 * gpSP is 853 KB of core against a 724 KB pool. The scanline renderer (video.o)
 * and the 16 KB BIOS image are linked at a sentinel address instead, shipped as
 * one file — /cores/gba.xip — cached into QSPI flash, and executed and
 * read straight out of it. Same trick as Super Metroid's sm.xip; the linker
 * script says which object goes where and why.
 *
 * Code and BIOS share the region, and therefore one cache entry, on purpose: the
 * renderer's own rodata sits between them, and a pointer from one cache entry
 * into another goes stale the moment the circular cache evicts one and not the
 * other. As a single blob every such pointer is a sentinel into the blob itself,
 * so one relocation against one base fixes all of them at once.
 *
 * The relocation happens on the way IN — the cache hands each buffer to
 * gba_relocate_xip() before programming it — rather than by rewriting flash that
 * has already been written. A rewrite would have to erase first, and an erase
 * interrupted by a flat battery leaves a blank hole indistinguishable from a
 * finished job. On a cache hit nothing is written and nothing needs to be: the
 * copy in flash was relocated to that same address when it was first stored.
 */
#define GBA_CODE_BASE  0xDEC00000u
#define GBA_XIP_PATH   "/cores/gba.xip"
#define GBA_BIOS_PATH  "/bios/gba/gba_bios.bin"

static uint8_t *g_xip_addr;
static uint32_t g_xip_size;
static int32_t  g_xip_offset;

/* Rewrite every sentinel-range word in [start, end) to where the blob really
 * landed. Thumb bit included, hence the & ~1. */
static int patch_gba_sentinels(uint32_t *start, uint32_t *end, int32_t offset, uint32_t size)
{
    int patched = 0;
    for (uint32_t *p = start; p < end; p++) {
        uint32_t v = *p;
        if ((v & ~1u) >= GBA_CODE_BASE && (v & ~1u) < GBA_CODE_BASE + size) {
            *p = (uint32_t)(v + offset);
            patched++;
        }
    }
    return patched;
}

/* Relocation hook: runs on each buffer of gba.xip on its way into the flash. */
static void gba_relocate_xip(uint8_t *buffer, uint32_t length, uint32_t offset_in_file,
                             uint8_t *file_address, uint32_t file_size)
{
    (void)offset_in_file;
    int32_t offset = (int32_t)((uint32_t)file_address - GBA_CODE_BASE);
    patch_gba_sentinels((uint32_t *)buffer, (uint32_t *)(buffer + (length & ~3u)), offset, file_size);
}

/* Where a thing linked into the blob actually ended up. main_gba.o is the one
 * object the sentinel pass below does not walk (it holds GBA_CODE_BASE itself, and
 * a scan that could not tell the constant from a reference would rewrite the very
 * constant it is built on), so the single blob pointer this file holds — the BIOS
 * image — is relocated by hand, here. */
static const void *gba_xip_ptr(const void *sentinel)
{
    return (const void *)((uint32_t)sentinel + g_xip_offset);
}

static bool gba_cache_xip_to_flash(void)
{
    g_xip_size = 0;
    g_xip_addr = odroid_overlay_cache_file_in_flash_relocate(GBA_XIP_PATH, &g_xip_size, false,
                                                             &gba_relocate_xip);
    if (g_xip_addr == NULL || g_xip_size == 0) {
        printf("gba: %s missing\n", GBA_XIP_PATH);
        return false;
    }
    g_xip_offset = (int32_t)((uint32_t)g_xip_addr - GBA_CODE_BASE);
    printf("gba: xip blob at %p, %lu bytes, offset 0x%08lX\n",
           g_xip_addr, (unsigned long)g_xip_size, (unsigned long)g_xip_offset);

    /* Everything in the overlay that points into the blob — the RAM->XIP call
     * veneers into the renderer, and every reference to its rodata — still holds a
     * sentinel. Fix them before a single line of core code runs.
     *
     * The ITCM image (cpu.o, the interpreter) is deliberately not scanned, and
     * does not need to be: it references nothing in the blob. The linker script
     * keeps its rodata in RAM to guarantee that, and nm confirms it calls no
     * function of the renderer's. */
    int n = patch_gba_sentinels((uint32_t *)_GBA_MAIN_CODE_END,
                                (uint32_t *)_OVERLAY_GBA_BSS_START,
                                g_xip_offset, g_xip_size);
    printf("gba: patched %d sentinel refs in the overlay\n", n);
    return true;
}

/* Prefer the official BIOS from SD when present, otherwise fall back to the
 * bundled open BIOS in gba.xip. A partial read is treated as invalid and we
 * keep the open BIOS to avoid booting with garbage content. */
static void gba_load_bios(void)
{
    FILE *f = fopen(GBA_BIOS_PATH, "rb");
    if (f != NULL) {
        size_t n = fread(bios_rom, 1, sizeof(bios_rom), f);
        int extra = fgetc(f);
        fclose(f);
        if (n == sizeof(bios_rom) && extra == EOF) {
            printf("gba: using official BIOS from %s\n", GBA_BIOS_PATH);
            return;
        }
        printf("gba: ignoring %s (expected exactly %u bytes)\n",
               GBA_BIOS_PATH, (unsigned)sizeof(bios_rom));
    }

    memcpy(bios_rom, gba_xip_ptr(open_gba_bios_rom), sizeof(bios_rom));
    printf("gba: using bundled open BIOS\n");
}

/* ------------------------------------------------------------------- SRAM --- */
/* The cart's own save — the one the game writes when you save in-game. This is
 * what a Pokemon player actually cares about; a savestate is a convenience on
 * top of it. */
static void gba_SramSave(void)
{
    char *path = odroid_system_get_path(ODROID_PATH_SAVE_SRAM, ACTIVE_FILE->path);
    FILE *f = fopen(path, "wb");
    if (f != NULL) {
        fwrite(gamepak_backup, 1, sizeof(gamepak_backup), f);
        fclose(f);
    }
    free(path);
}

static void gba_SramLoad(void)
{
    char *path = odroid_system_get_path(ODROID_PATH_SAVE_SRAM, ACTIVE_FILE->path);
    FILE *f = fopen(path, "rb");
    if (f != NULL) {
        fread(gamepak_backup, 1, sizeof(gamepak_backup), f);
        fclose(f);
    }
    free(path);
}

/* -------------------------------------------------------------- savestate --- */
/* gpsp builds its savestate as one contiguous bson document, and that document is
 * 416KB because six buffers — iwram, ewram, vram, oam, palette, ioregs — are
 * ~390KB of it. There is no 416KB block free here; the largest is the 300KB LCD
 * pool. So the six go straight to the file and only the rest, 4,333 bytes of it,
 * is ever held in RAM (gba_bulk_regions() / gba_save_state_slim()).
 *
 * Order matters on the way back in: the bulk buffers have to be in place before
 * the slim document is applied, because applying it rebuilds the palette cache
 * from palette_ram — and a load restores state, not the caches derived from it. */
#define GBA_STATE_MAGIC   0x41425347u  /* 'GBAS' */
#define GBA_STATE_VER     1

typedef struct {
    uint32_t magic;
    uint32_t version;
    uint32_t slim_len;   /* bytes of the bson document that follow */
    uint32_t bulk_len;   /* bytes of raw buffers after that */
} gba_state_header_t;

static bool gba_SaveState(const char *savePathName)
{
    unsigned nreg = 0;
    const gba_bulk_region_t *rg = gba_bulk_regions(&nreg);
    uint8_t *slim = (uint8_t *)lcd_get_active_buffer();   /* borrow the off-screen buffer */

    lcd_wait_for_vblank();
    gba_save_state_slim(slim);

    uint32_t slim_len = *(uint32_t *)slim;
    uint32_t bulk_len = 0;
    for (unsigned i = 0; i < nreg; i++)
        bulk_len += rg[i].len;

    FILE *f = fopen(savePathName, "wb");
    if (f == NULL)
        return false;

    gba_state_header_t h = {GBA_STATE_MAGIC, GBA_STATE_VER, slim_len, bulk_len};
    bool ok = fwrite(&h, sizeof(h), 1, f) == 1 &&
              fwrite(slim, 1, slim_len, f) == slim_len;
    for (unsigned i = 0; ok && i < nreg; i++)
        ok = fwrite(rg[i].ptr, 1, rg[i].len, f) == rg[i].len;

    fclose(f);
    lcd_clear_active_buffer();
    return ok;
}

static bool gba_LoadState(const char *savePathName)
{
    unsigned nreg = 0;
    const gba_bulk_region_t *rg = gba_bulk_regions(&nreg);

    /* The main loop is blocked for the whole read below, so the audio DMA would
     * otherwise loop its last buffer — a latched tone — until playback resumes. */
    audio_clear_buffers();

    FILE *f = fopen(savePathName, "rb");
    if (f == NULL)
        return false;

    gba_state_header_t h;
    if (fread(&h, sizeof(h), 1, f) != 1 || h.magic != GBA_STATE_MAGIC ||
        h.version != GBA_STATE_VER || h.slim_len == 0 ||
        h.slim_len > GBA_STATE_SLIM_SIZE) {
        fclose(f);
        return false;
    }

    uint32_t bulk_len = 0;
    for (unsigned i = 0; i < nreg; i++)
        bulk_len += rg[i].len;
    if (h.bulk_len != bulk_len) {   /* a state this build did not write */
        fclose(f);
        return false;
    }

    uint8_t *slim = (uint8_t *)lcd_get_active_buffer();
    bool ok = fread(slim, 1, h.slim_len, f) == h.slim_len;
    for (unsigned i = 0; ok && i < nreg; i++)
        ok = fread(rg[i].ptr, 1, rg[i].len, f) == rg[i].len;
    fclose(f);

    if (!ok)
        return false;

    /* Bulk first, then the document: applying it rebuilds the palette cache out
     * of the palette_ram we just restored. */
    if (!gba_load_state_slim(slim))
        return false;

    lcd_clear_active_buffer();
    return true;
}

static void *gba_Screenshot(void)
{
    lcd_wait_for_vblank();
    lcd_clear_active_buffer();
    blit_emulator();
    return lcd_get_active_buffer();
}

/* ------------------------------------------------------------------ audio --- */
static void gba_pcm_submit(void)
{
    uint32_t got = sound_read_samples(gba_audio_stereo, GBA_AUDIO_FRAMES);

    if (common_emu_sound_loop_is_muted())
        return;

    int32_t   factor = common_emu_sound_get_volume();
    int16_t  *out    = audio_get_active_buffer();
    uint16_t  len    = audio_get_buffer_length();

    if (len > GBA_AUDIO_FRAMES)
        len = GBA_AUDIO_FRAMES;

    /* Hold the last sample when the core comes up short; do not slam to zero.
     *
     * A shortfall is normal here — we ask for a hair more than a frame produces, on
     * purpose (see GBA_AUDIO_FRAMES) — so this runs a fraction of a sample per frame.
     * Repeating a sample for 20 microseconds is inaudible. Dropping the waveform to
     * zero and back is a click, and doing it every frame is a crackle. */
    static int16_t last_mono = 0;

    for (uint16_t i = 0; i < len; i++) {
        /* One speaker: fold the pair rather than throw a channel away. Anything
         * panned hard to the side would otherwise vanish. */
        if (i < got) {
            last_mono = (int16_t)(((int32_t)gba_audio_stereo[i * 2] +
                                   gba_audio_stereo[i * 2 + 1]) / 2);
        }
        out[i] = last_mono;
    }

    /* The analog rolloff the real console has and our clean DAC does not —
     * cutoff follows the rate this cart is clocking its FIFOs at, so the
     * resampling images go and the music stays. See gba_audio_filter.h. */
    gba_lpf_configure(sound_fifo_rate_hz());
    gba_lpf_apply(out, len);

    for (uint16_t i = 0; i < len; i++)
        out[i] = (int16_t)(((int32_t)out[i] * factor) >> 8);

}

/* ------------------------------------------------------------------ video --- */
/* The nearest-neighbour source column for every destination column. It is the same
 * for all 213 rows, so it is computed once per scaling mode instead of doing a
 * multiply and a shift for each of ~68,000 pixels, every frame. */
static uint16_t nn_xmap[LCD_WIDTH];
static int32_t  nn_xmap_width = -1;

__attribute__((optimize("unroll-loops")))
static inline void screen_blit_nn(int32_t dest_width, int32_t dest_height)
{
    int w1 = video_frame.width;
    int h1 = video_frame.height;
    int w2 = dest_width;
    int h2 = dest_height;

    int y_ratio = (int)((h1 << 16) / h2) + 1;
    int hpad = (LCD_WIDTH - dest_width) / 2;
    int wpad = (LCD_HEIGHT - dest_height) / 2;

    if (nn_xmap_width != dest_width) {
        int x_ratio = (int)((w1 << 16) / w2) + 1;
        for (int j = 0; j < w2; j++)
            nn_xmap[j] = (uint16_t)((j * x_ratio) >> 16);
        nn_xmap_width = dest_width;
    }

    uint16_t *screen_buf = (uint16_t *)video_frame.buffer;
    uint16_t *dest = lcd_get_active_buffer();

    /* Write every pixel of the buffer, borders included. A separate clear is a
     * ~150KB memset that can overtake the beam and desync the vblank swap. */
    for (int i = 0; i < wpad; i++)
        memset(dest + i * LCD_WIDTH, 0, LCD_WIDTH * sizeof(uint16_t));
    for (int i = wpad + h2; i < LCD_HEIGHT; i++)
        memset(dest + i * LCD_WIDTH, 0, LCD_WIDTH * sizeof(uint16_t));

    /* Two pixels per store.
     *
     * The LCD pool is uncached AND unbuffered (see ._ram_uc in the linker script), so
     * every write to it is a bus round trip the CPU stalls on until it completes. FIT
     * writes 320x213 pixels a frame; one 16-bit store each is 68,160 round trips, and
     * that — not the scaling arithmetic — was 9.1 ms of a 16.67 ms frame. Packing two
     * pixels into one 32-bit store halves the trips.
     *
     * 32-bit, not 64: a Cortex-M7 traps an unaligned STRD, and Super Metroid already
     * died once on exactly that (see the root CLAUDE.md). Rows are 640 bytes, so a row
     * start is always 4-byte aligned, and hpad is even for every scaling mode here —
     * but a 64-bit store would need 8, which is not guaranteed. */
    for (int i = 0; i < h2; i++) {
        uint16_t *row = dest + (i + wpad) * LCD_WIDTH;
        int y2 = ((i * y_ratio) >> 16);
        const uint16_t *src_row = screen_buf + (y2 * w1);

        for (int j = 0; j < hpad; j++)
            row[j] = 0;

        uint32_t *out32 = (uint32_t *)(row + hpad);
        int pairs = w2 >> 1;
        for (int j = 0; j < pairs; j++) {
            uint32_t lo = src_row[nn_xmap[j * 2]];
            uint32_t hi = src_row[nn_xmap[j * 2 + 1]];
            out32[j] = lo | (hi << 16);
        }
        if (w2 & 1)
            row[hpad + w2 - 1] = src_row[nn_xmap[w2 - 1]];

        for (int j = hpad + w2; j < LCD_WIDTH; j++)
            row[j] = 0;
    }
}

static void screen_blit_bilinear(int32_t dest_width)
{
    int hpad = (LCD_WIDTH - dest_width) / 2;
    uint16_t *dest = lcd_get_active_buffer();

    image_t dst_img = {dest_width, LCD_HEIGHT, 2, ((uint8_t *)dest) + hpad * 2};
    image_t src_img = {video_frame.width, video_frame.height, 2, video_frame.buffer};

    if (hpad > 0)
        memset(dest, 0x00, hpad * 2);

    imlib_draw_image(&dst_img, &src_img, 0, 0, LCD_WIDTH,
                     ((float)dest_width) / ((float)video_frame.width),
                     ((float)LCD_HEIGHT) / ((float)video_frame.height),
                     NULL, -1, 255, NULL, NULL, IMAGE_HINT_BILINEAR, NULL, NULL);
}

static void blit_emulator(void)
{
    /* NOT lcd_sleep_while_swap_pending() — that is a wait, and a wait timed together
     * with work reads as work. It moves to blit(), where it is timed on its own.
     *
     * This is the second time the same trap has cost a build. "Draw 9.1 ms" on the
     * title screen was this wait; on a screen where the CPU had slack, the slack
     * showed up there and read as an expensive renderer. Then Scale jumped from
     * 1.99 ms to 4.47 ms between two scenes — for a fixed-size blit that cannot
     * depend on the scene at all. Same wait, same lie. */
    odroid_display_scaling_t scaling = odroid_display_get_scaling_mode();
    odroid_display_filter_t filtering = odroid_display_get_filter_mode();

    /* Which branch ran. SOFT does not touch the nearest-neighbour scaler at all: it
     * clears all 153 KB of the buffer and then bilinear-resamples into it. */
    snprintf(diag_path_str, sizeof(diag_path_str), "%s/%s",
             scaling == ODROID_DISPLAY_SCALING_OFF  ? "off"
           : scaling == ODROID_DISPLAY_SCALING_FIT  ? "fit"
           : scaling == ODROID_DISPLAY_SCALING_FULL ? "full" : "cust",
             filtering == ODROID_DISPLAY_FILTER_SOFT ? "soft" : "hard");

    static odroid_display_scaling_t last_scaling = -1;
    if (scaling != last_scaling) {
        lcd_clear_buffers();
        last_scaling = scaling;
    }

    /* 240x160 is 3:2. Filling the 240px height would need 360px of width, which
     * the 320px panel does not have — so FIT fills the width instead and letter-
     * boxes: 320x213. */
    switch (scaling) {
    case ODROID_DISPLAY_SCALING_OFF:
        screen_blit_nn(GBA_WIDTH, GBA_HEIGHT);   /* native, centred */
        break;
    case ODROID_DISPLAY_SCALING_FIT:
        if (filtering == ODROID_DISPLAY_FILTER_SOFT) {
            lcd_clear_active_buffer();           /* bilinear does not fill the borders */
            screen_blit_bilinear(LCD_WIDTH);
        } else {
            screen_blit_nn(LCD_WIDTH, 213);
        }
        break;
    case ODROID_DISPLAY_SCALING_FULL:
    case ODROID_DISPLAY_SCALING_CUSTOM:
        if (filtering == ODROID_DISPLAY_FILTER_SOFT)
            screen_blit_bilinear(LCD_WIDTH);
        else
            screen_blit_nn(LCD_WIDTH, LCD_HEIGHT);
        break;
    default:
        screen_blit_nn(LCD_WIDTH, 213);
        break;
    }
}

/* Draw is 9.1 ms of a 16.67 ms frame and halving the pixel stores changed it by
 * 0.02 ms — which says the stores were never the cost, and that the code I changed
 * may not even be the code that runs. blit() is three different things depending on
 * two settings, so stop guessing and split it: the scaler, the overlay, and which
 * path was taken. */
static void blit(void)
{
    /* The wait for the LCD's previous swap to finish. Idle time, not work — and the
     * one number here that going FASTER makes bigger, because the slack has to land
     * somewhere. Timed on its own so it stops being mistaken for the renderer. */
    common_emu_clear_dwt_cycles();
    lcd_sleep_while_swap_pending();
    gba_diag_add(&diag_wait, common_emu_get_dwt_cycles());

    common_emu_clear_dwt_cycles();
    blit_emulator();
    gba_diag_add(&diag_scale, common_emu_get_dwt_cycles());

    common_emu_clear_dwt_cycles();
    common_ingame_overlay();
    gba_diag_add(&diag_overlay, common_emu_get_dwt_cycles());
}

/* ------------------------------------------------------------------ input --- */
/* KEYINPUT bit order, and what gpsp's gba_set_keys() expects: a set bit is held. */
#define GBA_KEY_A      0x0001
#define GBA_KEY_B      0x0002
#define GBA_KEY_SELECT 0x0004
#define GBA_KEY_START  0x0008
#define GBA_KEY_RIGHT  0x0010
#define GBA_KEY_LEFT   0x0020
#define GBA_KEY_UP     0x0040
#define GBA_KEY_DOWN   0x0080
#define GBA_KEY_R      0x0100
#define GBA_KEY_L      0x0200

static void gba_input_read(odroid_gamepad_state_t *joystick)
{
    uint32_t keys = 0;
    if (joystick->values[ODROID_INPUT_UP])     keys |= GBA_KEY_UP;
    if (joystick->values[ODROID_INPUT_DOWN])   keys |= GBA_KEY_DOWN;
    if (joystick->values[ODROID_INPUT_LEFT])   keys |= GBA_KEY_LEFT;
    if (joystick->values[ODROID_INPUT_RIGHT])  keys |= GBA_KEY_RIGHT;
    if (joystick->values[ODROID_INPUT_A])      keys |= GBA_KEY_A;
    if (joystick->values[ODROID_INPUT_B])      keys |= GBA_KEY_B;
    if (joystick->values[ODROID_INPUT_START])  keys |= GBA_KEY_START;
    if (joystick->values[ODROID_INPUT_SELECT]) keys |= GBA_KEY_SELECT;
    /* The unit has no shoulder buttons. Y/X stand in for L/R — every GBA game
     * that uses them uses them, and there is nowhere else to put them. */
    if (joystick->values[ODROID_INPUT_X])      keys |= GBA_KEY_R;
    if (joystick->values[ODROID_INPUT_Y])      keys |= GBA_KEY_L;

    gba_set_keys(keys);
}

/* gw_sleep() restores the *settings* OC level on wake, but GBA always forces
 * level 3 (~353 MHz) during gameplay. Without this the core keeps running at
 * the (slower) settings clock after a sleep/wake cycle. Re-apply the boost
 * and reinit audio (SystemClock_Config also reprograms the audio PLL). */
static void gba_sleep_wake_up(void)
{
    SystemClock_Config(3);
    odroid_audio_init(odroid_audio_sample_rate_get());
    audio_start_playing(GBA_AUDIO_FRAMES);
}

/* ------------------------------------------------------------------- main --- */
void app_main_gba(uint8_t load_state, uint8_t start_paused, int8_t save_slot)
{
    odroid_gamepad_state_t joystick;
    /* Read-only (enabled = -1): the frame budget is 16.67ms, and these two say who
     * is spending it. If Emulate dominates, the answer is clock and the interpreter.
     * If Draw does, the answer is the renderer and where its code lives. */
    odroid_dialog_choice_t options[] = {
        {0, "Emu+ppu",  diag_emu_draw_str, -1, NULL},
        {0, "Emu only", diag_emu_skip_str, -1, NULL},
        {0, " = PPU",   diag_ppu_str,      -1, NULL},
        {0, "Scale",    diag_scale_str,    -1, NULL},
        {0, "Ovl",      diag_overlay_str,  -1, NULL},
        {0, "LCD wait",  diag_wait_str,   -1, NULL},
        {0, "Path",     diag_path_str,     -1, NULL},
        ODROID_DIALOG_CHOICE_LAST
    };

    if (start_paused) {
        common_emu_state.pause_after_frames = 2;
        odroid_audio_mute(true);
    } else {
        common_emu_state.pause_after_frames = 0;
    }
    /* 1674, not 1667. A GBA frame is 16.7427 ms — 59.7275 fps, not 60 — and the pacing
     * loop's idea of a frame has to be the same one the audio DMA enforces, or the two
     * fight and the loser is a dropped frame. The LCD is told 60 because its refresh
     * rate is a hardware setting with no 59.7 to choose. */
    common_emu_state.frame_time_10us = (uint16_t)(100000.0f * GBA_FRAME_CYCLES / GBA_CPU_HZ + 0.5f);
    lcd_set_refresh_rate(60);

    /* Level 3 (~353 MHz): force the old max PLL */
    SystemClock_Config(3);

    /* AHB BSS (bios_rom / cheats / sound_buffer) lives in the AHB core segment
     * (see gba_core.ld). run_dynamic_core() zeroes that segment's bss_size
     * before jumping here — no manual memset needed. */

    gba_framebuffer = ahb_malloc(GBA_FRAMEBUFFER_BYTES);
    if (gba_framebuffer == NULL)
        gba_fatal("Out of AHB SRAM", "The 75KB framebuffer could not be allocated");
    memset(gba_framebuffer, 0, GBA_FRAMEBUFFER_BYTES);

    video_frame.buffer = gba_framebuffer;
    gba_screen_pixels = gba_framebuffer;

    odroid_system_init(APPID_GBA, GBA_SAMPLE_RATE);
    odroid_system_emu_init(&gba_LoadState, &gba_SaveState, &gba_Screenshot,
                           NULL, &gba_sleep_wake_up, &gba_SramSave, NULL);

    /* Native 240x160 is a small island on a 320x240 panel; FIT is the sane
     * first-run default. Any choice the user makes afterwards is theirs. */
    if (odroid_display_get_scaling_mode() == ODROID_DISPLAY_SCALING_OFF)
        odroid_display_set_scaling_mode(ODROID_DISPLAY_SCALING_FIT);

    audio_start_playing(GBA_AUDIO_FRAMES);
    gba_lpf_reset();

    /* Before any core code runs: init_main() and everything after it call into the
     * renderer, and those calls are still pointing at the sentinel address until
     * the blob has been cached and the overlay patched. */
    if (!gba_cache_xip_to_flash())
        gba_fatal("Missing " GBA_XIP_PATH, "Re-run the retro-go_update.bin update");

    init_main();
    init_memory();
    init_sound();

    gba_load_bios();
    memset(gamepak_backup, 0xFF, sizeof(gamepak_backup));

    /* The ROM is up to 32MB and stays in external flash, memory-mapped. Nothing
     * is copied into RAM and nothing is paged: the core reads the cart where it
     * lies. Page 0 is the exception — an RTC cart has its GPIO registers *written*
     * into "ROM" at 0x080000C4, and flash does not take writes, so the core keeps
     * a RAM shadow of it. Ruby, Sapphire and Emerald all keep time that way. */
    uint32_t rom_size = 0;
    uint8_t *rom = odroid_overlay_cache_file_in_flash(ACTIVE_FILE->path, &rom_size, false);
    if (rom == NULL || rom_size == 0)
        gba_fatal("Could not cache the ROM in flash", "The cart may be larger than the free flash");
    gba_set_xip_rom(rom, rom_size);
    init_gamepak_buffer();

    /* force_rtc, force_rumble, force_serial — and only the first one changes.
     *
     * RTC: -1, not 0. Zero is not "no opinion" here, it is FEAT_DISABLE — an override
     * that says turn the clock OFF. gpSP recognised Ruby, set rtc_enabled from the
     * cart, and then the 0 we passed switched it straight back off. The game reported
     * that its internal battery had run dry, which for a cart with no working clock is
     * exactly true. Ruby, Sapphire and Emerald all keep time: berries grow, tides turn.
     * Let the cart decide.
     *
     * Rumble: 0 stays. There is no motor in a Game & Watch, so emulating the pak is
     * work with nowhere to land.
     *
     * Serial: 0 stays, and note it is NOT the same tri-state — it is a serial MODE
     * (serial.h:20-26), where 0 is SERIAL_MODE_DISABLED and "auto" is 6. There is no
     * link port either, and leaving it disabled also keeps gba_over.h from turning on
     * Pokemon's serial emulation, which would be per-frame work for a cable that does
     * not exist. */
    if (load_gamepak(NULL, ACTIVE_FILE->path,
                     FEAT_AUTODETECT,        /* rtc: ask the cart          */
                     FEAT_DISABLE,           /* rumble: no motor           */
                     SERIAL_MODE_DISABLED) != 0)   /* serial: no link port */
        gba_fatal("Not a Game Boy Advance ROM", "The header did not check out");

    /* After load_gamepak, on purpose: it is what sets idle_loop_target_pc from
     * gpSP's own gba_over.h, and ours has to win. A game with no busy-wait PC
     * spins through the whole 280,896-cycle frame instead of doing ~75,000 cycles
     * of work and stopping — so this is not a tuning knob, it is the difference
     * between full speed and no chance of it. See gba_idle_loop.c. */
    uint32_t idle_pc = gba_idle_loop_lookup((const char *)&rom[0xAC]);
    if (idle_pc != 0) {
        idle_loop_target_pc = idle_pc;
        printf("gba: idle loop at 0x%08lX\n", (unsigned long)idle_pc);
    }

    /* The OTHER kind of wait: a raster poll — `ldrh rN,[VCOUNT]; cmp; bne` —
     * that the classic always-burn skip must not touch, because these games'
     * delay code CALLS the poll in a counted burst and on hardware ~120 calls
     * fit inside the matching scanline; burn every arrival and a six-frame
     * intro becomes seven hundred (proven: Super Robot Taisen D froze). So the
     * target is the poll's closing branch and the slice burns only while the
     * branch will loop (IDLE_COND_WHEN_NE; the check costs nothing off-match).
     *
     * Hand-curated, one entry per game PROVEN on the host A/B rig
     * (tools/gba_m4a/prove_main.c, IDLE_PC= + IDLE_COND=ne): screens 99.8%
     * identical at a two-frame shift, interpreted instructions -15..-17%.
     * Only for carts with NO entry in the generated idle table — the two
     * waits would otherwise fight over one target slot. */
    static const struct { char code[5]; uint32_t branch_pc; } vcount_polls[] = {
        { "A6SJ", 0x8932178 },   /* Super Robot Taisen D  (-15.2%) */
        { "ATIJ", 0x858f088 },   /* Tennis no Ouji-sama Genius Boys Academy (-16.8%) */
    };
    if (idle_pc == 0) {
        for (size_t i = 0; i < sizeof(vcount_polls) / sizeof(vcount_polls[0]); i++) {
            if (memcmp(vcount_polls[i].code, &rom[0xAC], 4) == 0) {
                idle_loop_target_pc = vcount_polls[i].branch_pc;
                idle_loop_cond = 1;   /* IDLE_COND_WHEN_NE */
                printf("gba: vcount poll at 0x%08lX (cond NE)\n",
                       (unsigned long)idle_loop_target_pc);
                break;
            }
        }
    }

    gba_SramLoad();
    reset_gba();

#if CHEAT_CODES == 1
    /* After reset: parsing a code installs the hook the engine watches for, and a
     * reset would throw it away again. Only the codes the user actually ticked on
     * for this ROM go in. */
    cheat_clear();
    unsigned slot = 0;
    for (int i = 0; i < ACTIVE_FILE->cheat_count && slot < GBA_MAX_CHEAT_SLOTS; i++) {
        if (odroid_settings_ActiveGameGenieCodes_is_enabled(ACTIVE_FILE->path, i))
            cheat_parse(slot++, ACTIVE_FILE->cheat_codes[i]);
    }
#endif

    if (load_state) {
        odroid_system_emu_load_state(save_slot);
    } else {
        lcd_clear_buffers();
    }

    common_emu_enable_dwt_cycles();

    while (true) {
        wdog_refresh();

        bool drawFrame = common_emu_frame_loop();
        skip_next_frame = drawFrame ? 0 : 1;

        odroid_input_read_gamepad(&joystick);
        common_emu_input_loop(&joystick, options, &blit);
        common_emu_input_loop_handle_turbo(&joystick);

        gba_input_read(&joystick);

        /* execute_arm() returns when the frame driver says the frame is done — and it
         * has drawn the picture along the way, unless skip_next_frame said not to. */
        common_emu_clear_dwt_cycles();
        execute_arm(execute_cycles);
        gba_diag_add(drawFrame ? &diag_emu_draw : &diag_emu_skip,
                     common_emu_get_dwt_cycles());

        if (drawFrame) {
            /* No outer timer around blit(): blit() clears the DWT counter itself, and
             * an outer read would then only see whatever came after the last inner
             * clear. That is what made "Draw" report 0.22 ms — exactly the overlay's
             * number — while Scale alone was 1.99 ms. A measurement that quietly
             * measures something else is worse than none. */
            blit();
            lcd_swap();
        }
        gba_diag_publish();

        gba_pcm_submit();

        common_emu_sound_sync(false);
    }
}
