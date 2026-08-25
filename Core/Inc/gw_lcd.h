#ifndef _LCD_H_
#define _LCD_H_

#include "stm32h7xx_hal.h"
#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

#define GW_LCD_WIDTH  320
#define GW_LCD_HEIGHT 240

#ifdef GW_LCD_MODE_LUT8
typedef uint8_t pixel_t;
#else
typedef uint16_t pixel_t;
#endif // GW_LCD_MODE_LUT8

/* Bytes per framebuffer for the current build's pixel format.
 * Replaces sizeof(framebuffer1) — the framebuffer symbols are pointers
 * into a runtime-carved pool now, not arrays. */
#define GW_LCD_FRAME_SIZE   ((size_t)(GW_LCD_WIDTH * GW_LCD_HEIGHT * sizeof(pixel_t)))

/* Framebuffers live in the .lcd_pool linker region (RAM_UC). The C code
 * initialises framebuffer1/framebuffer2 to point at offsets 0 and
 * GW_LCD_FRAME_SIZE within the pool. lcd_setup_framebuffers(LUT8) shrinks
 * that footprint; the upper 150 KiB is GNW_CORE_REGION_RAM_UC / leftover
 * heap (lcd_get_bonus_pool). */
extern pixel_t *framebuffer1;
extern pixel_t *framebuffer2;

typedef enum
{
   LCD_INIT_CLEAR_BUFFERS = 1 << 0
} lcd_init_flags_t;

/* LCD pixel format. Per-emulator switchable at runtime via
 * lcd_setup_framebuffers(). RGB565 is the default for most emulators
 * (2 framebuffers x 150 KiB = 300 KiB). LUT8 is an 8-bit indexed
 * palette mode (2 framebuffers x 75 KiB = 150 KiB), freeing the upper
 * 150 KiB of the LCD pool as overflow memory / an optional core
 * segment (GNW_CORE_REGION_RAM_UC). The CLUT (256 RGB888 entries) is
 * programmed via lcd_set_clut(). */
typedef enum
{
   LCD_MODE_RGB565 = 0,
   LCD_MODE_LUT8   = 1
} lcd_mode_t;

// 0 => framebuffer1
// 1 => framebuffer2
extern uint32_t active_framebuffer;
extern uint32_t frame_counter;

void lcd_deinit(SPI_HandleTypeDef *spi);
void lcd_init(SPI_HandleTypeDef *spi, LTDC_HandleTypeDef *ltdc, lcd_init_flags_t flags);
void *lcd_clear_active_buffer();
void *lcd_clear_inactive_buffer();
void lcd_clear_buffers();
uint8_t lcd_backlight_get();
void lcd_backlight_set(uint8_t brightness);
void lcd_backlight_on();
void lcd_backlight_off();
/* Queue the current active buffer for display at the next VBLANK and flip
 * the active index immediately (async). Do not cache the pointer from
 * lcd_get_active_buffer() across a swap — re-fetch after swap (get_active
 * waits out any pending reload before returning a writable back buffer). */
void lcd_swap(void);
void lcd_sync(void); // DEPRECATED
void lcd_clone(void);
/* Back buffer to draw into. Waits if a prior lcd_swap() has not yet landed
 * at VBLANK, so the returned pointer is never the live LTDC front buffer. */
void* lcd_get_active_buffer(void);
void* lcd_get_inactive_buffer(void);
void lcd_set_buffers(uint16_t *buf1, uint16_t *buf2);
void lcd_wait_for_vblank(void);
uint32_t lcd_is_swap_pending(void);
bool lcd_sleep_while_swap_pending(void);

// To be used by fault handlers
void lcd_reset_active_buffer(void);

uint32_t lcd_get_frame_counter(void);
uint32_t lcd_get_pixel_position();
void lcd_set_dithering(uint32_t enable);
void lcd_set_refresh_rate(uint32_t frequency);
uint32_t lcd_get_last_refresh_rate(void);

/* Reconfigure the LCD pool layout + LTDC pixel format for the requested mode.
 * Repoints framebuffer1/framebuffer2/fb1/fb2 to the new layout, switches the
 * LTDC peripheral's pixel format, and updates the live framebuffer address.
 * In LUT8 mode the upper 150 KiB of the pool becomes available — query
 * via lcd_get_bonus_pool().
 *
 * Safe to call at any time after lcd_init(). Callers should clear the
 * framebuffers afterward (mode change leaves stale pixels reinterpreted). */
void lcd_setup_framebuffers(lcd_mode_t mode);

/* Get the current bonus-pool region (memory in the LCD pool not occupied
 * by framebuffers, minus any prefix claimed by lcd_claim_bonus_pool()).
 * NULL/0 in RGB565 mode (the pool is fully used). In LUT8 mode, with
 * nothing claimed, this is 150 KiB of contiguous cacheable AXI SRAM
 * (__RAM_UC_CORE_START__). A loaded GNW_CORE_REGION_RAM_UC segment is
 * carved out of the front by the core loader. */
void lcd_get_bonus_pool(uint8_t **out_ptr, size_t *out_size);

/* Carve `nbytes` off the front of the LUT8 bonus (starting at
 * __RAM_UC_CORE_START__). Subsequent lcd_get_bonus_pool() calls return
 * the leftover. No-op in RGB565 or if nbytes is 0. Used by
 * run_dynamic_core() after memcpy'ing a RAM_UC segment so leftover heap
 * does not overlap loaded code+bss. Saturates at the bonus size.
 * Reset when leaving LUT8, or when entering LUT8 from RGB565. */
void lcd_claim_bonus_pool(size_t nbytes);

/* Program the LTDC's color lookup table (CLUT) used in LUT8 mode. The
 * `clut` array holds `count` 32-bit entries packed as 0x00RRGGBB (1..256).
 * Wraps HAL_LTDC_ConfigCLUT + HAL_LTDC_EnableCLUT for the layer. No-op
 * (returns harmlessly) if the LTDC isn't currently in L8 mode. Also
 * caches the entries internally so lcd_pack_color() can do nearest-match
 * lookups.
 *
 * When 2*count ≤ 256, slots [count..2*count) are filled with darkened
 * twins (LCD_DARKEN_BIT). A 256-entry cart palette fills the hardware
 * table; twins and overlay theme colors then share those 256 slots
 * (overlay at LCD_OVERLAY_CLUT_BASE overwrites cart[64..] while the
 * menu is up). */
void lcd_set_clut(const uint32_t *clut, uint16_t count);

/* Fixed-size snapshot of the active cart CLUT as RGB565, used by the
 * savestate-screenshot loader to convert a LUT8 preview to RGB565 when
 * the menu's framebuffer is in RGB565 mode.
 *
 * Writes exactly LCD_SCREENSHOT_CLUT_ENTRIES uint16_t entries into `out`:
 * cart slots [0..active_count) are filled with their RGB565 equivalent;
 * the remaining slots up to LCD_SCREENSHOT_CLUT_ENTRIES are zero-padded
 * so the on-disk size is constant regardless of cart palette size. */
#define LCD_SCREENSHOT_CLUT_ENTRIES  32
#define LCD_SCREENSHOT_CLUT_BYTES    (LCD_SCREENSHOT_CLUT_ENTRIES * 2)
void lcd_get_clut_rgb565(uint16_t *out);

/* Expand `count` LUT8 CLUT indices from `src` into RGB565 at `dst`.
 *
 * If `clut` is NULL, look up the live hardware CLUT cache (cart palette,
 * darkened twins, and overlay theme colors) — used by user BMP screenshots.
 *
 * If `clut` is non-NULL, it must point to LCD_SCREENSHOT_CLUT_ENTRIES RGB565
 * values as stored in savestate screenshot files; darkened twins are
 * reconstructed via LCD_DARKEN_PERCENT, and indices outside [0..64) become 0.
 * Used by the savestate-preview loader when converting a LUT8 .raw to RGB565. */
void lcd_convert_lut8_to_rgb565(const uint8_t *src, uint16_t *dst, size_t count,
                                const uint16_t *clut);

/* Reserved CLUT range for Retro-Go menu/overlay colors. Small palettes
 * (pico-8) use [0..32) + darkened twins at [32..64); we reserve
 * [64..64+MAX) for the active theme's colors past the twins.
 * A 256-colour cart fills the whole LTDC table: overlay slots must NOT
 * stay stamped over cart[64..] during gameplay (Doom PLAYPAL / NES).
 * Call lcd_overlay_clut_begin()/end() around pause/HUD chrome so theme
 * colours are applied only while that chrome is visible.
 * No darkened twins are stored for the menu — menu pixels are drawn
 * AFTER odroid_overlay_darken_all(), so they never need a "darkened
 * menu pixel" lookup.
 * MAX is theme (4) + HUD white + panel gray + empty-bar darker gray. */
#define LCD_OVERLAY_CLUT_BASE  0x40   /* index 64 */
#define LCD_OVERLAY_CLUT_MAX   7
#define LCD_OVERLAY_CLUT_WHITE 4      /* pure white — volume/brightness bars */
#define LCD_OVERLAY_CLUT_GRAY  5      /* ~25% gray — HUD panel fallback */
#define LCD_OVERLAY_CLUT_GRAY_DARK 6  /* ~12% gray — empty level boxes */

/* Register the overlay theme colors (RGB888 0x00RRGGBB). Saves them for
 * lcd_pack_color() exact-match and for begin/end. When overlay slots sit
 * past the cart/twins they are also pushed to the live CLUT; otherwise
 * only begin() stamps them (so 256-colour carts keep PLAYPAL intact).
 * Call whenever the user picks a different theme. */
void lcd_set_overlay_clut(const uint32_t *colors, uint16_t count);

/* Temporarily stamp saved overlay colours into the live CLUT (saving any
 * cart entries they overwrite). Nested begin/end pairs are refcounted.
 * Pair with lcd_overlay_clut_end() after pause/HUD drawing. */
void lcd_overlay_clut_begin(void);

/* Pop one begin(). When the last nest exits and overlay collided with the
 * cart, restore cart colours. Returns 1 if the cart CLUT was restored. */
int lcd_overlay_clut_end(void);

/* 1 if the next lcd_overlay_clut_end() will restore cart slots — callers
 * should clear chrome pixels first to avoid a one-frame colour flash. */
int lcd_overlay_clut_end_will_restore(void);

/* Drop every begin() nest and restore cart colours when they collided.
 * Use after pause exit (buffers already cleared). */
void lcd_overlay_clut_end_all(void);

/* Sum of R+G+B for a CLUT index (0..765). Overlay / out-of-range → 0.
 * Used by the in-game HUD to treat near-black game pixels like letterbox
 * (stamp solid gray — darken alone stays invisible on black). */
int lcd_clut_luma_sum(uint8_t idx);

/* Pack an RGB565 color value for the current LCD pixel format.
 *  - RGB565 mode: returns `rgb565` unchanged (write as uint16_t to fb).
 *  - LUT8 mode:   returns the nearest CLUT index in the low byte (write as
 *                 uint8_t to fb).
 *
 * The caller must query lcd_get_mode() and use the right framebuffer
 * stride: 1 byte/pixel in LUT8, 2 bytes/pixel in RGB565. */
uint16_t lcd_pack_color(uint16_t rgb565);

/* Returns the active LCD pixel format (LCD_MODE_RGB565 or LCD_MODE_LUT8).
 * Drawing code uses this to pick the right framebuffer cast/stride. */
int lcd_get_mode(void);

/* Returns the byte size of one framebuffer in the current mode.
 * 153600 (320x240x2) in RGB565, 76800 (320x240) in LUT8. Use this for
 * any runtime memset/memcpy/fread on a framebuffer — GW_LCD_FRAME_SIZE
 * is fixed at compile time (RGB565) and would overflow in LUT8. */
size_t lcd_get_frame_size(void);

/* Historical pico-8 shortcut: with count==32, twins live at [32..64) so
 * `idx | LCD_DARKEN_BIT` == `idx + count`. Prefer lcd_darken_index() /
 * lcd_darken_active_buffer() — they use +count (or an RGB nearest-match
 * fallback when a 256-colour cart left no twin slots). */
#define LCD_DARKEN_BIT  0x20  /* index +32: darkened twin when count==32 */

/* Darken percent applied to entries [count..2*count). 40 = 40% darker. */
#define LCD_DARKEN_PERCENT  40

/* 1 if lcd_set_clut() programmed darkened twins at [count..2*count). */
int lcd_clut_has_dark_twins(void);

/* Map one LUT8 index to its darkened form (twin slot, or nearest-match
 * of a darkened RGB when twins are unavailable). Index 0 stays 0 so
 * letterbox clears remain black. */
uint8_t lcd_darken_index(uint8_t idx);

/* Fullscreen darken of the active framebuffer in LUT8 mode. No-op in
 * RGB565 (callers keep using get_darken_pixel there). */
void lcd_darken_active_buffer(void);

/* ----------------------------------------------------------------------
 * Mode-agnostic overlay drawing helper ("pen").
 *
 * Background: the LCD can be in RGB565 (2 bytes/pixel) or LUT8 (1 byte/pixel,
 * indexed via CLUT). Overlay draw functions used to duplicate every fill
 * loop with `if (lcd_get_mode()==LCD_MODE_LUT8) {...} else {...}`.
 *
 * Usage: call `lcd_pen(rgb565)` once per draw call (resolves the buffer
 * pointer + the LUT8 index for the chosen color), then use `lcd_pen_set`
 * and `lcd_pen_run` to emit pixels — they branch on mode internally and
 * the compiler can hoist that check out of inner loops.
 * ---------------------------------------------------------------------- */
typedef struct {
    void    *fb;        /* lcd_get_active_buffer() at construction time */
    uint16_t rgb565;    /* original color, used in RGB565 mode */
    uint8_t  lut8_idx;  /* nearest CLUT index, used in LUT8 mode */
    uint8_t  is_lut8;   /* 1 if framebuffer is 1 byte/pixel LUT8 */
} lcd_pen_t;

static inline lcd_pen_t lcd_pen(uint16_t color)
{
    lcd_pen_t p;
    p.fb       = lcd_get_active_buffer();
    p.rgb565   = color;
    p.is_lut8  = (lcd_get_mode() == LCD_MODE_LUT8) ? 1 : 0;
    p.lut8_idx = p.is_lut8 ? (uint8_t)lcd_pack_color(color) : 0;
    return p;
}

/* Write a single pixel at framebuffer offset `off` (in pixels, not bytes). */
static inline void lcd_pen_set(const lcd_pen_t *p, int off)
{
    if (p->is_lut8) ((uint8_t  *)p->fb)[off] = p->lut8_idx;
    else            ((uint16_t *)p->fb)[off] = p->rgb565;
}

/* Fill `count` consecutive pixels starting at offset `off`. */
static inline void lcd_pen_run(const lcd_pen_t *p, int off, int count)
{
    if (p->is_lut8) {
        uint8_t *q = (uint8_t *)p->fb + off;
        for (int i = 0; i < count; i++) q[i] = p->lut8_idx;
    } else {
        uint16_t *q = (uint16_t *)p->fb + off;
        for (int i = 0; i < count; i++) q[i] = p->rgb565;
    }
}

/* Darken a single pixel.
 *
 * RGB565 path: halves each channel and adds a small constant — calling it
 * twice on the same pixel further darkens (asymptote ~ #2104 dark grey).
 *
 * LUT8 path: lcd_darken_index() — twin at idx+count when available, else
 * nearest darkened RGB. A second darken on an already-darkened twin drops
 * to index 0 (same “third tier” behaviour as the old DARKEN_BIT path). */
static inline void lcd_pen_darken(const lcd_pen_t *p, int off)
{
    if (p->is_lut8) {
        uint8_t *q = &((uint8_t *)p->fb)[off];
        *q = lcd_darken_index(*q);
    } else {
        uint16_t *q = &((uint16_t *)p->fb)[off];
        *q = (uint16_t)(((*q >> 1) & 0x7BEF) + 0x2104);
    }
}

#endif
