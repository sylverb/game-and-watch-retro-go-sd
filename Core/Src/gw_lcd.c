#include <stdio.h>
#include <string.h>

#include "gw_lcd.h"
#include "stm32h7xx_hal.h"
#include "main.h"

/* Linker-defined .lcd_pool region (RAM_UC, 300K). Sized for the largest
 * supported mode; the C side carves it into framebuffer1/framebuffer2 here.
 * Declared as `uint8_t[]` so pointer-arithmetic on it is byte-stride.
 * Initialisers below are address constants resolved at link time, so no
 * boot-order issue — pointers are valid from the first instruction. */
extern uint8_t __lcd_pool_start__[];
extern uint8_t __lcd_pool_end__[];

pixel_t *framebuffer1 = (pixel_t *)__lcd_pool_start__;
pixel_t *framebuffer2 = (pixel_t *)(__lcd_pool_start__ + GW_LCD_FRAME_SIZE);

/* LTDC-side framebuffer pointers — same as framebuffer1/2 by default but
 * lcd_set_buffers() lets callers redirect the hardware to alternative
 * buffers (used by the welcome/error screen flows). Initialised from the
 * linker symbol so they're valid before lcd_init runs. */
uint16_t *fb1 = (uint16_t *)__lcd_pool_start__;
uint16_t *fb2 = (uint16_t *)(__lcd_pool_start__ + GW_LCD_FRAME_SIZE);

extern LTDC_HandleTypeDef hltdc;

#ifdef HAL_DAC_MODULE_ENABLED
extern DAC_HandleTypeDef hdac1;
extern DAC_HandleTypeDef hdac2;
#endif

uint32_t active_framebuffer;
uint32_t frame_counter;
uint32_t last_frequency = 60;

/* Staged CLUT flush: HAL_LTDC_ReloadEventCallback applies it at vblank.
 * Definition lives with the CLUT helpers further down. */
static uint8_t clut_hw_dirty;
static void clut_hw_flush(void);

#define HAL_DAC_ENABLED(__HANDLE__, __DAC_Channel__) \
  (((__HANDLE__)->Instance->CR & (DAC_CR_EN1 << ((__DAC_Channel__) & 0x10UL))))

uint8_t lcd_backlight_get()
{
#ifdef HAL_DAC_MODULE_ENABLED
  if (!HAL_DAC_ENABLED(&hdac1, DAC_CHANNEL_1)) {
    return 0;
  }

  // Convert 12-bit to 8-bit
  return HAL_DAC_GetValue(&hdac1, DAC_CHANNEL_1) >> 4;
#else
  return HAL_GPIO_ReadPin(GPIO_LCD_BRIGHTNESS_1_GPIO_Port, GPIO_LCD_BRIGHTNESS_1_Pin) == GPIO_PIN_SET ? 255 : 0;
#endif
}

void lcd_backlight_off()
{
  HAL_DAC_Stop(&hdac1, DAC_CHANNEL_1);
  HAL_DAC_Stop(&hdac1, DAC_CHANNEL_2);
  HAL_DAC_Stop(&hdac2, DAC_CHANNEL_1);
}

void lcd_backlight_set(uint8_t brightness)
{
  HAL_DAC_SetValue(&hdac1, DAC_CHANNEL_1, DAC_ALIGN_8B_R, brightness);
  HAL_DAC_SetValue(&hdac1, DAC_CHANNEL_2, DAC_ALIGN_8B_R, brightness);
  HAL_DAC_SetValue(&hdac2, DAC_CHANNEL_1, DAC_ALIGN_8B_R, brightness);

  HAL_DAC_Start(&hdac1, DAC_CHANNEL_1);
  HAL_DAC_Start(&hdac1, DAC_CHANNEL_2);
  HAL_DAC_Start(&hdac2, DAC_CHANNEL_1);
}

void lcd_backlight_on()
{
  lcd_backlight_set(255);
}

static void gw_set_power_1V8(uint32_t p) {
  HAL_GPIO_WritePin(GPIO_POWER_1V8_GPIO_Port, GPIO_POWER_1V8_Pin, p == 0 ? GPIO_PIN_SET : GPIO_PIN_RESET);
}
static void gw_set_power_3V3(uint32_t p) {
  HAL_GPIO_WritePin(GPIO_POWER_3V3_GPIO_Port, GPIO_POWER_3V3_Pin, p == 1 ? GPIO_PIN_SET : GPIO_PIN_RESET);
}
static void gw_lcd_set_chipselect(uint32_t p) {
  HAL_GPIO_WritePin(GPIO_LCD_CS_GPIO_Port, GPIO_LCD_CS_Pin, p == 0 ? GPIO_PIN_SET : GPIO_PIN_RESET);
}
static void gw_lcd_set_reset(uint32_t p) {
  HAL_GPIO_WritePin(GPIO_LCD_RESET_GPIO_Port, GPIO_LCD_RESET_Pin, p == 0 ? GPIO_PIN_SET : GPIO_PIN_RESET);
}

static void gw_lcd_spi_tx(SPI_HandleTypeDef *spi, uint8_t *pData) {
  gw_lcd_set_chipselect(1);
  HAL_Delay(2);
  HAL_SPI_Transmit(spi, pData, 2, 100);
  HAL_Delay(2);
  wdog_refresh();
  gw_lcd_set_chipselect(0);
  HAL_Delay(2);
}

void lcd_deinit(SPI_HandleTypeDef *spi) {
  __HAL_LTDC_DISABLE_IT(&hltdc, LTDC_IT_LI | LTDC_IT_RR);
  /* Stop scanning out before cutting panel rails — otherwise the glass
   * sees a live RGB/CLK bus while VDD collapses, and the next SPI bring-up
   * can leave it in a stuck state (FB+LTDC OK, screen garbage). */
  __HAL_LTDC_DISABLE(&hltdc);

  // Power off
  gw_set_power_1V8(0);
  gw_set_power_3V3(0);
}

void *lcd_clear_active_buffer() {
  void *buffer = lcd_get_active_buffer();
  memset(buffer, 0, lcd_get_frame_size());
  return buffer;
}

void *lcd_clear_inactive_buffer() {
  void *buffer = lcd_get_inactive_buffer();
  memset(buffer, 0, lcd_get_frame_size());
  return buffer;
}

void lcd_clear_buffers() {
  size_t fs = lcd_get_frame_size();
  memset(framebuffer1, 0, fs);
  memset(framebuffer2, 0, fs);
}

void lcd_init(SPI_HandleTypeDef *spi, LTDC_HandleTypeDef *ltdc, lcd_init_flags_t flags) {
  // Disable LCD Chip select
  gw_lcd_set_chipselect(0);

  /* MX_LTDC_Init() already set LTDEN. Keep the RGB bus quiet while the
   * panel powers up and takes its SPI config — a live pixel clock during
   * that window is a plausible cause of the intermittent "FB dump OK /
   * screen scrambled" boot glitch (survives sleep because wake re-runs
   * the same race; only a full reboot eventually wins). */
  __HAL_LTDC_DISABLE(ltdc);

  // LCD held out of reset while rails come up
  gw_lcd_set_reset(0);

  // Enable 3V3 then 1V8 with settle time (restored after 6a7f9f99 which
  // dropped these delays and made panel bring-up racy).
  gw_set_power_3V3(1);
  HAL_Delay(2);
  gw_set_power_1V8(1);
  HAL_Delay(50);
  wdog_refresh();

  /* reset sequence */
  gw_lcd_set_reset(0);
  HAL_Delay(1);
  gw_lcd_set_reset(1);
  HAL_Delay(20);
  gw_lcd_set_reset(0);
  HAL_Delay(50);
  wdog_refresh();

  gw_lcd_spi_tx(spi, (uint8_t *)"\x08\x80");
  gw_lcd_spi_tx(spi, (uint8_t *)"\x6E\x80");
  gw_lcd_spi_tx(spi, (uint8_t *)"\x80\x80");

  gw_lcd_spi_tx(spi, (uint8_t *)"\x68\x00");
  gw_lcd_spi_tx(spi, (uint8_t *)"\xd0\x00");
  gw_lcd_spi_tx(spi, (uint8_t *)"\x1b\x00");
  gw_lcd_spi_tx(spi, (uint8_t *)"\xe0\x00");

  gw_lcd_spi_tx(spi, (uint8_t *)"\x6a\x80");
  gw_lcd_spi_tx(spi, (uint8_t *)"\x80\x00");
  gw_lcd_spi_tx(spi, (uint8_t *)"\x14\x80");
  wdog_refresh();

  if (flags & LCD_INIT_CLEAR_BUFFERS) {
    lcd_clear_buffers();
  }

  HAL_LTDC_SetAddress(ltdc, (uint32_t)fb1, 0);
  HAL_LTDC_ProgramLineEvent(&hltdc, 239);
  __HAL_LTDC_ENABLE_IT(&hltdc, LTDC_IT_LI | LTDC_IT_RR);
  __HAL_LTDC_ENABLE(ltdc);

  printf("LCD: Finished init\n");
}

void HAL_LTDC_ReloadEventCallback (LTDC_HandleTypeDef *hltdc) {
  /* Address is already in the shadow CFBAR from lcd_swap()'s NoReload
   * SetAddress — this interrupt means that shadow just became live, at
   * the start of vblank. Applying CLUT here (not during active scan)
   * keeps palette updates off the visible raster. */
  (void)hltdc;
  if (clut_hw_dirty)
    clut_hw_flush();
}

void HAL_LTDC_LineEventCallback (LTDC_HandleTypeDef *hltdc) {
  frame_counter++;
  HAL_LTDC_ProgramLineEvent(hltdc,  239);
}

uint32_t lcd_is_swap_pending(void)
{
  return (uint32_t) ((hltdc.Instance->SRCR) & (LTDC_SRCR_VBR | LTDC_SRCR_IMR));
}

bool lcd_sleep_while_swap_pending(void)
{
  /* VBLANK reload normally clears SRCR within one frame (~16 ms). After a
   * PLL rewrite (SystemClock_Config) LTDC can leave VBR/IMR stuck: an
   * unbounded __WFI() loop freezes the UI (e.g. "Caching game" at 0%) and
   * starves the window watchdog. Cap the wait and keep kicking WWDG. */
  const uint32_t timeout_ms = 100;
  uint32_t start = HAL_GetTick();
  uint32_t pending = false;

  while (lcd_is_swap_pending())
  {
    pending = true;
    wdog_refresh();
    if ((HAL_GetTick() - start) >= timeout_ms)
      break;
    __WFI();
  }

  return pending;
}

uint32_t lcd_get_pixel_position()
{
  return (uint32_t)(hltdc.Instance->CPSR);
}

void lcd_swap(void)
{
  /* Program the just-drawn buffer into the shadow CFBAR, then reload at
   * vblank. HAL_LTDC_SetAddress() would SRCR-IMR from the reload ISR a
   * few lines into the next frame (horizontal bar at the top of the
   * panel). NoReload + VBR applies at the actual start of blanking.
   *
   * Flip active_framebuffer immediately so the next draw targets the
   * other buffer, but that buffer is still scanned by LTDC until VBR
   * clears — lcd_get_active_buffer() waits out the pending reload so
   * callers never paint into the live front buffer. Emulation can still
   * run between swap and the next get_active (async swap preserved). */
  HAL_LTDC_SetAddress_NoReload(&hltdc, (uint32_t)lcd_get_active_buffer(), 0);
  HAL_LTDC_Reload(&hltdc, LTDC_RELOAD_VERTICAL_BLANKING);
  active_framebuffer = active_framebuffer ? 0 : 1;
}

void lcd_sync(void)
{
  void *active = lcd_get_active_buffer();
  void *inactive = lcd_get_inactive_buffer();

  if (active != inactive) {
    memcpy(inactive, active, lcd_get_frame_size());
  }
}

void lcd_clone(void)
{
  void *active = lcd_get_active_buffer();
  void *inactive = lcd_get_inactive_buffer();

  if (active != inactive) {
    memcpy(active, inactive, lcd_get_frame_size());
  }
}

void* lcd_get_active_buffer(void)
{
  return active_framebuffer ? framebuffer2 : framebuffer1;
}

void* lcd_get_inactive_buffer(void)
{
  return active_framebuffer ? framebuffer1 : framebuffer2;
}

void lcd_reset_active_buffer(void)
{
  HAL_LTDC_SetAddress(&hltdc, (uint32_t) fb1, 0);
  active_framebuffer = 0;
}

void lcd_set_buffers(uint16_t *buf1, uint16_t *buf2)
{
  fb1 = buf1;
  fb2 = buf2;
}

/* ----- LCD pool layout / mode switching -----
 *
 * RGB565 mode: 2 framebuffers x 150 KiB = 300 KiB, fills the pool, no bonus.
 * LUT8 mode:   2 framebuffers x  75 KiB = 150 KiB, leaves 150 KiB of bonus
 *              at __RAM_UC_CORE_START__ (cacheable; see
 *              mpu_set_lcd_pool_uncached_range).
 *
 * `current_lcd_mode` tracks the active layout so lcd_get_bonus_pool reports
 * the right region. Initialised to RGB565 to match the static-init layout
 * of framebuffer1/2 + fb1/2 set up at the top of this file.
 * `bonus_claimed` is a prefix of that LUT8 leftover reserved by the core
 * loader (GNW_CORE_REGION_RAM_UC); lcd_get_bonus_pool skips it. */
static lcd_mode_t current_lcd_mode = LCD_MODE_RGB565;
static size_t bonus_claimed;

static size_t lcd_bonus_total(void)
{
  size_t pool = (size_t)((uintptr_t)__lcd_pool_end__ - (uintptr_t)__lcd_pool_start__);
  size_t fb_block = 2 * (size_t)(GW_LCD_WIDTH * GW_LCD_HEIGHT);
  return (pool > fb_block) ? (pool - fb_block) : 0;
}

static void clut_apply_saved_overlay(void);
static void clut_rebuild_darken_map(void);
static void clut_push(void); /* defined with CLUT helpers below */

void lcd_setup_framebuffers(lcd_mode_t mode)
{
  uint8_t *base = __lcd_pool_start__;
  size_t fb_size_bytes;
  uint32_t pixel_format;

  if (mode == LCD_MODE_LUT8) {
    fb_size_bytes = (size_t)(GW_LCD_WIDTH * GW_LCD_HEIGHT * 1);  /* 1 byte/px */
    pixel_format  = LTDC_PIXEL_FORMAT_L8;
  } else {
    fb_size_bytes = (size_t)(GW_LCD_WIDTH * GW_LCD_HEIGHT * 2);  /* RGB565 */
    pixel_format  = LTDC_PIXEL_FORMAT_RGB565;
  }

  /* Clear framebuffer regions BEFORE the format change. Until
   * HAL_LTDC_Reload() takes effect at the next vsync the LTDC keeps
   * reading the old data; stale RGB565 bytes reinterpreted as LUT8
   * indices flash garbage. Zero is black in both RGB565 and CLUT idx 0.
   * The union of the two modes' footprints is the whole 300 KiB pool
   * whenever RGB565 is involved (so entering/leaving LUT8 also wipes
   * the bonus). LUT8→LUT8 only zeros the 150 KiB FB pair, preserving a
   * loaded RAM_UC segment and leftover heap. */
  {
    size_t old_fb = (current_lcd_mode == LCD_MODE_LUT8)
        ? (size_t)(GW_LCD_WIDTH * GW_LCD_HEIGHT * 1)
        : (size_t)(GW_LCD_WIDTH * GW_LCD_HEIGHT * 2);
    size_t footprint = 2 * (fb_size_bytes > old_fb ? fb_size_bytes : old_fb);
    memset(base, 0, footprint);
  }

  /* Repoint pointers. pixel_t is the build-time default; in LUT8 mode the
   * caller works with the framebuffer as uint8_t* via cast. fb1/fb2 are
   * what the LTDC peripheral reads — must match the framebuffer layout. */
  framebuffer1 = (pixel_t *)(base);
  framebuffer2 = (pixel_t *)(base + fb_size_bytes);
  fb1 = (uint16_t *)(base);
  fb2 = (uint16_t *)(base + fb_size_bytes);

  /* Fresh bonus when leaving LUT8, or when entering it from RGB565.
   * LUT8→LUT8 keeps bonus_claimed so a core re-setup does not un-hide
   * a loaded RAM_UC prefix. */
  if (mode != LCD_MODE_LUT8 || current_lcd_mode != LCD_MODE_LUT8)
    bonus_claimed = 0;

  current_lcd_mode = mode;
  active_framebuffer = 0;

  /* Display the empty back buffer so the first present can fill framebuffer1
   * without tearing. lcd_swap() will point LTDC at the just-drawn buffer. */
  HAL_LTDC_SetPixelFormat(&hltdc, pixel_format, 0);
  HAL_LTDC_SetAddress_NoReload(&hltdc, (uint32_t)framebuffer2, 0);
  if (mode == LCD_MODE_LUT8) {
    /* Theme colours were stored while the launcher was still RGB565 —
     * stamp them into the live CLUT now so the first core frame can
     * lcd_pack_color() the in-game HUD exactly. */
    clut_apply_saved_overlay();
    clut_rebuild_darken_map();
    clut_push();
    HAL_LTDC_EnableCLUT_NoReload(&hltdc, 0);
  }
  HAL_LTDC_Reload(&hltdc, LTDC_RELOAD_VERTICAL_BLANKING);

  /* MPU follow-up: re-cover the LCD pool so only the active framebuffer
   * region stays uncached. In LUT8 mode the entire 150 KiB bonus
   * (__RAM_UC_CORE_START__) becomes cacheable Normal memory, which is
   * required if a core segment or leftover heap lives there. The
   * framebuffer footprint must stay uncached so LTDC sees CPU writes
   * immediately. */
  uint32_t fb_footprint = (mode == LCD_MODE_LUT8)
      ? (uint32_t)(2 * GW_LCD_WIDTH * GW_LCD_HEIGHT)        /* 150 KiB */
      : (uint32_t)(2 * GW_LCD_WIDTH * GW_LCD_HEIGHT * 2);   /* 300 KiB */
  SCB_CleanInvalidateDCache_by_Addr(
      (uint32_t *)base,
      (int32_t)((uintptr_t)__lcd_pool_end__ - (uintptr_t)base));
  HAL_MPU_Disable();
  mpu_set_lcd_pool_uncached_range(fb_footprint);
  HAL_MPU_Enable(MPU_HFNMI_PRIVDEF);
  __DSB();
  __ISB();

  lcd_sleep_while_swap_pending();
}

void lcd_get_bonus_pool(uint8_t **out_ptr, size_t *out_size)
{
  if (current_lcd_mode == LCD_MODE_LUT8) {
    size_t fb_block = 2 * (size_t)(GW_LCD_WIDTH * GW_LCD_HEIGHT);
    size_t total = lcd_bonus_total();
    size_t claimed = (bonus_claimed < total) ? bonus_claimed : total;
    if (out_ptr)  *out_ptr  = __lcd_pool_start__ + fb_block + claimed;
    if (out_size) *out_size = total - claimed;
  } else {
    if (out_ptr)  *out_ptr  = NULL;
    if (out_size) *out_size = 0;
  }
}

void lcd_claim_bonus_pool(size_t nbytes)
{
  if (current_lcd_mode != LCD_MODE_LUT8 || nbytes == 0)
    return;
  size_t total = lcd_bonus_total();
  if (bonus_claimed >= total)
    return;
  size_t room = total - bonus_claimed;
  bonus_claimed += (nbytes < room) ? nbytes : room;
}

/* Active CLUT staged for HAL_LTDC_ConfigCLUT and reused by lcd_pack_color()
 * for nearest-match RGB→index lookups.
 *
 * The LTDC layer CLUT is 256 slots. Two layouts share that table:
 *
 *   Small palettes (count ≤ 128, pico-8 = 32):
 *     [0..count)         cart palette
 *     [count..2*count)   darkened twins (LCD_DARKEN_BIT = index+count)
 *     [64..64+OMAX)      Retro-Go overlay theme (LCD_OVERLAY_CLUT_BASE)
 *                        when 2*count ≤ 64 this sits past the twins;
 *                        otherwise it overlaps cart/twin slots and the
 *                        overlay rewrite wins while the menu is up
 *
 *   Full 256-colour palettes (NES, Doom PLAYPAL):
 *     [0..256)           cart palette; no room for darkened twins
 *     overlay at 0x40 collides with cart[64..] — do NOT keep it stamped
 *     after lcd_set_clut(); lcd_overlay_clut_begin()/end() apply it only
 *     while pause/HUD chrome is drawn, then restore the cart slots
 *
 * No darkened-twin slots for the overlay — see gw_lcd.h for rationale.
 * Twins are selected with idx+count (see lcd_darken_index). The old
 * LCD_DARKEN_BIT (0x20) OR only matches that when count==32; on a
 * 256-colour cart there are no twins and that OR picked another cart
 * colour (NES letterbox 0 → $20 white). */
#define LCD_CLUT_HW_MAX  256
static uint32_t active_clut[LCD_CLUT_HW_MAX];
static uint16_t active_clut_count   = 0;   /* cart entries in [0..count) */
static uint16_t overlay_clut_count  = 0;   /* overlay entries in [BASE..BASE+count) */
static uint8_t  clut_dark_twins     = 0;   /* twins stored at [count..2*count) */
/* Survives lcd_set_clut() — used by lcd_pack_color + begin/end. */
static uint32_t overlay_clut_saved[LCD_OVERLAY_CLUT_MAX];
/* Cart RGB under the overlay window, saved when begin() stamps over them. */
static uint32_t cart_clut_under_overlay[LCD_OVERLAY_CLUT_MAX];
static uint8_t  overlay_clut_live = 0;     /* 1 if overlay colours are in active_clut */
static uint8_t  overlay_clut_depth = 0;    /* nested begin()/end() for pause dialogs */
/* O(1) darken for lcd_darken_index / HUD (rebuilt on set_clut). */
static uint8_t  clut_darken_map[LCD_CLUT_HW_MAX];
static uint8_t  clut_darken_map_valid = 0;

static void clut_rebuild_darken_map(void); /* defined after clut_nearest_cart */

static int clut_cart_span(void)
{
  if (clut_dark_twins)
    return (int)(2u * active_clut_count);
  return (int)active_clut_count;
}

/* Overlay window sits inside cart (or twin) slots — stamping permanently
 * would corrupt PLAYPAL/CGRAM. */
static int overlay_collides_with_cart(void)
{
  if (overlay_clut_count == 0)
    return 0;
  return (LCD_OVERLAY_CLUT_BASE + (int)overlay_clut_count) <= clut_cart_span();
}

static void clut_cache_cart_under_overlay(void)
{
  if (!overlay_collides_with_cart())
    return;
  for (uint16_t i = 0; i < overlay_clut_count; i++) {
    uint16_t idx = (uint16_t)(LCD_OVERLAY_CLUT_BASE + i);
    if (idx < active_clut_count)
      cart_clut_under_overlay[i] = active_clut[idx];
  }
}

static void clut_apply_saved_overlay(void)
{
  if (overlay_clut_count == 0)
    return;
  if (!overlay_clut_live)
    clut_cache_cart_under_overlay();
  for (uint16_t i = 0; i < overlay_clut_count; i++)
    active_clut[LCD_OVERLAY_CLUT_BASE + i] = overlay_clut_saved[i];
  overlay_clut_live = 1;
}

static void clut_restore_cart_under_overlay(void)
{
  if (!overlay_clut_live || !overlay_collides_with_cart())
    return;
  for (uint16_t i = 0; i < overlay_clut_count; i++) {
    uint16_t idx = (uint16_t)(LCD_OVERLAY_CLUT_BASE + i);
    if (idx < active_clut_count)
      active_clut[idx] = cart_clut_under_overlay[i];
  }
  overlay_clut_live = 0;
}

static int clut_hw_total(void)
{
  int total = clut_cart_span();
  if (overlay_clut_count > 0 &&
      (overlay_clut_live || !overlay_collides_with_cart())) {
    int overlay_end = LCD_OVERLAY_CLUT_BASE + overlay_clut_count;
    if (overlay_end > total) total = overlay_end;
  }
  if (total > LCD_CLUT_HW_MAX) total = LCD_CLUT_HW_MAX;
  return total;
}

/* Write the staged CLUT into LTDC RAM. Called from the vblank reload ISR
 * so the 256 CLUTWR stores never land on a visible scanline (that showed
 * up as a horizontal bar sweeping the panel, frozen at the top in pause). */
static void clut_hw_flush(void)
{
  int total = clut_hw_total();
  clut_hw_dirty = 0;
  if (total <= 0) return;
  HAL_LTDC_ConfigCLUT(&hltdc, active_clut, (uint32_t)total, 0);
  HAL_LTDC_EnableCLUT_NoReload(&hltdc, 0);
}

/* Stage a hardware update for the next lcd_swap() vblank. Do not write
 * CLUTWR here — that is live scanout memory. */
static void clut_push(void)
{
  if (clut_hw_total() <= 0) return;
  clut_hw_dirty = 1;
}

/* Compute and store a 40%-darkened twin of `e` (RGB888) at slot `idx`. */
static void clut_store_dark_twin(int idx, uint32_t e)
{
  int keep = 100 - LCD_DARKEN_PERCENT;
  uint32_t r, g, b;
  if (idx < 0 || idx >= LCD_CLUT_HW_MAX) return;
  r = (((e >> 16) & 0xFF) * keep) / 100;
  g = (((e >>  8) & 0xFF) * keep) / 100;
  b = (((e      ) & 0xFF) * keep) / 100;
  active_clut[idx] = (r << 16) | (g << 8) | b;
}

static uint16_t rgb888_entry_to_rgb565(uint32_t e)
{
  uint8_t r = (uint8_t)((e >> 16) & 0xFF);
  uint8_t g = (uint8_t)((e >>  8) & 0xFF);
  uint8_t b = (uint8_t)((e      ) & 0xFF);
  return (uint16_t)(((r >> 3) << 11) | ((g >> 2) << 5) | (b >> 3));
}

/* Darken an RGB565 color by LCD_DARKEN_PERCENT — mirrors clut_store_dark_twin
 * so embedded cart CLUTs can reconstruct the [32..64) darkened-twin range. */
static uint16_t darken_rgb565(uint16_t c)
{
  const int keep = 100 - LCD_DARKEN_PERCENT;
  int r = (c >> 11) & 0x1F;
  int g = (c >>  5) & 0x3F;
  int b = (c      ) & 0x1F;
  r = (r * keep) / 100;
  g = (g * keep) / 100;
  b = (b * keep) / 100;
  return (uint16_t)((r << 11) | (g << 5) | b);
}

void lcd_get_clut_rgb565(uint16_t *out)
{
  if (out == NULL) return;
  uint16_t n = active_clut_count;
  if (n > LCD_SCREENSHOT_CLUT_ENTRIES) n = LCD_SCREENSHOT_CLUT_ENTRIES;
  for (uint16_t i = 0; i < n; i++) {
    out[i] = rgb888_entry_to_rgb565(active_clut[i]);
  }
  for (uint16_t i = n; i < LCD_SCREENSHOT_CLUT_ENTRIES; i++) out[i] = 0;
}

void lcd_convert_lut8_to_rgb565(const uint8_t *src, uint16_t *dst, size_t count,
                                const uint16_t *clut)
{
  if (src == NULL || dst == NULL) return;

  for (size_t i = 0; i < count; i++) {
    uint8_t idx = src[i];
    if (clut != NULL) {
      if (idx < LCD_SCREENSHOT_CLUT_ENTRIES) {
        dst[i] = clut[idx];
      } else if (idx < 2 * LCD_SCREENSHOT_CLUT_ENTRIES) {
        dst[i] = darken_rgb565(clut[idx - LCD_SCREENSHOT_CLUT_ENTRIES]);
      } else {
        dst[i] = 0;
      }
    } else if (idx < LCD_CLUT_HW_MAX) {
      dst[i] = rgb888_entry_to_rgb565(active_clut[idx]);
    } else {
      dst[i] = 0;
    }
  }
}

void lcd_set_clut(const uint32_t *clut, uint16_t count)
{
  if (current_lcd_mode != LCD_MODE_LUT8 || clut == NULL || count == 0) return;
  if (count > LCD_CLUT_HW_MAX) count = LCD_CLUT_HW_MAX;

  for (uint16_t i = 0; i < count; i++)
    active_clut[i] = clut[i];

  /* Darkened twins at [count..2*count) only when they fit in the 256-slot
   * hardware table. Pico-8 (count=32) still gets [32..64). A 256-colour
   * cart fills the table and has no twin slots. */
  clut_dark_twins = ((uint32_t)count * 2u) <= (uint32_t)LCD_CLUT_HW_MAX;
  if (clut_dark_twins) {
    for (uint16_t i = 0; i < count; i++)
      clut_store_dark_twin(count + i, clut[i]);
  }
  active_clut_count = count;
  overlay_clut_live = 0;
  overlay_clut_depth = 0;

  /* Cache cart colours under the overlay window for begin()/end(). */
  clut_cache_cart_under_overlay();

  /* Re-stamp overlay only when it sits past cart/twins (pico-8). Full
   * 256-colour carts own every slot — permanent stamp corrupted PLAYPAL
   * indices 64..; pause/HUD use lcd_overlay_clut_begin() instead. */
  if (overlay_clut_count > 0 && !overlay_collides_with_cart())
    clut_apply_saved_overlay();

  clut_rebuild_darken_map();
  clut_push();
}

int lcd_clut_has_dark_twins(void)
{
  return clut_dark_twins != 0;
}

/* Nearest cart-palette index for an RGB888 colour (overlay slots skipped —
 * darken targets gameplay pixels, not menu chrome already drawn on top). */
static uint8_t clut_nearest_cart(uint32_t rgb888)
{
  int r = (int)((rgb888 >> 16) & 0xFF);
  int g = (int)((rgb888 >>  8) & 0xFF);
  int b = (int)((rgb888      ) & 0xFF);
  int best_dist = 0x7FFFFFFF;
  uint8_t best_idx = 0;
  uint16_t n = active_clut_count;
  if (n == 0) return 0;
  if (n > LCD_CLUT_HW_MAX) n = LCD_CLUT_HW_MAX;

  for (uint16_t i = 0; i < n; i++) {
    uint32_t e = active_clut[i];
    int er = (int)((e >> 16) & 0xFF);
    int eg = (int)((e >>  8) & 0xFF);
    int eb = (int)((e      ) & 0xFF);
    int dr = r - er, dg = g - eg, db = b - eb;
    int d = dr * dr + dg * dg + db * db;
    if (d < best_dist) {
      best_dist = d;
      best_idx = (uint8_t)i;
      if (d == 0) break;
    }
  }
  return best_idx;
}

static uint32_t clut_darken_rgb888(uint32_t e)
{
  int keep = 100 - LCD_DARKEN_PERCENT;
  uint32_t r = (((e >> 16) & 0xFF) * (uint32_t)keep) / 100u;
  uint32_t g = (((e >>  8) & 0xFF) * (uint32_t)keep) / 100u;
  uint32_t b = (((e      ) & 0xFF) * (uint32_t)keep) / 100u;
  return (r << 16) | (g << 8) | b;
}

static void clut_rebuild_darken_map(void)
{
  clut_darken_map[0] = 0; /* letterbox / clear stays black */

  if (clut_dark_twins && active_clut_count > 0) {
    uint16_t c = active_clut_count;
    for (uint16_t i = 1; i < LCD_CLUT_HW_MAX; i++) {
      if (i < c)
        clut_darken_map[i] = (uint8_t)(i + c);
      else if (i < (uint16_t)(2u * c))
        clut_darken_map[i] = 0; /* second darken → black */
      else
        clut_darken_map[i] = (uint8_t)i; /* overlay / beyond */
    }
  } else if (active_clut_count > 0) {
    uint16_t ov0 = LCD_OVERLAY_CLUT_BASE;
    uint16_t ov1 = (uint16_t)(LCD_OVERLAY_CLUT_BASE + overlay_clut_count);
    for (uint16_t i = 1; i < LCD_CLUT_HW_MAX; i++) {
      /* Never nearest-match live overlay chrome into the cart palette —
       * that turned HUD gray into a greenish NES colour on the 2nd darken. */
      if (overlay_clut_live && overlay_clut_count > 0 && i >= ov0 && i < ov1) {
        clut_darken_map[i] = (uint8_t)i;
        continue;
      }
      if (i < active_clut_count)
        clut_darken_map[i] = clut_nearest_cart(clut_darken_rgb888(active_clut[i]));
      else
        clut_darken_map[i] = (uint8_t)i;
    }
  } else {
    for (uint16_t i = 1; i < LCD_CLUT_HW_MAX; i++)
      clut_darken_map[i] = (uint8_t)i;
  }
  clut_darken_map_valid = 1;
}

uint8_t lcd_darken_index(uint8_t idx)
{
  if (current_lcd_mode != LCD_MODE_LUT8)
    return idx;
  if (!clut_darken_map_valid)
    clut_rebuild_darken_map();
  return clut_darken_map[idx];
}

int lcd_clut_luma_sum(uint8_t idx)
{
  if (idx >= LCD_CLUT_HW_MAX)
    return 0;
  uint32_t e = active_clut[idx];
  return (int)((e >> 16) & 0xFF) + (int)((e >> 8) & 0xFF) + (int)(e & 0xFF);
}

void lcd_darken_active_buffer(void)
{
  if (current_lcd_mode != LCD_MODE_LUT8)
    return;

  if (!clut_darken_map_valid)
    clut_rebuild_darken_map();

  uint8_t *fb = (uint8_t *)lcd_get_active_buffer();
  size_t n = lcd_get_frame_size();
  for (size_t i = 0; i < n; i++)
    fb[i] = clut_darken_map[fb[i]];
}

void lcd_set_overlay_clut(const uint32_t *colors, uint16_t count)
{
  if (colors == NULL) return;
  if (count > LCD_OVERLAY_CLUT_MAX) count = LCD_OVERLAY_CLUT_MAX;

  for (uint16_t i = 0; i < count; i++)
    overlay_clut_saved[i] = colors[i];
  overlay_clut_count = count;

  /* Stamp into the live table when chrome is already up, when there is no
   * collision with the cart, or before any cart palette exists. Colliding
   * carts keep PLAYPAL until lcd_overlay_clut_begin(). */
  if (current_lcd_mode == LCD_MODE_LUT8) {
    if (overlay_clut_live || !overlay_collides_with_cart() ||
        active_clut_count == 0) {
      clut_apply_saved_overlay();
      clut_rebuild_darken_map();
      clut_push();
    }
  } else {
    /* Stage into active_clut so a later LUT8 switch / setup sees them. */
    for (uint16_t i = 0; i < count; i++)
      active_clut[LCD_OVERLAY_CLUT_BASE + i] = colors[i];
  }
}

void lcd_overlay_clut_begin(void)
{
  if (current_lcd_mode != LCD_MODE_LUT8 || overlay_clut_count == 0)
    return;
  if (overlay_clut_depth < 255)
    overlay_clut_depth++;
  if (overlay_clut_depth == 1) {
    clut_apply_saved_overlay();
    clut_rebuild_darken_map();
    clut_push();
  }
}

int lcd_overlay_clut_end_will_restore(void)
{
  return (current_lcd_mode == LCD_MODE_LUT8 &&
          overlay_clut_depth == 1 &&
          overlay_clut_live &&
          overlay_collides_with_cart()) ? 1 : 0;
}

int lcd_overlay_clut_end(void)
{
  if (current_lcd_mode != LCD_MODE_LUT8)
    return 0;
  if (overlay_clut_depth == 0)
    return 0;
  overlay_clut_depth--;
  if (overlay_clut_depth > 0 || !overlay_clut_live)
    return 0;
  if (!overlay_collides_with_cart())
    return 0; /* pico-8: theme stays resident past the cart */
  clut_restore_cart_under_overlay();
  clut_rebuild_darken_map();
  clut_push();
  return 1;
}

void lcd_overlay_clut_end_all(void)
{
  if (current_lcd_mode != LCD_MODE_LUT8)
    return;
  if (overlay_clut_depth == 0 && !overlay_clut_live)
    return;
  if (overlay_clut_live && overlay_collides_with_cart()) {
    overlay_clut_depth = 1;
    lcd_overlay_clut_end();
  } else {
    overlay_clut_depth = 0;
  }
}

uint16_t lcd_pack_color(uint16_t rgb565)
{
  if (current_lcd_mode != LCD_MODE_LUT8) return rgb565;
  if (active_clut_count == 0 && overlay_clut_count == 0) return 0;

  /* Decode RGB565 → RGB888 components for distance comparison. */
  int r = ((rgb565 >> 11) & 0x1F) * 255 / 31;
  int g = ((rgb565 >>  5) & 0x3F) * 255 / 63;
  int b = ((rgb565      ) & 0x1F) * 255 / 31;

  int best_dist = 0x7FFFFFFF;
  int best_idx  = 0;

  /* Scan saved overlay first — exact-match menu/HUD colours win even when
   * the live CLUT still holds cart[64..] (before begin()). */
  for (uint16_t i = 0; i < overlay_clut_count; i++) {
    uint32_t e = overlay_clut_saved[i];
    int er = (int)((e >> 16) & 0xFF);
    int eg = (int)((e >>  8) & 0xFF);
    int eb = (int)((e      ) & 0xFF);
    int dr = r - er, dg = g - eg, db = b - eb;
    int d  = dr*dr + dg*dg + db*db;
    if (d < best_dist) { best_dist = d; best_idx = LCD_OVERLAY_CLUT_BASE + (int)i; }
    if (d == 0) return (uint16_t)best_idx;
  }
  /* Then cart palette. Skip live overlay slots so theme colours keep their
   * reserved indices. Darkened twins are for lcd_darken_index(), not paint. */
  for (uint16_t i = 0; i < active_clut_count; i++) {
    if (overlay_clut_live &&
        i >= LCD_OVERLAY_CLUT_BASE &&
        i < (uint16_t)(LCD_OVERLAY_CLUT_BASE + overlay_clut_count))
      continue;
    uint32_t e = active_clut[i];
    int er = (int)((e >> 16) & 0xFF);
    int eg = (int)((e >>  8) & 0xFF);
    int eb = (int)((e      ) & 0xFF);
    int dr = r - er, dg = g - eg, db = b - eb;
    int d  = dr*dr + dg*dg + db*db;
    if (d < best_dist) { best_dist = d; best_idx = (int)i; }
  }
  return (uint16_t)(best_idx & 0xFF);
}

int lcd_get_mode(void)
{
  return (int)current_lcd_mode;
}

size_t lcd_get_frame_size(void)
{
  return (current_lcd_mode == LCD_MODE_LUT8)
      ? (size_t)(GW_LCD_WIDTH * GW_LCD_HEIGHT)            /* 1 byte/pixel */
      : (size_t)(GW_LCD_WIDTH * GW_LCD_HEIGHT * 2);       /* RGB565 */
}

void lcd_wait_for_vblank(void)
{
  uint32_t old_counter = frame_counter;
  while (old_counter == frame_counter) {
    __WFI();
  }
}

uint32_t lcd_get_frame_counter(void)
{
  return frame_counter;
}

void lcd_set_dithering(uint32_t enable) {
  LTDC_HandleTypeDef *ltdc = &hltdc;
  if (enable)
    HAL_LTDC_EnableDither(ltdc);
  else
    HAL_LTDC_DisableDither(ltdc);
}

/* set display refresh rate 50Hz or 60Hz  */
void lcd_set_refresh_rate(uint32_t frequency) {
  uint32_t plln = 9, pllr = 24;
  if (frequency == 60) {
    plln = 9;
    pllr = 24;
  }
  else if (frequency == 50) {
    plln = 10;
    pllr = 32;
  }
  else if (frequency == 72) {
    plln = 9;
    pllr = 20;
  }
  else if (frequency == 75) {
    plln = 15;
    pllr = 32;
  } else {
    //  printf("wrong lcd refresh rate; 50Hz or 60Hz only\n");
    //  assert(0);
    return;
  }

  last_frequency = frequency;

  /** reconfig PLL3 */

  RCC_PeriphCLKInitTypeDef PeriphClkInitStruct = {0};

  PeriphClkInitStruct.PeriphClockSelection = RCC_PERIPHCLK_LTDC;

  PeriphClkInitStruct.PLL3.PLL3M = 4;
  PeriphClkInitStruct.PLL3.PLL3N = plln;
  PeriphClkInitStruct.PLL3.PLL3P = 2;
  PeriphClkInitStruct.PLL3.PLL3Q = 2;
  PeriphClkInitStruct.PLL3.PLL3R = pllr;
  PeriphClkInitStruct.PLL3.PLL3RGE = RCC_PLL3VCIRANGE_3;
  PeriphClkInitStruct.PLL3.PLL3VCOSEL = RCC_PLL3VCOWIDE;
  PeriphClkInitStruct.PLL3.PLL3FRACN = 0;

  if (HAL_RCCEx_PeriphCLKConfig(&PeriphClkInitStruct) != HAL_OK) {
    Error_Handler();
  }
}

uint32_t lcd_get_last_refresh_rate() {
  return last_frequency;
}