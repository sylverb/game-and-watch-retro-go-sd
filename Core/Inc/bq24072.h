#pragma once
#ifndef BQ24072_H
#define BQ24072_H

#include <stdbool.h>
#include <stdint.h>

typedef enum {
    BQ24072_STATE_MISSING,
    BQ24072_STATE_CHARGING,
    BQ24072_STATE_DISCHARGING,
    BQ24072_STATE_FULL,
} bq24072_state_t;

int32_t bq24072_init(void);

void bq24072_interrupts_enable(void);
void bq24072_interrupts_disable(void);

void bq24072_handle_power_good(void);
void bq24072_handle_charging(void);

bq24072_state_t bq24072_get_state(void);
int bq24072_get_level(void);
int bq24072_get_percent(void);
int bq24072_get_percent_filtered(void);
/* Stock PC8 strap: 0 = high-count ADC tables, 1 = low-count tables. */
uint8_t bq24072_get_board_rev(void);

/*
 * Under-load empty-pack gate (stock sub_80062b4 intent). Does not overwrite
 * a healthy HUD latch with the backlight IR-drop trough.
 */
void bq24072_boot_resample(void);

/*
 * After STOP2: clear low-batt debounce and arm allow_rise.
 * Call before bringing LCD/SD back; sample with bq24072_wake_resample() once
 * those loads are on so the HUD matches a cold boot, not rested OCV.
 */
void bq24072_wake_prepare(void);

/*
 * Latch one conversion into the HUD (allow_rise). Prefer calling this after
 * backlight/SD resume; also used when cancelling low-batt power-off on charger.
 */
void bq24072_wake_resample(void);

/*
 * Stock Zelda 0x800ee42 / Mario sub_80062b4: rare HUD latch at mode entry.
 * Skips while /PG asserted. Call when launching a game (not every frame).
 */
void bq24072_mode_entry_resample(void);

/* Stock Mario: refuse boot when bar == 0 and not charging. */
bool bq24072_should_block_boot(void);
/* Stock Mario: after ~5s with bar < 2 while discharging, power off. */
bool bq24072_should_power_off(void);
/* True when USB PGOOD or /CHG is asserted (charger present). */
bool bq24072_charger_connected(void);
/* If should_power_off(), show low-batt icon then standby (returns if charger plugged during hold). */
void bq24072_enforce_low_battery(void);
/*
 * True when discharging with live level < 2 (same band as runtime low-batt).
 * SD init draws a large pulse — call before touching the card or the SoC browns out.
 */
bool bq24072_battery_too_low_for_sd(void);

void bq24072_poll(void);

#endif // BQ24072_H
