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

/* --- Generic vblank-synchronized present (opt-in; used by triple-buffered
 * cores like EarthBound) ---------------------------------------------------
 * lcd_present_at_vblank(addr) loads the address into the LTDC layer SHADOW
 * registers via HAL_LTDC_SetAddress_NoReload(), then arms a vertical-blanking
 * reload so the *hardware* latches it tear-free at the next vblank.
 *
 * It deliberately does NOT program the address from the reload-event ISR with
 * HAL_LTDC_SetAddress() (which forces an *immediate* reload). That older design
 * was racy: the immediate reload only lands tear-free if the ISR runs inside
 * the vblank window, but a higher-priority interrupt (audio SAI / SD DMA) can
 * delay the LTDC reload ISR past vblank into the active display area. The
 * immediate reload then reloads the layer window / line-count registers
 * mid-scanout, which blanks the remainder of the frame to the background colour
 * — an occasional 1-frame black flash. Letting the hardware do the vblank
 * reload removes the software-timing dependency entirely.
 *
 * tb_mode selects this path: lcd_present_at_vblank() sets it; lcd_swap() and
 * lcd_set_buffers() clear it so the launcher / other cores keep the legacy
 * fb1/fb2 toggle in the reload callback. */
static volatile uint32_t tb_mode;       /* 1 = triple-buffer present active */
static volatile uint32_t tb_pending;    /* addr queued for next vblank (legacy compat) */
static volatile uint32_t tb_displayed;  /* addr the LTDC will scan out next vblank */

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

  // Wake up !
  // Enable 1.8V &3V3 power supply
  gw_lcd_set_reset(0);
  gw_set_power_3V3(1);
  gw_set_power_1V8(1);

  // Lets go, bootup sequence.
  /* reset sequence */
  gw_lcd_set_reset(1);
  HAL_Delay(5);
  gw_lcd_set_reset(0);
  HAL_Delay(20);
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

  // Wait for screen to finish initializing
  HAL_Delay(50);
  wdog_refresh();

  if (flags & LCD_INIT_CLEAR_BUFFERS) {
    lcd_clear_buffers();
  }

  HAL_LTDC_SetAddress(ltdc,(uint32_t) &fb1, 0);
  HAL_LTDC_ProgramLineEvent(&hltdc, 239);
  __HAL_LTDC_ENABLE_IT(&hltdc, LTDC_IT_LI | LTDC_IT_RR);

  printf("LCD: Finished init\n");
}

void HAL_LTDC_ReloadEventCallback (LTDC_HandleTypeDef *hltdc) {
  if (tb_mode) {
    /* Triple-buffer present: the address was loaded into the shadow registers
     * by lcd_present_at_vblank() and this very interrupt fired *because* the
     * hardware just completed the vblank reload that latched it. There is
     * nothing to do here — and crucially we must NOT issue another SetAddress
     * (immediate reload), which is what used to glitch a frame when this ISR
     * ran late. */
    return;
  }
  if (active_framebuffer == 0) {
    HAL_LTDC_SetAddress(hltdc, (uint32_t) fb2, 0);
  } else {
    HAL_LTDC_SetAddress(hltdc, (uint32_t) fb1, 0);
  }
}

void HAL_LTDC_LineEventCallback (LTDC_HandleTypeDef *hltdc) {
  frame_counter++;
  HAL_LTDC_ProgramLineEvent(hltdc,  239);
}

uint32_t lcd_is_tb_mode(void)
{
  return tb_mode;
}

uint32_t lcd_is_swap_pending(void)
{
  return (uint32_t) ((hltdc.Instance->SRCR) & (LTDC_SRCR_VBR | LTDC_SRCR_IMR));
}

bool lcd_sleep_while_swap_pending(void)
{
  uint32_t pending = false;

  while (lcd_is_swap_pending())
  {
    pending = true;
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
  /* Legacy fb1/fb2 double-buffer swap; the reload callback programs the
   * address. Leaving triple-buffer mode if a core had entered it. */
  tb_mode = 0;
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
  /* Returning to legacy double-buffering (launcher / other cores). */
  tb_mode = 0;
  tb_pending = 0;
  tb_displayed = 0;
}

/* Queue a buffer to be displayed at the next vblank, without blocking.
 * The address is written to the LTDC shadow registers now and latched by the
 * hardware vblank reload — no software immediate-reload, so a late reload ISR
 * can't glitch the frame (see the tb_mode comment block above). */
void lcd_present_at_vblank(void *addr)
{
  tb_mode = 1;
  HAL_LTDC_SetAddress_NoReload(&hltdc, (uint32_t)addr, 0);
  tb_displayed = (uint32_t)addr;
  tb_pending = 0;
  HAL_LTDC_Reload(&hltdc, LTDC_RELOAD_VERTICAL_BLANKING);
}

/* Address the LTDC will scan out as of the next vblank (0 until the first
 * present). Used by triple-buffered cores' diagnostics. */
void *lcd_get_displayed_buffer(void)
{
  return (void *)tb_displayed;
}

/* ----- LCD pool layout / mode switching (Phase 2) -----
 *
 * RGB565 mode: 2 framebuffers x 154K = 300K, fills the pool, no bonus.
 * LUT8 mode:   2 framebuffers x 77K  = 154K, leaves 154K of bonus.
 *
 * `current_lcd_mode` tracks the active layout so lcd_get_bonus_pool reports
 * the right region. Initialised to RGB565 to match the static-init layout
 * of framebuffer1/2 + fb1/2 set up at the top of this file. */
static lcd_mode_t current_lcd_mode = LCD_MODE_RGB565;

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

  /* Clear both framebuffer regions BEFORE the format change. Until
   * HAL_LTDC_Reload() takes effect at the next vsync the LTDC keeps
   * reading the old data; if we leave stale RGB565 bytes in place and
   * then switch to LUT8 mode, those bytes get reinterpreted as CLUT
   * indices and the screen flashes garbage colors briefly. Zeroing the
   * framebuffer means the transition is smoothly black-to-black (zero
   * is black in both RGB565 and CLUT idx 0 on PICO-8's palette). Clear
   * the union of the two modes' framebuffer footprints (= max stride),
   * never the bonus region — PICO-8's pool may live there. */
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

  current_lcd_mode = mode;
  active_framebuffer = 0;

  /* Push to LTDC: change format and source address, then reload at vsync. */
  HAL_LTDC_SetPixelFormat(&hltdc, pixel_format, 0);
  HAL_LTDC_SetAddress(&hltdc, (uint32_t)fb1, 0);
  HAL_LTDC_Reload(&hltdc, LTDC_RELOAD_VERTICAL_BLANKING);

  /* MPU follow-up: re-cover the LCD pool so only the active framebuffer
   * region stays uncached. In LUT8 mode the 146 KB bonus area becomes
   * cacheable Normal memory, which is essential if cold engine code or
   * the engine's TLSF pool lives there. The framebuffer footprint must
   * stay uncached so LTDC sees CPU writes immediately. */
  uint32_t fb_footprint = (mode == LCD_MODE_LUT8)
      ? (uint32_t)(2 * GW_LCD_WIDTH * GW_LCD_HEIGHT)        /* 154 KB */
      : (uint32_t)(2 * GW_LCD_WIDTH * GW_LCD_HEIGHT * 2);   /* 300 KB */
  HAL_MPU_Disable();
  mpu_set_lcd_pool_uncached_range(fb_footprint);
  HAL_MPU_Enable(MPU_HFNMI_PRIVDEF);
  __DSB();
  __ISB();
}

void lcd_get_bonus_pool(uint8_t **out_ptr, size_t *out_size)
{
  if (current_lcd_mode == LCD_MODE_LUT8) {
    /* 2 LUT8 framebuffers occupy the lower 154K; the rest is bonus. */
    size_t fb_block = 2 * (size_t)(GW_LCD_WIDTH * GW_LCD_HEIGHT);
    if (out_ptr)  *out_ptr  = __lcd_pool_start__ + fb_block;
    if (out_size) *out_size = (size_t)((uintptr_t)__lcd_pool_end__ -
                                       (uintptr_t)__lcd_pool_start__) - fb_block;
  } else {
    if (out_ptr)  *out_ptr  = NULL;
    if (out_size) *out_size = 0;
  }
}

/* Active CLUT staged for HAL_LTDC_ConfigCLUT and reused by lcd_pack_color()
 * for nearest-match RGB→index lookups.
 *
 * Layout (sized to fit DTCM BSS budget — bumped only by ~16 bytes vs the
 * cart-only 64-entry baseline):
 *   [0..32)         cart palette entries (count ≤ 32)
 *   [32..64)        cart palette darkened twins (LCD_DARKEN_BIT path)
 *   [64..64+OMAX)   Retro-Go overlay theme entries (LCD_OVERLAY_CLUT_BASE)
 *
 * No darkened-twin slots for the overlay — see gw_lcd.h for rationale.
 * Setting LCD_DARKEN_BIT (0x20) on a cart pixel byte switches it to the
 * darker twin; the LTDC's CLUT does the lookup at scanout. */
#define LCD_CLUT_CACHE_MAX     32
#define LCD_EXTENDED_CLUT_MAX  (LCD_CLUT_CACHE_MAX * 2 + LCD_OVERLAY_CLUT_MAX)
static uint32_t active_clut[LCD_EXTENDED_CLUT_MAX];
static uint16_t active_clut_count   = 0;   /* cart entries in [0..count); twins at [count..2*count) */
static uint16_t overlay_clut_count  = 0;   /* overlay entries in [BASE..BASE+count) */

/* Push the entire populated range to the LTDC. The HAL writes index =
 * counter, so we always send slot 0 onward. Determine how far we need
 * to go from whichever range is populated. */
static void clut_push(void)
{
  int total = (int)(2u * active_clut_count);
  if (overlay_clut_count > 0) {
    int overlay_end = LCD_OVERLAY_CLUT_BASE + overlay_clut_count;
    if (overlay_end > total) total = overlay_end;
  }
  if (total <= 0) return;
  HAL_LTDC_ConfigCLUT(&hltdc, active_clut, total, 0);
  HAL_LTDC_EnableCLUT(&hltdc, 0);
}

/* Compute and store a 40%-darkened twin of `e` (RGB888) at slot `idx`. */
static void clut_store_dark_twin(int idx, uint32_t e)
{
  int keep = 100 - LCD_DARKEN_PERCENT;
  uint32_t r = (((e >> 16) & 0xFF) * keep) / 100;
  uint32_t g = (((e >>  8) & 0xFF) * keep) / 100;
  uint32_t b = (((e      ) & 0xFF) * keep) / 100;
  active_clut[idx] = (r << 16) | (g << 8) | b;
}

void lcd_get_clut_rgb565(uint16_t *out)
{
  if (out == NULL) return;
  uint16_t n = active_clut_count;
  if (n > LCD_SCREENSHOT_CLUT_ENTRIES) n = LCD_SCREENSHOT_CLUT_ENTRIES;
  for (uint16_t i = 0; i < n; i++) {
    uint32_t e = active_clut[i];
    uint8_t r = (uint8_t)((e >> 16) & 0xFF);
    uint8_t g = (uint8_t)((e >>  8) & 0xFF);
    uint8_t b = (uint8_t)((e      ) & 0xFF);
    out[i] = (uint16_t)(((r >> 3) << 11) | ((g >> 2) << 5) | (b >> 3));
  }
  for (uint16_t i = n; i < LCD_SCREENSHOT_CLUT_ENTRIES; i++) out[i] = 0;
}

void lcd_set_clut(const uint32_t *clut, uint16_t count)
{
  if (current_lcd_mode != LCD_MODE_LUT8 || clut == NULL || count == 0) return;
  if (count > LCD_CLUT_CACHE_MAX) count = LCD_CLUT_CACHE_MAX;

  /* Cart palette → [0..count); darkened twins → [count..2*count). */
  for (uint16_t i = 0; i < count; i++) {
    active_clut[i] = clut[i];
    clut_store_dark_twin(count + i, clut[i]);
  }
  active_clut_count = count;
  clut_push();
}

void lcd_set_overlay_clut(const uint32_t *colors, uint16_t count)
{
  if (colors == NULL) return;
  if (count > LCD_OVERLAY_CLUT_MAX) count = LCD_OVERLAY_CLUT_MAX;

  /* Always store the values (so they survive a later mode switch into
   * LUT8). Push to hardware only if we're already in LUT8 — otherwise
   * the next lcd_set_clut() call from the cart will pick them up via
   * clut_push() since active_clut[] is already populated. */
  for (uint16_t i = 0; i < count; i++) {
    active_clut[LCD_OVERLAY_CLUT_BASE + i] = colors[i];
  }
  overlay_clut_count = count;
  if (current_lcd_mode == LCD_MODE_LUT8) clut_push();
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

  /* Scan overlay first — exact-match menu colors win (distance 0). */
  for (uint16_t i = 0; i < overlay_clut_count; i++) {
    uint32_t e = active_clut[LCD_OVERLAY_CLUT_BASE + i];
    int er = (int)((e >> 16) & 0xFF);
    int eg = (int)((e >>  8) & 0xFF);
    int eb = (int)((e      ) & 0xFF);
    int dr = r - er, dg = g - eg, db = b - eb;
    int d  = dr*dr + dg*dg + db*db;
    if (d < best_dist) { best_dist = d; best_idx = LCD_OVERLAY_CLUT_BASE + (int)i; }
  }
  /* Then cart palette. The darkened twins are NOT searched — they're for
   * the +LCD_DARKEN_BIT OR path, not direct color matching. */
  for (uint16_t i = 0; i < active_clut_count; i++) {
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