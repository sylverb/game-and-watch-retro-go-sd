#include <stdio.h>
#include <stdbool.h>

#include <stm32h7xx_hal.h>

#include "main.h"
#include "bq24072.h"

#include "utils.h"

#define BQ24072_PROFILING   0

typedef enum {
    BQ24072_PIN_CHG,
    BQ24072_PIN_PGOOD,
    BQ24072_PIN_COUNT // Keep this last
} bq24072_pin_t;

// PE7 - CHG
// PE8 - CE
// PA2 - PGOOD
// PC4 - Battery voltage

static const struct {
    uint32_t     pin;
    GPIO_TypeDef* bank;
} bq_pins[BQ24072_PIN_COUNT] = {
    [BQ24072_PIN_CHG]   = { .pin = GPIO_CHARGER_CHARGING_Pin,  .bank = GPIO_CHARGER_CHARGING_GPIO_Port  },
    [BQ24072_PIN_PGOOD] = { .pin = GPIO_CHARGER_POWERGOOD_Pin, .bank = GPIO_CHARGER_POWERGOOD_GPIO_Port },
};

extern ADC_HandleTypeDef hadc1;
extern TIM_HandleTypeDef htim1;

#if BQ24072_PROFILING
static volatile uint32_t bq24072_battery_value;
static volatile uint32_t bq24072_filtered_value_dbg;
#endif // BQ24072_PROFILING

static struct {
    /*
     * Raw windowed average of the last ADC conversion window.
     * Equivalent to OFW DAT_00003408+0x08 (raw sum / count).
     * Not used directly for level computation; kept for debug/profiling.
     */
    uint16_t value;

    /*
     * Monotonically-non-increasing ADC floor used as the actual input
     * to level/percent computation.
     * OFW equivalent: DAT_00003408+0x14  ("filtered value for level").
     *
     * This field can only decrease unless adc_reset_pending is set,
     * which prevents the displayed battery level from rising while
     * discharging (a common artefact of ADC noise).
     */
    uint16_t filtered_value;

    /*
     * One-shot flag: when set, the next published average is allowed to
     * exceed the current floor, resetting filtered_value upward.
     * OFW equivalent: puVar3[3] at DAT_00003408+0x03.
     *
     * Must be set whenever the charger state changes (plug/unplug) so
     * that the floor tracks the new voltage level correctly.
     */
    bool     adc_reset_pending;

    bool     adc_settle_pending;
    bool     sample_valid;
    uint8_t  sample_count;
    uint32_t sample_sum;
    bool     charging;
    bool     power_good;

    struct {
        bool            initialized;
        int             percent;
        bq24072_state_t state;
    } last;
} bq24072_data;

typedef struct {
    uint16_t th[5];
} bq24072_level_table_t;

/*
 * Threshold tables extracted verbatim from OFW FUN_00003148 / FUN_0000320e.
 *
 * The OFW selects the active table based on two runtime flags:
 *
 *   1. Domain flag (DAT_000033fc):
 *        == 0  -> "high" domain  (ADC reading >= 0x4000, normal operation)
 *        != 0  -> "low"  domain  (ADC reading <  0x4000, low-battery range)
 *      In our implementation this maps to: adc_value < 0x4000.
 *
 *   2. Charging-context bit (bit 1 of FUN_000034ac() return in FUN_00003148,
 *      or bit 7 of *DAT_00003404 in FUN_0000320e — same semantic):
 *        bit SET   -> charging/full table
 *        bit CLEAR -> discharging table
 *      In our implementation: state == CHARGING || state == FULL.
 *
 * th[3] of bq24072_levels_high_discharging is 0x9d79, which Ghidra shows as
 * "(uint)&LAB_00009d78 + 1" — a relocation artefact; the numeric value is correct.
 *
 * th[3] of bq24072_levels_high_charging is 0xa436, shown by Ghidra as
 * "(uint)FUN_0000a436" — likewise a relocation artefact; value is correct.
 */
static const bq24072_level_table_t bq24072_levels_low_discharging = {
    .th = { 0x29a5, 0x29c5, 0x2cb5, 0x2dcf, 0x2f86 },
};

static const bq24072_level_table_t bq24072_levels_low_charging = {
    .th = { 0x2a62, 0x2a81, 0x2d71, 0x2e8c, 0x3043 },
};

static const bq24072_level_table_t bq24072_levels_high_discharging = {
    /* th[3] = &LAB_00009d78 + 1 = 0x9d79  (Ghidra relocation artefact) */
    .th = { 0x8f28, 0x8f94, 0x99af, 0x9d79, 0xa35e },
};

static const bq24072_level_table_t bq24072_levels_high_charging = {
    /* th[3] = (uint)FUN_0000a436 = 0xa436  (Ghidra relocation artefact) */
    .th = { 0x95e5, 0x9651, 0xa06c, 0xa436, 0xaa1b },
};

/* ======================================================================
 * Level / percent helpers
 * ====================================================================== */

/*
 * Returns the OFW battery level (0-5) for the given ADC reading.
 * Mirrors the comparison chain at LAB_000031dc / LAB_000032a0 in the OFW.
 * Kept so that debug logs can be correlated directly with OFW output.
 */
static uint8_t bq24072_level_from_table(uint16_t adc_value,
                                         const bq24072_level_table_t* table)
{
    if (adc_value > table->th[4]) return 5;
    if (adc_value > table->th[3]) return 4;
    if (adc_value > table->th[2]) return 3;
    if (adc_value > table->th[1]) return 2;
    if (adc_value > table->th[0]) return 1;
    return 0;
}

/*
 * Converts an ADC reading to a smooth 0-100 % SoC estimate by linearly
 * interpolating within the OFW level segments.
 *
 * The OFW only exposes a 0-5 level, but the first two thresholds are
 * extremely close (< 0x20 apart), so a flat 20 %/step mapping would
 * grossly over-report charge near empty.  The soc_anchor table below
 * maps each segment boundary to a calibrated SoC anchor, and we
 * interpolate linearly between anchors within each segment.
 */
static int bq24072_percent_from_table(uint16_t adc_value,
                                       const bq24072_level_table_t* table)
{
    static const uint8_t soc_anchor[6] = {
         0,   // <= th[0] : essentially empty
         1,   // <= th[1] : just above the first threshold
         6,   // <= th[2]
        18,   // <= th[3]
        45,   // <= th[4]
       100,   // >  th[4] : full
    };

    int      segment;
    uint16_t low_adc, high_adc;
    int      low_percent, high_percent, span_adc;

    if (adc_value <= table->th[0]) return soc_anchor[0];
    if (adc_value >  table->th[4]) return soc_anchor[5];

    if      (adc_value <= table->th[1]) segment = 0;
    else if (adc_value <= table->th[2]) segment = 1;
    else if (adc_value <= table->th[3]) segment = 2;
    else                                segment = 3;

    low_adc      = table->th[segment];
    high_adc     = table->th[segment + 1];
    low_percent  = soc_anchor[segment + 1];
    high_percent = soc_anchor[segment + 2];

    if (high_adc <= low_adc) return low_percent;

    span_adc = (int)high_adc - (int)low_adc;
    return low_percent +
           (((int)adc_value - (int)low_adc) * (high_percent - low_percent)) / span_adc;
}

/*
 * Selects the appropriate threshold table based on the current ADC
 * domain and charge state.
 *
 * Mirrors the OFW logic that reads DAT_000033fc (domain flag) and the
 * charging bit from FUN_000034ac() / *DAT_00003404:
 *   - adc_value < 0x4000  <->  DAT_000033fc != 0  (low domain)
 *   - adc_value >= 0x4000 <->  DAT_000033fc == 0  (high domain)
 *   - CHARGING / FULL     <->  charging-context bit SET
 *   - DISCHARGING         <->  charging-context bit CLEAR
 */
static const bq24072_level_table_t* bq24072_select_table(uint16_t adc_value,
                                                           bq24072_state_t state)
{
    bool low_domain       = (adc_value < 0x4000);
    bool charging_context = (state == BQ24072_STATE_CHARGING) ||
                            (state == BQ24072_STATE_FULL);

    if (low_domain)
        return charging_context ? &bq24072_levels_low_charging
                                : &bq24072_levels_low_discharging;

    return charging_context ? &bq24072_levels_high_charging
                            : &bq24072_levels_high_discharging;
}

/* ======================================================================
 * ADC interrupt callback
 *
 * Reconstructed from OFW FUN_00001b58
 *
 * OFW data-structure layout at DAT_00003408 (batt_ctx):
 *   +0x00  level byte         (FUN_000032d2 returns this)
 *   +0x04  valid flag         (FUN_000033c8 returns this)
 *   +0x03  adc_reset_pending  (one-shot reset flag, "puVar3[3]")
 *   +0x08  raw average        (sum / count after window)
 *   +0x0c  sample count       (running, never reset in OFW)
 *   +0x14  filtered value     (monotone floor, input to FUN_00003148)
 *   +0x1c  adc floor          (same as filtered value, "stored_min")
 *
 * Key behavioral points extracted from the OFW:
 *
 *  (a) SAMPLE WINDOW — OFW condition is "if (8 < uVar9)" where uVar9 is
 *      the counter AFTER increment.  The first publish therefore occurs
 *      on the 9th sample, not the 8th.  Fixed: >= 9u.
 *
 *  (b) RUNNING vs WINDOWED ACCUMULATOR — The OFW never resets sample_sum
 *      or sample_count after publishing (running accumulator).  We use a
 *      windowed accumulator instead to prevent uint32_t overflow on long
 *      uptimes.  Behavior is identical at steady state; the only visible
 *      difference is during the very first 9-sample window at boot.
 *
 *  (c) MONOTONICITY FILTER — OFW FUN_00001b58 maintains a non-increasing
 *      ADC floor ("stored_min" at DAT_00003408+0x1c).  After each window:
 *
 *        if (new_avg <= stored_min  OR  reset_flag) {
 *            stored_min   = new_avg;     // floor moves down (or resets)
 *            use_value    = new_avg;
 *        } else {
 *            use_value    = stored_min;  // reject rise, hold floor
 *        }
 *        reset_flag = 0;
 *
 *      This stops the battery icon from jumping upward due to ADC noise
 *      while the device is discharging.  The previous bq24072.c only
 *      applied monotonicity at the percent level (bq24072_get_percent_filtered),
 *      which is too late: the table lookup itself could select a higher
 *      level segment on a noisy reading.
 * ====================================================================== */
void HAL_ADC_ConvCpltCallback(ADC_HandleTypeDef* hadc)
{
    uint16_t sample;
    uint16_t new_avg;

    if (bq24072_data.adc_settle_pending) {
        /*
         * Discard the very first conversion after each trigger.
         * The STM32 ADC sample-and-hold capacitor may still hold residue
         * from the OFW's last conversion, so the first result is
         * unreliable.  Start a fresh conversion immediately.
         */
        (void)HAL_ADC_GetValue(hadc);
        bq24072_data.adc_settle_pending = false;
        HAL_ADC_Start_IT(hadc);
        return;
    }

    sample = (uint16_t)HAL_ADC_GetValue(hadc);
    bq24072_data.sample_sum   += sample;
    bq24072_data.sample_count += 1u;

    /*
     * Publish only after accumulating 9 samples.
     * OFW: "if (8 < uVar9)" — uVar9 is count AFTER increment,
     * so first publish happens when count reaches 9.
     * CHANGED from original >= 8 to >= 9 to match OFW exactly.
     */
    if (bq24072_data.sample_count >= 9u) {

        new_avg = (uint16_t)(bq24072_data.sample_sum /
                             bq24072_data.sample_count);

        /* Reset window for next cycle (windowed, not running like OFW). */
        bq24072_data.sample_sum   = 0u;
        bq24072_data.sample_count = 0u;

        /* Store raw average — OFW: *(puVar3+0x08). */
        bq24072_data.value = new_avg;

        /*
         * OFW monotonicity filter (FUN_00001b58, DAT_00003408+0x1c):
         *
         *   First ever publish: seed the floor unconditionally.
         *
         *   Subsequent publishes:
         *     - new_avg <= floor  -> normal discharge, accept and lower floor.
         *     - adc_reset_pending -> charger event; allow floor to rise once.
         *     - new_avg  > floor  -> ADC noise / ripple; silently keep floor.
         *
         *   adc_reset_pending is always cleared after this block.
         *   OFW: puVar3[3] = 0.
         */
        if (!bq24072_data.sample_valid) {
            /* First publish: unconditionally seed filtered_value. */
            bq24072_data.filtered_value = new_avg;
        } else if ((new_avg <= bq24072_data.filtered_value) ||
                    bq24072_data.adc_reset_pending) {
            /* Accept: discharge step or explicit charger-state reset. */
            bq24072_data.filtered_value = new_avg;
        }
        /* else: new_avg > floor without reset — silently keep floor. */

        bq24072_data.adc_reset_pending = false;  /* OFW: puVar3[3] = 0  */
        bq24072_data.sample_valid      = true;   /* OFW: puVar3[2] = 1  */

#if BQ24072_PROFILING == 1
        bq24072_battery_value      = bq24072_data.value;
        bq24072_filtered_value_dbg = bq24072_data.filtered_value;
#endif

        HAL_ADC_Stop_IT(hadc);
        /* else: keep accumulating into the current window. */
    }
}

/* ======================================================================
 * Initialisation
 * ====================================================================== */
int32_t bq24072_init(void)
{
    bq24072_data.value              = 0u;
    bq24072_data.filtered_value     = 0u;
    bq24072_data.adc_reset_pending  = false;
    bq24072_data.adc_settle_pending = false;
    bq24072_data.sample_valid       = false;
    bq24072_data.sample_count       = 0u;
    bq24072_data.sample_sum         = 0u;
    bq24072_data.last.initialized   = false;

    bq24072_handle_power_good();
    bq24072_handle_charging();
    bq24072_poll();

    HAL_TIM_Base_Start_IT(&htim1);

    return 0;
}

/* ======================================================================
 * GPIO interrupt management
 * ====================================================================== */
void bq24072_interrupts_enable(void)
{
    GPIO_InitTypeDef GPIO_InitStruct = {0};

    GPIO_InitStruct.Pin  = GPIO_CHARGER_POWERGOOD_Pin;
    GPIO_InitStruct.Mode = GPIO_MODE_IT_RISING_FALLING;
    GPIO_InitStruct.Pull = GPIO_NOPULL;
    HAL_GPIO_Init(GPIO_CHARGER_POWERGOOD_GPIO_Port, &GPIO_InitStruct);

    GPIO_InitStruct.Pin  = GPIO_CHARGER_CHARGING_Pin;
    GPIO_InitStruct.Mode = GPIO_MODE_IT_RISING_FALLING;
    GPIO_InitStruct.Pull = GPIO_NOPULL;
    HAL_GPIO_Init(GPIO_CHARGER_CHARGING_GPIO_Port, &GPIO_InitStruct);
}

void bq24072_interrupts_disable(void)
{
    HAL_GPIO_DeInit(GPIO_CHARGER_POWERGOOD_GPIO_Port, GPIO_CHARGER_POWERGOOD_Pin);
    HAL_GPIO_DeInit(GPIO_CHARGER_CHARGING_GPIO_Port,  GPIO_CHARGER_CHARGING_Pin);
}

/* ======================================================================
 * Power-good / charging state handlers
 *
 * When the charger cable is plugged or unplugged the battery voltage
 * changes direction (starts rising or falling).  The ADC floor stored
 * in filtered_value would block any upward movement after a charger
 * plug event, so we must set adc_reset_pending to allow the floor to
 * track the new voltage.
 *
 * This mirrors the OFW mechanism where an external event sets
 * DAT_00003408+0x03 (puVar3[3]) before the next ADC callback fires.
 * ====================================================================== */
void bq24072_handle_power_good(void)
{
    bool new_pg = !(HAL_GPIO_ReadPin(bq_pins[BQ24072_PIN_PGOOD].bank,
                                     bq_pins[BQ24072_PIN_PGOOD].pin)
                    == GPIO_PIN_SET);

    if (new_pg != bq24072_data.power_good) {
        /* State changed: allow filtered_value to move upward once. */
        bq24072_data.adc_reset_pending = true;
    }
    bq24072_data.power_good = new_pg;
}

void bq24072_handle_charging(void)
{
    bool new_chg = !(HAL_GPIO_ReadPin(bq_pins[BQ24072_PIN_CHG].bank,
                                      bq_pins[BQ24072_PIN_CHG].pin)
                     == GPIO_PIN_SET);

    if (new_chg != bq24072_data.charging) {
        /* State changed: allow filtered_value to move upward once. */
        bq24072_data.adc_reset_pending = true;
    }
    bq24072_data.charging = new_chg;
}

/* ======================================================================
 * State query
 * ====================================================================== */
bq24072_state_t bq24072_get_state(void)
{
    if (bq24072_data.power_good) {
        return bq24072_data.charging ? BQ24072_STATE_CHARGING
                                     : BQ24072_STATE_FULL;
    }
    return (!bq24072_data.charging) ? BQ24072_STATE_DISCHARGING
                                    : BQ24072_STATE_MISSING;
}

/* ======================================================================
 * Percent computation
 *
 * IMPORTANT: uses filtered_value (OFW: DAT_00003408+0x14), NOT value
 * (raw average).  The monotonicity filter in the ADC callback ensures
 * that filtered_value only moves downward while discharging, so the
 * table lookup always lands in the correct (or conservatively lower)
 * segment — consistent with OFW FUN_00003148 / FUN_0000320e behaviour.
 * ====================================================================== */
int bq24072_get_percent(void)
{
    bq24072_state_t              state;
    const bq24072_level_table_t* table;
    int                          percent;

    state = bq24072_get_state();
    if (state == BQ24072_STATE_MISSING) return 0;

    table   = bq24072_select_table(bq24072_data.filtered_value, state);
    percent = bq24072_percent_from_table(bq24072_data.filtered_value, table);

    /* Keep level helper reachable; useful for correlating with OFW logs
     * that report the 0-5 level (FUN_000032d2 / *DAT_00003408). */
    (void)bq24072_level_from_table(bq24072_data.filtered_value, table);

    if (percent < 0)   return 0;
    if (percent > 100) return 100;
    return percent;
}

/*
 * Rate-limited UI percent.
 *
 * Applies a slow step-limiter (±1 %/poll) on top of the ADC-level
 * monotonicity filter to avoid the icon flickering during a single
 * ADC window.  The discharge-rise guard here is a secondary safety net;
 * the primary enforcement is already done in HAL_ADC_ConvCpltCallback.
 */
int bq24072_get_percent_filtered(void)
{
    int             percent;
    int             delta;
    bq24072_state_t state;
    const int       snap_threshold = 5;
    const int       step           = 1;

    if (!bq24072_data.sample_valid) {
        return bq24072_data.last.initialized ? bq24072_data.last.percent : 0;
    }

    state   = bq24072_get_state();
    percent = bq24072_get_percent();

    if (!bq24072_data.last.initialized) {
        bq24072_data.last.initialized = true;
        bq24072_data.last.state       = state;
        bq24072_data.last.percent     = percent;
        return percent;
    }

    if (state != bq24072_data.last.state) {
        /* State transition: snap immediately to new reading. */
        bq24072_data.last.state   = state;
        bq24072_data.last.percent = percent;
        return percent;
    }

    switch (state) {

        case BQ24072_STATE_MISSING:
            bq24072_data.last.percent = 0;
            return 0;

        case BQ24072_STATE_FULL:
            bq24072_data.last.percent = percent;
            return percent;

        case BQ24072_STATE_CHARGING:
        case BQ24072_STATE_DISCHARGING:
            delta = percent - bq24072_data.last.percent;

            /*
             * Belt-and-suspenders: the ADC filter already prevents rises
             * while discharging, but guard here too for corner cases such
             * as a state change mid-window.
             */
            if ((state == BQ24072_STATE_DISCHARGING) && (delta > 0))
                return bq24072_data.last.percent;

            if ((delta >= snap_threshold) || (delta <= -snap_threshold)) {
                /* Large deviation from filtered value: trust it immediately.
                 * This recovers from a bad boot sample quickly. */
                bq24072_data.last.percent = percent;
            } else if (delta > 0) {
                bq24072_data.last.percent += step;
            } else if (delta < 0) {
                bq24072_data.last.percent -= step;
            }
            return bq24072_data.last.percent;

        default:
            break;
    }

    return percent;
}

/* ======================================================================
 * Poll: trigger a new ADC conversion window
 * ====================================================================== */
void bq24072_poll(void)
{
    bq24072_data.adc_settle_pending = true;
    HAL_ADC_Start_IT(&hadc1);
}
