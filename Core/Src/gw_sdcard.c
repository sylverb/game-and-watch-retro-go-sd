#include "gw_flash.h"
#include "gw_linker.h"
#include "gw_lcd.h"
#include "gw_sleep.h"
#include <string.h>
#include <time.h>
#include <stdio.h>
#include "stm32h7xx.h"

#include "main.h"
#include "bitmaps.h"
#include "odroid_colors.h"
#include "odroid_overlay.h"
#include "odroid_input.h"
#include "config.h"
#include "gui.h"
#include "error_screens.h"
#include "ff.h"
#include "gw_sdcard.h"

bool fs_mounted = false;
static FRESULT cause;

static void (*s_sd_io_poll)(void);

void sd_io_set_poll(void (*fn)(void))
{
    s_sd_io_poll = fn;
}

void sd_io_poll(void)
{
    if (s_sd_io_poll)
        s_sd_io_poll();
}

void sdcard_error_screen(void) {
    lcd_backlight_set(180);
    
    char buf[64];
    int idle_s = uptime_get();

    switch (cause) {
        case FR_NOT_READY:
            draw_error_screen("No SD CARD found", "Insert SD Card", "Press any key to power off");
            break;
        default:
            draw_error_screen("SD CARD ERROR", "Unable to mount SD Card", "Press any key to power off");
            break;
    }

    while (1)
    {
        odroid_gamepad_state_t joystick;
        wdog_refresh();
        int steps = uptime_get() - idle_s;
        sprintf(buf, "%ds to sleep", 600 - steps);
        odroid_overlay_draw_text_line(4, 29 * 8 - 4, strlen(buf) * 8, buf, C_RED, curr_colors->bg_c);

        lcd_sync();
        lcd_swap();
        //lcd_wait_for_vblank();
        HAL_Delay(10);
        if (steps >= 600)
            break;
        odroid_input_read_gamepad(&joystick);
        if (joystick.values[ODROID_INPUT_POWER] || joystick.values[ODROID_INPUT_A] || joystick.values[ODROID_INPUT_B]){
            break;
        }
    }

    GW_EnterDeepSleep(true, NULL, NULL);
}

/*************************
 * SD Card Public API *
 *************************/

/**
 * Initialize SD card and mount the filesystem.
 */
FATFS FatFs;  // Fatfs handle
void sdcard_init(void) {
    // Check if SD Card is connected to SPI1
    sdcard_init_spi1();
    sdcard_hw_type = SDCARD_HW_SPI1;
    cause = f_mount(&FatFs, (const TCHAR *)"", 1);
    if (cause == FR_OK) {
        printf("filesytem mounted.\n");
        fs_mounted = true;
        return;
    } else {
        sdcard_deinit_spi1();
    }

    // Check if SD Card is connected over OSPI1
    sdcard_init_ospi1();
    sdcard_hw_type = SDCARD_HW_OSPI1;
    cause = f_mount(&FatFs, (const TCHAR *)"", 1);
    if (cause == FR_OK) {
        printf("filesytem mounted.\n");
        fs_mounted = true;
        return;
    } else {
        sdcard_deinit_ospi1();
    }

    // No SD Card detected
    sdcard_hw_type = SDCARD_HW_NO_SD_FOUND;
}

void sdcard_deinit(void) {
    if (fs_mounted) {
      f_unmount("");
      fs_mounted = false;
    }
    switch (sdcard_hw_type) {
      case SDCARD_HW_SPI1:
        sdcard_deinit_spi1();
        break;
      case SDCARD_HW_OSPI1:
        sdcard_deinit_ospi1();
        break;
      default:
        break;
    }
}

void sdcard_init_spi1() {
    // PeriphClkInitStruct.PeriphClockSelection = RCC_PERIPHCLK_SPI1 should be set
    // but as it's common with SPI2, it's already selected
    GPIO_InitTypeDef GPIO_InitStruct = {0};

    /*Configure GPIO pin Output Level */
    /* PA15 = 0v : Disable SD Card VCC */
    HAL_GPIO_WritePin(SD_VCC_GPIO_Port, SD_VCC_Pin, GPIO_PIN_RESET);

    /*Configure GPIO pin Output Level */
    /* PB9 = 0v : SD Card disable CS  */
    HAL_GPIO_WritePin(SD_CS_GPIO_Port, SD_CS_Pin, GPIO_PIN_SET);

    /*Configure GPIO pin : PA15 to control SD Card VCC */
    GPIO_InitStruct.Pin = SD_VCC_Pin;
    GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;
    GPIO_InitStruct.Pull = GPIO_NOPULL;
    GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;
    HAL_GPIO_Init(SD_VCC_GPIO_Port, &GPIO_InitStruct);

    /*Configure GPIO pin : PB9 SD Card CS */
    GPIO_InitStruct.Pin = SD_CS_Pin;
    GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;
    GPIO_InitStruct.Pull = GPIO_NOPULL;
    GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;
    HAL_GPIO_Init(SD_CS_GPIO_Port, &GPIO_InitStruct);

    // Reset sd card by setting VCC to 0 for 5ms
    HAL_Delay(5);

    /* PA15 = 0v : Enable SD Card VCC */
    HAL_GPIO_WritePin(SD_VCC_GPIO_Port, SD_VCC_Pin, GPIO_PIN_SET);

    MX_SPI1_Init();

    HAL_SPI_MspInit(&hspi1);    
}

void sdcard_deinit_spi1() {
    /* SD Card disable CS  */
    HAL_GPIO_WritePin(SD_CS_GPIO_Port, SD_CS_Pin, GPIO_PIN_SET);

    /* 8 clock cycles with CS high so the card releases MISO before we shut down SPI */
    uint8_t dummy = 0xFF;
    HAL_SPI_Transmit(&hspi1, &dummy, 1, 100);

    // Deinit SPI1
    HAL_SPI_DeInit(&hspi1);
    HAL_SPI_MspDeInit(&hspi1);

    // Disable SD Card VCC
    HAL_GPIO_WritePin(SD_VCC_GPIO_Port, SD_VCC_Pin, GPIO_PIN_RESET);

    // We can now put CS signal to 0 to prevent power consumption
    // if system goes to sleep mode.
    HAL_GPIO_WritePin(SD_CS_GPIO_Port, SD_CS_Pin, GPIO_PIN_RESET);
  }

void sdcard_init_ospi1() {
}

void sdcard_deinit_ospi1() {
}

void switch_ospi_gpio(uint8_t ToOspi) {
  static uint8_t IsOspi = true;
  GPIO_InitTypeDef GPIO_InitStruct = {0};

  if (IsOspi == ToOspi)
    return;

  if (ToOspi) {
    if (HAL_OSPI_Init(&hospi1) != HAL_OK)
      Error_Handler();
  } else {
    HAL_OSPI_DeInit(&hospi1);

    /*Configure GPIO pin Output Level */
    HAL_GPIO_WritePin(GPIOE, GPIO_FLASH_NCS_Pin, GPIO_PIN_RESET);
    HAL_GPIO_WritePin(GPIOB, GPIO_FLASH_MOSI_Pin|GPIO_FLASH_CLK_Pin, GPIO_PIN_RESET);

    /*Configure GPIO pin : GPIO_FLASH_NCS_Pin */
    GPIO_InitStruct.Pin = GPIO_FLASH_NCS_Pin;
    GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;
    GPIO_InitStruct.Pull = GPIO_NOPULL;
    GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_VERY_HIGH;
    HAL_GPIO_Init(GPIO_FLASH_NCS_GPIO_Port, &GPIO_InitStruct);

    /*Configure GPIO pins : GPIO_FLASH_MOSI_Pin GPIO_FLASH_CLK_Pin */
    GPIO_InitStruct.Pin = GPIO_FLASH_MOSI_Pin|GPIO_FLASH_CLK_Pin;
    GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;
    GPIO_InitStruct.Pull = GPIO_NOPULL;
    GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_VERY_HIGH;
    HAL_GPIO_Init(GPIOB, &GPIO_InitStruct);

    /*Configure GPIO pins : GPIO_FLASH_MISO_Pin */
    GPIO_InitStruct.Pin = GPIO_FLASH_MISO_Pin;
    GPIO_InitStruct.Mode = GPIO_MODE_INPUT;
    GPIO_InitStruct.Pull = GPIO_PULLUP;
    HAL_GPIO_Init(GPIO_FLASH_MISO_GPIO_Port, &GPIO_InitStruct);
  }

  IsOspi = ToOspi;
}