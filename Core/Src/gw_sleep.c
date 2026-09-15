#include "gw_sleep.h"

#include <stdio.h>
#include "stm32h7xx_hal.h"
#include "main.h"
#include "bq24072.h"
#include "odroid_settings.h"
#include "odroid_display.h"
#include "odroid_audio.h"
#include "gw_buttons.h"
#include "gw_lcd.h"
#include "gw_audio.h"
#if SD_CARD == 1
#include "gw_sdcard.h"
#include "ff.h"
#endif

extern LTDC_HandleTypeDef hltdc;
extern SPI_HandleTypeDef hspi2;
extern TIM_HandleTypeDef htim1;

static void SleepModeEnterAndResume(sleep_pre_wakeup_callback_t pre_wakeup_callback, sleep_post_wakeup_callback_t post_wakeup_callback) {
  printf("[Sleep] Entering STOP2 mode\n");

  // Prevent charger interrupts from waking up the device
  bq24072_interrupts_disable();

  HAL_PWREx_ClearWakeupFlag(PWR_FLAG_WKUP1);
  HAL_TIM_Base_Stop_IT(&htim1);
  HAL_PWREx_EnterSTOP2Mode(PWR_LOWPOWERREGULATOR_ON, PWR_STOPENTRY_WFI);
  wdog_refresh();
  printf("[Sleep] Waking up\n");

  // Restore clocks
  SystemClock_Config(odroid_settings_cpu_oc_level_get());
  HAL_TIM_Base_Start_IT(&htim1);
  HAL_ResumeTick();

  // Restore charger
  bq24072_interrupts_enable();
  bq24072_handle_charging();
  bq24072_handle_power_good();

  // Restore LCD
  lcd_backlight_off();
  if (lcd_get_last_refresh_rate() != 60) {
    lcd_set_refresh_rate(lcd_get_last_refresh_rate());
  }
  lcd_init(&hspi2, &hltdc, 0);

  if (pre_wakeup_callback != NULL) {
    pre_wakeup_callback();
  }

  lcd_swap();

  // We want to keep this fade-in short because while it happens,
  // we can't do other things like restarting audio or SD...
  app_animate_lcd_brightness(0, odroid_display_get_backlight_raw(), 3);

#if SD_CARD == 1
  sdcard_init();
  if (fs_mounted == false) {
      sdcard_error_screen();
  }

  // Check if update file is present and reboot so bootloader can pick it up.
  // We do it here to reduce sleep resume delay in typical case
  #define UPDATE_ARCHIVE_FILE "/retro-go_update.bin"
  FIL update_file;
  FRESULT update_file_res = f_open(&update_file, UPDATE_ARCHIVE_FILE, FA_READ);
  if (update_file_res == FR_OK) {
      f_close(&update_file);

      sdcard_deinit();

      while(1) {
        HAL_NVIC_SystemReset();
      }
  }
#endif

  // Restore audio
  odroid_audio_init(odroid_audio_sample_rate_get());
  audio_start_playing_full_length(audio_get_buffer_full_length());
  HAL_GPIO_WritePin(GPIO_Speaker_enable_GPIO_Port, GPIO_Speaker_enable_Pin, GPIO_PIN_SET);

  if (post_wakeup_callback != NULL) {
    post_wakeup_callback();
  }

  printf("[Sleep] Finish waking up\n");
}

void GW_EnterDeepSleep(bool standby, sleep_pre_wakeup_callback_t pre_wakeup_callback, sleep_post_wakeup_callback_t post_wakeup_callback) {
  // Turn off speaker
  HAL_GPIO_WritePin(GPIO_Speaker_enable_GPIO_Port, GPIO_Speaker_enable_Pin, GPIO_PIN_RESET);

  // Stop SAI DMA (audio)
  audio_stop_playing();

  // Deinit the LCD, save power.
  lcd_backlight_off();
  lcd_deinit(&hspi2);

  // Enable wakup by PIN1, the power button
  HAL_PWR_EnableWakeUpPin(PWR_WAKEUP_PIN1_LOW);

#if SD_CARD == 1
  // Unmount Fs and Deinit SD Card if needed
  sdcard_deinit();
#endif

  // Delay 500ms to give us a chance to attach a debugger in case
  // we end up in a suspend-loop.
  for (int i = 0; i < 10; i++) {
    wdog_refresh();
    HAL_Delay(50);
  }

  if (standby) {
    // Leave a trace in RAM that we entered standby mode
    boot_magic_set(BOOT_MAGIC_STANDBY);

    HAL_PWREx_ClearWakeupFlag(PWR_FLAG_WKUP1);
    HAL_PWR_EnterSTANDBYMode();

    // Execution stops here, this function will not return
    while(1) {
      // If we for some reason survive until here, let's just reboot
      HAL_NVIC_SystemReset();
    }
  }
  else {
    SleepModeEnterAndResume(pre_wakeup_callback, post_wakeup_callback);
  }
}