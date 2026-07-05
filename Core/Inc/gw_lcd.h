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
 * GW_LCD_FRAME_SIZE within the pool. Phase 2 will allow per-emulator
 * runtime relocation so LUT8 emulators can reclaim unused half as bonus. */
extern pixel_t *framebuffer1;
extern pixel_t *framebuffer2;

typedef enum
{
   LCD_INIT_CLEAR_BUFFERS = 1 << 0
} lcd_init_flags_t;

/* LCD pixel format. Per-emulator switchable at runtime via
 * lcd_setup_framebuffers(). RGB565 is the default for most emulators
 * (15-bit-equivalent direct color, 2 framebuffers x 154K = 300K).
 * LUT8 is an 8-bit indexed palette mode (2 framebuffers x 77K = 154K),
 * freeing the upper 154K of the LCD pool as overflow memory. The CLUT
 * (256 RGB888 entries) is programmed via lcd_set_clut(). */
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
void lcd_swap(void);
void lcd_sync(void); // DEPRECATED
void lcd_clone(void);
void* lcd_get_active_buffer(void);
void* lcd_get_inactive_buffer(void);
void lcd_set_buffers(uint16_t *buf1, uint16_t *buf2);
void lcd_present_at_vblank(void *addr);
void *lcd_get_displayed_buffer(void);
void lcd_wait_for_vblank(void);
uint32_t lcd_is_swap_pending(void);
uint32_t lcd_is_tb_mode(void);
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
 * In LUT8 mode the upper 154K of the pool becomes available — query via
 * lcd_get_bonus_pool().
 *
 * Safe to call at any time after lcd_init(). Callers should clear the
 * framebuffers afterward (mode change leaves stale pixels reinterpreted). */
void lcd_setup_framebuffers(lcd_mode_t mode);

/* Get the current bonus-pool region (memory in the LCD pool not occupied
 * by framebuffers). NULL/0 in RGB565 mode (the pool is fully used). In LUT8
 * mode this is 154K of contiguous AXI SRAM ready to be claimed by the
 * caller (typically merged into an emulator's heap as overflow). */
void lcd_get_bonus_pool(uint8_t **out_ptr, size_t *out_size);

/* Program the LTDC's color lookup table (CLUT) used in LUT8 mode. The
 * `clut` array holds `count` 32-bit entries packed as 0x00RRGGBB. Wraps
 * HAL_LTDC_ConfigCLUT + HAL_LTDC_EnableCLUT for the layer. No-op (returns
 * harmlessly) if the LTDC isn't currently in L8 mode. Also caches the
 * entries internally so lcd_pack_color() can do nearest-match lookups. */
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

/* Reserved CLUT range for Retro-Go menu/overlay colors. The cart palette
 * uses [0..32) + darkened twins at [32..64); we reserve [64..64+MAX) for
 * the active theme's colors. No darkened twins are stored for the menu —
 * menu pixels are drawn AFTER odroid_overlay_darken_all(), so they never
 * need a "darkened menu pixel" lookup. (Side effect: lcd_pen_darken on
 * an already-drawn menu pixel falls back to black via the +0x20 OR
 * landing on an unprogrammed slot — acceptable for the rare case.)
 * MAX matches colors_t's 4 fields; bump if more menu colors get added. */
#define LCD_OVERLAY_CLUT_BASE  0x40   /* index 64 */
#define LCD_OVERLAY_CLUT_MAX   4

/* Register the overlay theme colors (RGB888 0x00RRGGBB). Re-programs the
 * hardware CLUT preserving the cart palette. lcd_pack_color() will then
 * exact-match the registered colors instead of nearest-matching them
 * against the cart palette. Call whenever the user picks a different
 * theme. No-op when not in LUT8 mode. */
void lcd_set_overlay_clut(const uint32_t *colors, uint16_t count);

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

/* Bit set on a CLUT index byte to map it to its precomputed darkened
 * twin. lcd_set_clut() automatically programs CLUT slots [count..2*count)
 * with darkened RGB versions of slots [0..count), so a fullscreen darken
 * is simply `fb[i] |= LCD_DARKEN_BIT;` — no per-pixel approximation, the
 * LTDC CLUT does the lookup at scanout. */
#define LCD_DARKEN_BIT  0x20  /* index +32: darkened twin slot */

/* Darken percent applied to entries [count..2*count). 40 = 40% darker. */
#define LCD_DARKEN_PERCENT  40

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

/* Pen targeting an explicit framebuffer — REQUIRED for cores that manage
 * their own buffers (EarthBound's triple-buffer rotation): there the
 * launcher's active-buffer pointer is frozen/stale, and drawing through it
 * lands on an arbitrary rotation buffer (the volume/brightness overlay
 * flicker bug). Pixel format still follows the LCD mode. */
static inline lcd_pen_t lcd_pen_for(void *fb, uint16_t color)
{
    lcd_pen_t p;
    p.fb       = fb;
    p.rgb565   = color;
    p.is_lut8  = (lcd_get_mode() == LCD_MODE_LUT8) ? 1 : 0;
    p.lut8_idx = p.is_lut8 ? (uint8_t)lcd_pack_color(color) : 0;
    return p;
}

static inline lcd_pen_t lcd_pen(uint16_t color)
{
    return lcd_pen_for(lcd_get_active_buffer(), color);
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
 * LUT8 path: ORs LCD_DARKEN_BIT to hit the precomputed darkened twin in
 * the CLUT. A naive OR is idempotent, which makes overlays that rely on
 * "darken-of-darken" as a third tier (e.g. volume/brightness empty-cell
 * boxes drawn over an already-darkened rounded background) collapse into
 * the background. Match the RGB565 behavior by detecting the
 * already-darkened case and dropping to CLUT index 0 (black on the
 * active palette) so the second darken is visibly deeper. */
static inline void lcd_pen_darken(const lcd_pen_t *p, int off)
{
    if (p->is_lut8) {
        uint8_t *q = &((uint8_t *)p->fb)[off];
        if (*q & LCD_DARKEN_BIT) *q = 0;
        else                     *q |= LCD_DARKEN_BIT;
    } else {
        uint16_t *q = &((uint16_t *)p->fb)[off];
        *q = (uint16_t)(((*q >> 1) & 0x7BEF) + 0x2104);
    }
}

#endif
