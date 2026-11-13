#ifndef _GW_BUTTONS_H_
#define _GW_BUTTONS_H_

#include <stdint.h>

#define B_Left (1 << 0)
#define B_Up (1 << 1)
#define B_Right (1 << 2)
#define B_Down (1 << 3)
#define B_A (1 << 4)
#define B_B (1 << 5)
#define B_TIME (1 << 6)
#define B_GAME (1 << 7)
#define B_PAUSE (1 << 8)
#define B_POWER (1 << 9)
#define B_START (1 << 10)
#define B_SELECT (1 << 11)

uint32_t buttons_get();

#ifdef REMOTE_INPUT
/* End of AHB .persistent pad (linker __remote_input__). Keep MPU
 * non-cacheable — see MPU_Config region 7 in main.c. */
extern uint8_t __remote_input__[];
#define SRAM_REMOTE_INPUT_ADDR ((uint32_t)__remote_input__)
#endif

#endif