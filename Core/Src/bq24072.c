#include <stdio.h>
#include <stdbool.h>

#include <stm32h7xx_hal.h>

#include "main.h"
#include "bq24072.h"
#include "odroid_overlay.h"

#include "utils.h"

/*
 * Fuel gauge — stock Mario / Zelda (REA) + RG live HUD
 * ---------------------------------------------------------
 * ADC: ADC2 @ PC4/INP4, ASYNC÷4, 16-bit, SMP=810.5, OVS=1024, OVSS=10,
 *      TROVS=0, offset-only calibrate, polled Start/Poll/DR.
 *
 * Stock Zelda 0x800ee42 skips the latch while /PG (USB raises the sense
 * node). RG keeps that idea for the *plug edge* (settle delay) but still
 * refreshes the HUD while /CHG after settle so a red bar can climb, and
 * while discharging in-emulator (~10 s) through the anti-rise filter
 * (drops update; unload spikes do not). Unplug forces a discharge re-latch
 * so a charger-inflated reading cannot stick.
 *
 * Also latches at init / boot_resample / wake_resample / mode_entry.
 *
 * Level map (Mario sub_8002d02): strict adc > thresh; PE7 low → chg tables;
 * board rev (PC8) selects rev0/rev1 scale.
 */

#define BQ24072_PROFILING           0
/* Stock main loop: level < 2 for 0x12c frames ≈ 5s @ 60Hz. */
#define BQ24072_RUNTIME_LOW_LEVEL   2
#define BQ24072_LOW_DEBOUNCE_MS     5000
/* Stock HAL_ADC_PollForConversion timeout is 0x3e8. */
#define BQ24072_ADC_POLL_MS         1000
/* Stock sub_8002e04 long timer 0x2710 — rare discharge / charge refresh. */
#define BQ24072_DISCHARGE_REFRESH_MS 10000
/* Ignore USB sense bump right after plug before allowing charge latches. */
#define BQ24072_CHARGE_SETTLE_MS    5000
/* After unplug, wait for the rail to fall before forcing a discharge latch. */
#define BQ24072_UNPLUG_SETTLE_MS    1000

/*
 * Thresholds from Mario sub_8002d02 / Zelda sub_800320e.
 * PE7/CHG active-low: pin low → first (higher) table; pin high → second.
 */
static const uint16_t bq24072_thr_chg_rev0[5] = {
    0x95e5, 0x9651, 0xa06c, 0xa436, 0xaa1b,
};
static const uint16_t bq24072_thr_dis_rev0[5] = {
    0x8f28, 0x8f94, 0x99af, 0x9d79, 0xa35e,
};
static const uint16_t bq24072_thr_chg_rev1[5] = {
    0x2a62, 0x2a81, 0x2d71, 0x2e8c, 0x3043,
};
static const uint16_t bq24072_thr_dis_rev1[5] = {
    0x29a5, 0x29c5, 0x2cb5, 0x2dcf, 0x2f86,
};

typedef enum {
    BQ24072_PIN_CHG,
    BQ24072_PIN_PGOOD,
    BQ24072_PIN_COUNT
} bq24072_pin_t;

static const struct {
    uint32_t        pin;
    GPIO_TypeDef*   bank;
} bq_pins[BQ24072_PIN_COUNT] = {
    [BQ24072_PIN_CHG]   = { .pin = GPIO_CHARGER_CHARGING_Pin, .bank = GPIO_CHARGER_CHARGING_GPIO_Port},
    [BQ24072_PIN_PGOOD] = { .pin = GPIO_CHARGER_POWERGOOD_Pin, .bank = GPIO_CHARGER_POWERGOOD_GPIO_Port},
};

extern ADC_HandleTypeDef hadc2;
extern TIM_HandleTypeDef htim1;

/* Defined below; used while latching a sample. */
bq24072_state_t bq24072_get_state(void);

#if BQ24072_PROFILING
static volatile uint32_t bq24072_battery_value;
#endif

static struct {
    uint16_t    value;       /* stock 0x20000ea0 */
    uint16_t    filtered;    /* stock 0x20000ea8 */
    int         level;       /* stock 0x20000e8c latched */
    int         live_level;  /* last raw peek (diag / boot gate); HUD uses level */
    bool        allow_rise;  /* stock 0x20000e8f; cleared after each sample */
    bool        charging;    /* PE7 low */
    bool        power_good;  /* PA2 low (rev1) */
    bool        have_sample;
    uint8_t     board_rev;   /* PC8: 0=high-count, 1=low-count */
    uint32_t    last_dis_ms;
    /* Diagnostic: first latched ADC vs later peeks (blink / 5s power-off). */
    uint16_t    first_adc;
    int         first_level;
    bool        have_first;
    uint16_t    peek_count;
    struct {
        int             percent;
        bq24072_state_t state;
    }           last;
} bq24072_data;

/* Stock sub_8006300 / Zelda 0x800ee8e */
static uint8_t bq24072_detect_board_rev(void)
{
    GPIO_InitTypeDef gpio = {0};
    GPIO_PinState pin;

    __HAL_RCC_GPIOC_CLK_ENABLE();

    gpio.Pin = GPIO_PIN_8;
    gpio.Mode = GPIO_MODE_INPUT;
    gpio.Pull = GPIO_PULLUP;
    HAL_GPIO_Init(GPIOC, &gpio);

    for (volatile int i = 0; i < 1000; i++) {
    }
    pin = HAL_GPIO_ReadPin(GPIOC, GPIO_PIN_8);

    gpio.Mode = GPIO_MODE_ANALOG;
    gpio.Pull = GPIO_NOPULL;
    HAL_GPIO_Init(GPIOC, &gpio);

    return (pin == GPIO_PIN_SET) ? 0 : 1;
}

void bq24072_handle_power_good(void)
{
    /* PA2 low ⇒ /PG asserted ⇒ USB present (stock sub_800646e rev1). */
    bq24072_data.power_good = !(HAL_GPIO_ReadPin(bq_pins[BQ24072_PIN_PGOOD].bank,
                                                 bq_pins[BQ24072_PIN_PGOOD].pin) == GPIO_PIN_SET);
}

void bq24072_handle_charging(void)
{
    /* PE7 low ⇒ /CHG asserted ⇒ charging (stock PE7 bit7 clear → chg tables). */
    bq24072_data.charging = !(HAL_GPIO_ReadPin(bq_pins[BQ24072_PIN_CHG].bank,
                                               bq_pins[BQ24072_PIN_CHG].pin) == GPIO_PIN_SET);
}

static const uint16_t *bq24072_active_thresholds(void)
{
    if (bq24072_data.board_rev == 0) {
        return bq24072_data.charging ? bq24072_thr_chg_rev0
                                     : bq24072_thr_dis_rev0;
    }
    return bq24072_data.charging ? bq24072_thr_chg_rev1
                                 : bq24072_thr_dis_rev1;
}

/* Stock: cmp thresh, adc; bhs → level N only if adc > thresh. */
static int bq24072_adc_to_level(uint16_t adc)
{
    const uint16_t *t = bq24072_active_thresholds();

    if (adc > t[4]) {
        return 5;
    }
    if (adc > t[3]) {
        return 4;
    }
    if (adc > t[2]) {
        return 3;
    }
    if (adc > t[1]) {
        return 2;
    }
    if (adc > t[0]) {
        return 1;
    }
    return 0;
}

static int bq24072_level_to_percent(int level)
{
    if (level <= 0) {
        return 0;
    }
    if (level >= 5) {
        return 100;
    }
    return level * 20;
}

static int bq24072_level_to_hud_bars(int level)
{
    int mapped;

    if (level <= 1) {
        mapped = 0;
    } else if (level < 6) {
        mapped = level - 2;
    } else {
        mapped = 3;
    }
    return mapped + 1;
}

static void bq24072_log_reading(const char *tag)
{
    printf("bq24072: %s ADC=0x%04x thr=%s lvl=%d bars=%d/4 %%=%d "
           "chg=%d pgood=%d rev=%u\n",
           tag,
           bq24072_data.value,
           bq24072_data.charging ? "chg" : "dis",
           bq24072_data.level,
           bq24072_level_to_hud_bars(bq24072_data.level),
           bq24072_level_to_percent(bq24072_data.level),
           bq24072_data.charging ? 1 : 0,
           bq24072_data.power_good ? 1 : 0,
           bq24072_data.board_rev);
}

static void bq24072_log_sample(const char *tag, uint16_t adc, int level)
{
    int d_adc = 0;
    int d_lvl = 0;

    if (bq24072_data.have_first) {
        d_adc = (int)adc - (int)bq24072_data.first_adc;
        d_lvl = level - bq24072_data.first_level;
    }

    printf("bq24072: %s ADC=0x%04x thr=%s lvl=%d bars=%d/4 %%=%d "
           "chg=%d pgood=%d rev=%u",
           tag,
           adc,
           bq24072_data.charging ? "chg" : "dis",
           level,
           bq24072_level_to_hud_bars(level),
           bq24072_level_to_percent(level),
           bq24072_data.charging ? 1 : 0,
           bq24072_data.power_good ? 1 : 0,
           bq24072_data.board_rev);

    if (bq24072_data.have_first) {
        printf(" | vs_first ADC=%+d lvl=%+d (first=0x%04x/lvl%d) HUD=0x%04x/lvl%d",
               d_adc, d_lvl,
               bq24072_data.first_adc, bq24072_data.first_level,
               bq24072_data.value, bq24072_data.level);
    }
    printf("\n");
}

static void bq24072_remember_first(uint16_t adc, int level)
{
    if (bq24072_data.have_first) {
        return;
    }
    bq24072_data.first_adc = adc;
    bq24072_data.first_level = level;
    bq24072_data.have_first = true;
    printf("bq24072: first_ref ADC=0x%04x lvl=%d bars=%d/4\n",
           adc, level, bq24072_level_to_hud_bars(level));
}

/*
 * Stock Mario sub_80015de: HAL_ADCEx_Calibration_Start(hadc, 0, 0x7FF)
 * → offset only, single-ended (ADCALLIN/ADCALDIF masked off).
 */
static bool bq24072_calibrate(void)
{
    if (HAL_ADCEx_Calibration_Start(&hadc2, ADC_CALIB_OFFSET, ADC_SINGLE_ENDED) != HAL_OK) {
        MX_ADC2_Init();
        if (HAL_ADCEx_Calibration_Start(&hadc2, ADC_CALIB_OFFSET, ADC_SINGLE_ENDED) != HAL_OK) {
            return false;
        }
    }
    return true;
}

/* Stock Mario sub_800163a: Start → Poll(1000) → GetValue(DR). */
static bool bq24072_read_adc_blocking(uint16_t *out)
{
    uint16_t sample;

    if (HAL_ADC_Start(&hadc2) != HAL_OK) {
        return false;
    }
    if (HAL_ADC_PollForConversion(&hadc2, BQ24072_ADC_POLL_MS) != HAL_OK) {
        HAL_ADC_Stop(&hadc2);
        return false;
    }
    sample = (uint16_t)HAL_ADC_GetValue(&hadc2);
    HAL_ADC_Stop(&hadc2);
    *out = sample;
    return true;
}

static bool bq24072_convert(uint16_t *out)
{
    MX_ADC2_Init();
    if (!bq24072_calibrate()) {
        return false;
    }
    return bq24072_read_adc_blocking(out);
}

/*
 * Stock Mario sub_8002ec2 / Zelda sub_80033ce:
 *   raw = conversion
 *   if raw > filtered && !allow_rise → keep filtered
 *   else filtered = raw
 *   value = result; allow_rise = 0; level = map(result)
 */
static void bq24072_sample_filter_latch(uint16_t raw)
{
#if BQ24072_PROFILING == 1
    bq24072_battery_value = raw;
#endif

    if (bq24072_data.have_sample
        && raw > bq24072_data.filtered
        && !bq24072_data.allow_rise) {
        raw = bq24072_data.filtered;
    } else {
        bq24072_data.filtered = raw;
    }

    bq24072_data.value = raw;
    bq24072_data.allow_rise = false;
    bq24072_handle_charging(); /* map uses live PE7 */
    bq24072_data.level = bq24072_adc_to_level(raw);
    bq24072_data.live_level = bq24072_data.level;
    bq24072_data.have_sample = true;
    bq24072_data.last.percent = bq24072_level_to_percent(bq24072_data.level);
    bq24072_data.last.state = bq24072_get_state();
    bq24072_remember_first(bq24072_data.value, bq24072_data.level);
}

/*
 * Stock Mario sub_80062ca body when not frozen:
 *   MX_ADC2_Init(); calibrate(); sample_filter_latch(conversion);
 */
static bool bq24072_fuel_update(void)
{
    uint16_t sample;

    if (!bq24072_convert(&sample)) {
        return false;
    }
    bq24072_sample_filter_latch(sample);
    return true;
}

int32_t bq24072_init(void)
{
    bq24072_data.have_sample = false;
    bq24072_data.filtered = 0;
    bq24072_data.value = 0;
    bq24072_data.level = 0;
    bq24072_data.live_level = 0;
    bq24072_data.last_dis_ms = 0;
    bq24072_data.have_first = false;
    bq24072_data.first_adc = 0;
    bq24072_data.first_level = 0;
    bq24072_data.peek_count = 0;
    /*
     * First sample: allow_rise must be set — filtered starts at 0, and stock
     * anti-rise would otherwise reject any raw > 0 (Mario sub_8002ec2).
     */
    bq24072_data.allow_rise = true;
    bq24072_data.last.percent = 0;
    bq24072_data.last.state = BQ24072_STATE_MISSING;
    bq24072_data.board_rev = bq24072_detect_board_rev();
    printf("bq24072: board rev %u (%s ADC tables, PC8=%s)\n",
           bq24072_data.board_rev,
           bq24072_data.board_rev == 0 ? "high-count" : "low-count",
           bq24072_data.board_rev == 0 ? "high" : "low");

    bq24072_handle_power_good();
    bq24072_handle_charging();

    /*
     * Stock's displayed bars come from a rare latch (sub_80062b4), not from
     * a backlight-sag trough. Sample once here for the HUD latch.
     */
    if (bq24072_data.power_good) {
        printf("bq24072: init skipped (PGOOD freeze, stock)\n");
    } else if (bq24072_fuel_update()) {
        bq24072_log_reading("init");
    } else {
        printf("bq24072: init ADC convert failed\n");
    }

    HAL_TIM_Base_Start_IT(&htim1);
    return 0;
}

void bq24072_boot_resample(void)
{
    uint16_t sample;
    int gate_level;

    /*
     * Stock Mario sub_80062b4: one conversion + map. No backlight, no Delay.
     * If init already latched, use this reading only as empty-boot gate /
     * live_level — do not let a later sag overwrite a healthy HUD latch.
     */
    bq24072_handle_power_good();
    bq24072_handle_charging();

    if (bq24072_data.power_good) {
        printf("bq24072: boot_resample skipped (PGOOD freeze, stock)\n");
        return;
    }

    if (!bq24072_convert(&sample)) {
        printf("bq24072: boot_resample ADC convert failed\n");
        return;
    }

    bq24072_handle_charging();
    gate_level = bq24072_adc_to_level(sample);
    bq24072_data.live_level = gate_level;
    bq24072_log_sample("boot_gate", sample, gate_level);

    if (gate_level == 0 && !bq24072_data.charging) {
        bq24072_data.allow_rise = true;
        bq24072_sample_filter_latch(sample);
        bq24072_log_reading("boot_resample");
        return;
    }

    if (!bq24072_data.have_sample) {
        bq24072_data.allow_rise = true;
        bq24072_sample_filter_latch(sample);
        bq24072_log_reading("boot_resample");
        return;
    }

    printf("bq24072: boot_gate HUD kept ADC=0x%04x lvl=%d bars=%d/4\n",
           bq24072_data.value, bq24072_data.level,
           bq24072_level_to_hud_bars(bq24072_data.level));
}

void bq24072_interrupts_enable(void)
{
    GPIO_InitTypeDef GPIO_InitStruct = {0};

    GPIO_InitStruct.Pin = GPIO_CHARGER_POWERGOOD_Pin;
    GPIO_InitStruct.Mode = GPIO_MODE_IT_RISING_FALLING;
    GPIO_InitStruct.Pull = GPIO_NOPULL;
    HAL_GPIO_Init(GPIO_CHARGER_POWERGOOD_GPIO_Port, &GPIO_InitStruct);

    GPIO_InitStruct.Pin = GPIO_CHARGER_CHARGING_Pin;
    GPIO_InitStruct.Mode = GPIO_MODE_IT_RISING_FALLING;
    GPIO_InitStruct.Pull = GPIO_NOPULL;
    HAL_GPIO_Init(GPIO_CHARGER_CHARGING_GPIO_Port, &GPIO_InitStruct);
}

void bq24072_interrupts_disable(void)
{
    HAL_GPIO_DeInit(GPIO_CHARGER_POWERGOOD_GPIO_Port, GPIO_CHARGER_POWERGOOD_Pin);
    HAL_GPIO_DeInit(GPIO_CHARGER_CHARGING_GPIO_Port, GPIO_CHARGER_CHARGING_Pin);
}

bq24072_state_t bq24072_get_state(void)
{
    if (bq24072_data.power_good) {
        if (bq24072_data.charging) {
            return BQ24072_STATE_CHARGING;
        }
        return BQ24072_STATE_FULL;
    }
    if (!bq24072_data.charging) {
        return BQ24072_STATE_DISCHARGING;
    }
    return BQ24072_STATE_MISSING;
}

int bq24072_get_level(void)
{
    if (bq24072_get_state() == BQ24072_STATE_MISSING || !bq24072_data.have_sample) {
        return 0;
    }
    /* Stock sub_8002dc6: return latched byte only. */
    return bq24072_data.level;
}

uint8_t bq24072_get_board_rev(void)
{
    return bq24072_data.board_rev;
}

int bq24072_get_percent(void)
{
    if (bq24072_get_state() == BQ24072_STATE_MISSING || !bq24072_data.have_sample) {
        return 0;
    }
    return bq24072_level_to_percent(bq24072_data.level);
}

bool bq24072_should_block_boot(void)
{
    if (bq24072_data.charging || !bq24072_data.have_sample) {
        return false;
    }
    /* Under-load gate from boot_resample (stock sub_80062b4 empty check). */
    return bq24072_data.live_level == 0;
}

/* Persists across should_power_off calls; cleared on wake / healthy latch. */
static uint32_t bq24072_low_since_ms;
static bool bq24072_low_logged_arm;
/* Shared with wake_resample so the post-STOP2 drop peek is timed from the
 * under-load latch, not a pre-sleep poll stamp frozen across STOP2. */
static uint32_t bq24072_last_info_ms;

static void bq24072_clear_low_debounce(const char *why)
{
    if (bq24072_low_since_ms != 0) {
        printf("bq24072: low_debounce clear (%s)\n", why);
    }
    bq24072_low_since_ms = 0;
    bq24072_low_logged_arm = false;
}

void bq24072_wake_prepare(void)
{
    /*
     * STOP2 stops TIM1 — charge progress and multi-day self-discharge never
     * reached the HUD. Clear debounce immediately so a stale low arm cannot
     * fire the moment clocks resume; defer the conversion until loads are on
     * (bq24072_wake_resample from gw_sleep after LCD/SD).
     */
    bq24072_handle_power_good();
    bq24072_handle_charging();
    bq24072_data.allow_rise = true;
    bq24072_data.have_first = false;
    bq24072_data.peek_count = 0;
    bq24072_clear_low_debounce("wake");
}

void bq24072_wake_resample(void)
{
    bq24072_handle_power_good();
    bq24072_handle_charging();
    bq24072_data.allow_rise = true;
    bq24072_clear_low_debounce("wake");

    if (bq24072_data.power_good) {
        printf("bq24072: wake_resample skipped (PGOOD) — allow_rise kept\n");
        return;
    }

    /* Fresh first_ref for this wake so peeks compare against the settle latch. */
    bq24072_data.have_first = false;
    bq24072_data.peek_count = 0;

    if (bq24072_fuel_update()) {
        uint32_t now = HAL_GetTick();

        bq24072_last_info_ms = now ? now : 1;
        bq24072_log_reading("wake");
        if (bq24072_data.level >= BQ24072_RUNTIME_LOW_LEVEL) {
            bq24072_clear_low_debounce("wake healthy");
        }
    } else {
        printf("bq24072: wake_resample ADC convert failed\n");
    }
}

void bq24072_mode_entry_resample(void)
{
    /* Stock Zelda 0x800ee42: skip while /PG; otherwise one filtered latch. */
    bq24072_handle_power_good();
    bq24072_handle_charging();

    if (bq24072_data.power_good) {
        printf("bq24072: mode_entry skipped (PGOOD freeze, stock)\n");
        return;
    }

    if (bq24072_fuel_update()) {
        uint32_t now = HAL_GetTick();

        bq24072_last_info_ms = now ? now : 1;
        bq24072_log_reading("mode_entry");
        if (bq24072_data.level >= BQ24072_RUNTIME_LOW_LEVEL) {
            bq24072_clear_low_debounce("mode_entry healthy");
        }
    } else {
        printf("bq24072: mode_entry ADC convert failed\n");
    }
}

bool bq24072_charger_connected(void)
{
    bq24072_handle_power_good();
    bq24072_handle_charging();
    return bq24072_data.power_good || bq24072_data.charging;
}

bool bq24072_should_power_off(void)
{
    uint32_t now;
    int level;

    if (bq24072_get_state() != BQ24072_STATE_DISCHARGING || !bq24072_data.have_sample) {
        bq24072_clear_low_debounce("state/chg change");
        return false;
    }

    /*
     * Stock Mario: main loop reads the latched level (sub_8002dc6), not a
     * continuous under-load ADC peek. Peeks under RG load sit ~150–260 ADC
     * below the latch and were falsely arming the 5 s power-off while Zelda
     * still treated the pack as OK.
     */
    level = bq24072_data.level;
    if (level >= BQ24072_RUNTIME_LOW_LEVEL) {
        bq24072_clear_low_debounce("HUD healthy");
        return false;
    }

    now = HAL_GetTick();
    if (bq24072_low_since_ms == 0) {
        bq24072_low_since_ms = now ? now : 1;
        if (!bq24072_low_logged_arm) {
            printf("bq24072: low_debounce ARM HUD_lvl=%d ADC=0x%04x "
                   "(power-off in %ums)\n",
                   level, bq24072_data.value, BQ24072_LOW_DEBOUNCE_MS);
            bq24072_low_logged_arm = true;
        }
        return false;
    }

    return (now - bq24072_low_since_ms) >= BQ24072_LOW_DEBOUNCE_MS;
}

void bq24072_enforce_low_battery(void)
{
    if (bq24072_should_power_off()) {
        printf("bq24072: POWER_OFF HUD_lvl=%d HUD_ADC=0x%04x",
               bq24072_data.level, bq24072_data.value);
        if (bq24072_data.have_first) {
            printf(" first=0x%04x/lvl%d",
                   bq24072_data.first_adc, bq24072_data.first_level);
        }
        printf("\n");
        odroid_overlay_low_battery_power_off();
    }
}

bool bq24072_battery_too_low_for_sd(void)
{
    bq24072_handle_power_good();
    bq24072_handle_charging();

    /* USB present / charging — SD current is fine. */
    if (bq24072_data.power_good || bq24072_data.charging) {
        return false;
    }

    /*
     * Gate on the stock latched level (same as Zelda HUD / power-off), not a
     * fresh under-load peek that reads ~150–260 ADC lower on RG.
     */
    if (!bq24072_data.have_sample) {
        printf("bq24072: sd_check no sample — refuse SD\n");
        return true;
    }

    bq24072_log_sample("sd_check", bq24072_data.value, bq24072_data.level);
    return bq24072_data.level < BQ24072_RUNTIME_LOW_LEVEL;
}

int bq24072_get_percent_filtered(void)
{
    static int last_logged_level = -1;
    static int last_logged_pgood = -1;
    static int last_logged_chg = -1;
    int percent;

    if (!bq24072_data.have_sample) {
        return 0;
    }

    /* UI follows the stock latched level; no extra hysteresis. */
    percent = bq24072_level_to_percent(bq24072_data.level);
    bq24072_data.last.percent = percent;
    bq24072_data.last.state = bq24072_get_state();

    if (bq24072_data.level != last_logged_level
        || (int)bq24072_data.power_good != last_logged_pgood
        || (int)bq24072_data.charging != last_logged_chg) {
        last_logged_level = bq24072_data.level;
        last_logged_pgood = (int)bq24072_data.power_good;
        last_logged_chg = (int)bq24072_data.charging;
        bq24072_log_reading("update");
    }

    return percent;
}

void bq24072_poll(void)
{
    static int last_logged_level = -1;
    static bool was_pgood;
    static bool logged_charge_settle;
    static uint32_t pgood_since_ms;
    static uint32_t unplug_at_ms;
    uint32_t now;
    uint16_t sample;
    bool force_unplug_latch;

    bq24072_handle_power_good();
    bq24072_handle_charging();

    now = HAL_GetTick();
    if (now == 0) {
        now = 1;
    }

    /* Stock: allow_rise while /CHG so the filter may climb. */
    if (bq24072_data.charging) {
        bq24072_data.allow_rise = true;
    }

    if (bq24072_data.power_good && !was_pgood) {
        pgood_since_ms = now;
        logged_charge_settle = false;
        printf("bq24072: PGOOD assert — charge settle %ums\n",
               (unsigned)BQ24072_CHARGE_SETTLE_MS);
    }
    if (!bq24072_data.power_good && was_pgood) {
        /*
         * Drop must be allowed: a charge latch may sit above true pack OCV.
         * Do not keep allow_rise from the charge session across this sample.
         */
        bq24072_data.allow_rise = false;
        unplug_at_ms = now;
        printf("bq24072: PGOOD deassert — discharge re-latch in %ums\n",
               (unsigned)BQ24072_UNPLUG_SETTLE_MS);
    }
    was_pgood = bq24072_data.power_good;

    if (bq24072_data.power_good) {
        if (!bq24072_data.charging) {
            /* USB in, charge complete — hold HUD (stock-ish idle-on-adapter). */
            return;
        }
        if ((now - pgood_since_ms) < BQ24072_CHARGE_SETTLE_MS) {
            return;
        }
        if (!logged_charge_settle) {
            printf("bq24072: charge settle done — HUD may rise\n");
            logged_charge_settle = true;
        }
        if (bq24072_last_info_ms != 0
            && (now - bq24072_last_info_ms) < (uint32_t)BQ24072_DISCHARGE_REFRESH_MS) {
            return;
        }
        if (bq24072_fuel_update()) {
            bq24072_last_info_ms = now;
            last_logged_level = bq24072_data.level;
            bq24072_log_reading("charge");
        }
        return;
    }

    /*
     * Discharging (menu or emulator): refresh HUD ~10 s via anti-rise latch.
     * After unplug, force one sample once the rail has settled.
     */
    force_unplug_latch = (unplug_at_ms != 0
                          && (now - unplug_at_ms) >= BQ24072_UNPLUG_SETTLE_MS);
    if (!force_unplug_latch
        && bq24072_last_info_ms != 0
        && (now - bq24072_last_info_ms) < (uint32_t)BQ24072_DISCHARGE_REFRESH_MS) {
        return;
    }
    if (!bq24072_convert(&sample)) {
        return;
    }
    bq24072_last_info_ms = now;
    if (force_unplug_latch) {
        unplug_at_ms = 0;
    }
    bq24072_handle_charging();
    bq24072_sample_filter_latch(sample);
    if (bq24072_data.peek_count < 0xffff) {
        bq24072_data.peek_count++;
    }
    {
        char tag[24];
        snprintf(tag, sizeof(tag), force_unplug_latch ? "unplug#%u" : "poll#%u",
                 (unsigned)bq24072_data.peek_count);
        bq24072_log_sample(tag, bq24072_data.value, bq24072_data.level);
    }
    if (bq24072_data.level != last_logged_level) {
        last_logged_level = bq24072_data.level;
    }
    if (bq24072_data.level >= BQ24072_RUNTIME_LOW_LEVEL) {
        bq24072_clear_low_debounce("poll healthy");
    }
}
