#ifndef _GW_SDCARD_H_
#define _GW_SDCARD_H_

#include "stm32h7xx_hal.h"

extern bool fs_mounted;

void sdcard_init(void);
void sdcard_deinit(void);
void sdcard_error_screen(void);

void sdcard_init_spi1();
void sdcard_deinit_spi1();
void sdcard_init_ospi1();
void sdcard_deinit_ospi1();
void switch_ospi_gpio(uint8_t ToOspi);

/* Optional: called from HW-SPI DMA wait loops so a homebrew (video) can keep
 * feeding its PCM ring while FatFs blocks on a sector. Soft-SPI ignores this.
 * Callback MUST NOT call FatFs / fread / f_* — not reentrant. */
void sd_io_set_poll(void (*fn)(void));
void sd_io_poll(void);
/* RAM_EMU bump rewind: drop SPI DMA bounce pointers so the next SD read
 * reallocates them (only cores/homebrews that actually read the card pay). */
void sd_io_on_ram_init(void);

#endif
