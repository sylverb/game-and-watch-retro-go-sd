/* USER CODE BEGIN Header */
/**
  ******************************************************************************
  * @file           : main.h
  * @brief          : Header for main.c file.
  *                   This file contains the common defines of the application.
  ******************************************************************************
  * @attention
  *
  * <h2><center>&copy; Copyright (c) 2020 STMicroelectronics.
  * All rights reserved.</center></h2>
  *
  * This software component is licensed by ST under BSD 3-Clause license,
  * the "License"; You may not use this file except in compliance with the
  * License. You may obtain a copy of the License at:
  *                        opensource.org/licenses/BSD-3-Clause
  *
  ******************************************************************************
  */
/* USER CODE END Header */

/* Define to prevent recursive inclusion -------------------------------------*/
#ifndef __MAIN_H
#define __MAIN_H

#ifdef __cplusplus
extern "C" {
#endif

#define BOOT_MODE_APP      0
#define BOOT_MODE_WARM     1
#define BOOT_MODE_HOT      2

/* Includes ------------------------------------------------------------------*/
#include <stdbool.h>
#include "stm32h7xx_hal.h"

/* Private includes ----------------------------------------------------------*/
/* USER CODE BEGIN Includes */

/* USER CODE END Includes */

/* Exported types ------------------------------------------------------------*/
/* USER CODE BEGIN ET */

typedef enum {
  BSOD_ABORT,
  BSOD_HARDFAULT,
  BSOD_MEMFAULT,
  BSOD_BUSFAULT,
  BSOD_USAGEFAULT,
  BSOD_WATCHDOG,
  BSOD_OTHER,

  BSOD_COUNT,
} BSOD_t;

typedef enum {
    SDCARD_HW_UNDETECTED,  // No detection done
    SDCARD_HW_NO_SD_FOUND, // No SD detected
    SDCARD_HW_SPI1,        // Tim Schuerewegen design (SPI1)
    SDCARD_HW_OSPI1,       // Yota9 design (soft SPI over OSPI)
} sdcard_hw_type_t;

extern sdcard_hw_type_t sdcard_hw_type;

/* USER CODE END ET */

/* Exported constants --------------------------------------------------------*/
/* USER CODE BEGIN EC */

// hacky
extern RTC_HandleTypeDef hrtc;
extern OSPI_HandleTypeDef hospi1;
extern SPI_HandleTypeDef hspi1;
extern DMA_HandleTypeDef hdma_spi1_tx;
extern DMA_HandleTypeDef hdma_spi1_rx;

/* USER CODE END EC */

/* Exported macro ------------------------------------------------------------*/
/* USER CODE BEGIN EM */

// #define PROFILING_ENABLED
#ifdef PROFILING_ENABLED

#define PROFILING_INIT(_t_name) \
        RTC_TimeTypeDef _t_name##_t0; \
        RTC_TimeTypeDef _t_name##_t1;

#define PROFILING_MEASURE(_t) \
        do { \
          RTC_DateTypeDef _sDate; \
          uint32_t Format = RTC_FORMAT_BIN; \
          HAL_StatusTypeDef ret = HAL_RTC_GetTime(&hrtc, &_t, Format); \
          HAL_RTC_GetDate(&hrtc, &_sDate, Format); \
        } while (0)

#define PROFILING_START(_t_name) PROFILING_MEASURE(_t_name##_t0)
#define PROFILING_END(_t_name) PROFILING_MEASURE(_t_name##_t1)
#define PROFILING_DIFF(_t_name) \
        (\
          (((_t_name##_t1).SecondFraction - (_t_name##_t1).SubSeconds) - ((_t_name##_t0).SecondFraction - (_t_name##_t0).SubSeconds)) \
          + \
          (((_t_name##_t1).Seconds - (_t_name##_t0).Seconds)) \
        )

#else

#define PROFILING_INIT(_t_name)
#define PROFILING_MEASURE(_t)
#define PROFILING_START(_t_name)
#define PROFILING_END(_t_name)
#define PROFILING_DIFF(_t_name)

#endif

/* USER CODE END EM */

/* Exported functions prototypes ---------------------------------------------*/

void Error_Handler(void);
void BSOD(BSOD_t fault, uint32_t pc, uint32_t lr) __attribute__((noreturn));

void boot_magic_set(uint32_t magic);
void SystemClock_Config(uint8_t new_oc_level);
void uptime_inc(void);
uint32_t uptime_get(void);

/* USER CODE BEGIN EFP */
uint32_t GW_GetBootButtons(void);
void wdog_refresh(void);
void MX_SPI1_Init(void);
void app_logo(void);
void app_sleep_logo(void);
void app_sleep_transition(bool show_logo, bool standby);
void app_animate_lcd_brightness(uint8_t initial, uint8_t target, uint8_t step);
uint16_t get_darken_pixel_d(uint16_t color, uint16_t color1, uint16_t darken);
uint16_t get_darken_pixel(uint16_t color, uint16_t darken);
uint16_t get_shined_pixel(uint16_t color, uint16_t shined);
int odroid_overlay_draw_text_line(uint16_t x_pos, uint16_t y_pos, uint16_t width, const char *text, uint16_t color, uint16_t color_bg);


/* USER CODE END EFP */

/* Private defines -----------------------------------------------------------*/
#define GPIO_Speaker_enable_Pin GPIO_PIN_3
#define GPIO_Speaker_enable_GPIO_Port GPIOE
#define BTN_PAUSE_Pin GPIO_PIN_13
#define BTN_PAUSE_GPIO_Port GPIOC
#define BTN_GAME_Pin GPIO_PIN_1
#define BTN_GAME_GPIO_Port GPIOC
#define BTN_PWR_Pin GPIO_PIN_0
#define BTN_PWR_GPIO_Port GPIOA
#define BTN_TIME_Pin GPIO_PIN_5
#define BTN_TIME_GPIO_Port GPIOC
#define BTN_A_Pin GPIO_PIN_9
#define BTN_A_GPIO_Port GPIOD
#define BTN_Left_Pin GPIO_PIN_11
#define BTN_Left_GPIO_Port GPIOD
#define BTN_Down_Pin GPIO_PIN_14
#define BTN_Down_GPIO_Port GPIOD
#define BTN_Right_Pin GPIO_PIN_15
#define BTN_Right_GPIO_Port GPIOD
#define BTN_Up_Pin GPIO_PIN_0
#define BTN_Up_GPIO_Port GPIOD
#define BTN_B_Pin GPIO_PIN_5
#define BTN_B_GPIO_Port GPIOD

#define GPIO_POWER_1V8_Pin GPIO_PIN_1
#define GPIO_POWER_1V8_GPIO_Port GPIOD
#define GPIO_POWER_3V3_Pin GPIO_PIN_4
#define GPIO_POWER_3V3_GPIO_Port GPIOD
#define GPIO_LCD_CS_Pin GPIO_PIN_12
#define GPIO_LCD_CS_GPIO_Port GPIOB
#define GPIO_LCD_RESET_Pin GPIO_PIN_8
#define GPIO_LCD_RESET_GPIO_Port GPIOD

// DAC1_OUT1
#define GPIO_LCD_BRIGHTNESS_1_Pin GPIO_PIN_4
#define GPIO_LCD_BRIGHTNESS_1_GPIO_Port GPIOA

// DAC1_OUT2
#define GPIO_LCD_BRIGHTNESS_2_Pin GPIO_PIN_5
#define GPIO_LCD_BRIGHTNESS_2_GPIO_Port GPIOA

// DAC2_OUT1
#define GPIO_LCD_BRIGHTNESS_3_Pin GPIO_PIN_6
#define GPIO_LCD_BRIGHTNESS_3_GPIO_Port GPIOA

#define GPIO_CHARGER_CE_Pin GPIO_PIN_8
#define GPIO_CHARGER_CE_GPIO_Port GPIOE
#define GPIO_CHARGER_CHARGING_Pin GPIO_PIN_7
#define GPIO_CHARGER_CHARGING_GPIO_Port GPIOE
#define GPIO_CHARGER_CHARGING_EXTI_IRQn EXTI9_5_IRQn
#define GPIO_CHARGER_POWERGOOD_Pin GPIO_PIN_2
#define GPIO_CHARGER_POWERGOOD_GPIO_Port GPIOA
#define GPIO_CHARGER_POWERGOOD_EXTI_IRQn EXTI2_IRQn

// ADC1_INP4
#define GPIO_CHARGER_BAT_LEVEL_Pin GPIO_PIN_4
#define GPIO_CHARGER_BAT_LEVEL_GPIO_Port GPIOC

#define SAI1_FS_Pin GPIO_PIN_4
#define SAI1_FS_GPIO_Port GPIOE
#define SAI1_SCK_Pin GPIO_PIN_5
#define SAI1_SCK_GPIO_Port GPIOE
#define SAI1_SD_Pin GPIO_PIN_6
#define SAI1_SD_GPIO_Port GPIOE

#if SD_CARD == 1
// SPI1 pins
#define SD_SPI_HANDLE hspi1
#define SD_VCC_GPIO_Port GPIOA
#define SD_VCC_Pin GPIO_PIN_15
#define SD_CS_GPIO_Port GPIOB
#define SD_CS_Pin GPIO_PIN_9

// OSPI1 pins
#define GPIO_FLASH_NCS_Pin GPIO_PIN_11
#define GPIO_FLASH_NCS_GPIO_Port GPIOE
#define GPIO_FLASH_MOSI_Pin GPIO_PIN_1
#define GPIO_FLASH_MOSI_GPIO_Port GPIOB
#define GPIO_FLASH_CLK_Pin GPIO_PIN_2
#define GPIO_FLASH_CLK_GPIO_Port GPIOB
#define GPIO_FLASH_MISO_Pin GPIO_PIN_12
#define GPIO_FLASH_MISO_GPIO_Port GPIOD
#endif

// Zelda only buttons; they are not connected on mario.
#define BTN_START_Pin GPIO_PIN_11
#define BTN_START_GPIO_Port GPIOC
#define BTN_SELECT_Pin GPIO_PIN_12
#define BTN_SELECT_GPIO_Port GPIOC

/* USER CODE BEGIN Private defines */

#define BOOT_MAGIC_STANDBY  0xfedebeda
#define BOOT_MAGIC_RESET    0x1fa1afe1
#define BOOT_MAGIC_WATCHDOG 0xd066cafe
#define BOOT_MAGIC_BSOD     0xbad00000
#define BOOT_MAGIC_FLASHAPP 0xf1a5f1a5
#define BOOT_MAGIC_EMULATOR 0x434f5245

#define BOOT_MAGIC_BSOD_MASK 0xffff0000

/* USER CODE END Private defines */

#define GLOBAL_DATA __attribute__((section(".intflash_global")))

/* Reconfigure the MPU regions that cover the LCD pool to mark only
 * `framebuffer_bytes` (starting at 0x24000000) as uncached/non-bufferable.
 * Everything above defaults to cacheable — which is what we want for
 * the LCD bonus area freed when LUT8 shrinks the framebuffer footprint.
 *
 * Supported sizes (hand-decomposed into exactly 4 MPU regions):
 *   300 KB (RGB565, the full LCD pool)
 *   154 KB (LUT8, leaving 146 KB cacheable bonus)
 *
 * Caller is responsible for HAL_MPU_Disable/Enable bracket — the function
 * only writes the region descriptors. */
void mpu_set_lcd_pool_uncached_range(uint32_t framebuffer_bytes);

#ifdef __cplusplus
}
#endif

#endif /* __MAIN_H */

/************************ (C) COPYRIGHT STMicroelectronics *****END OF FILE****/
