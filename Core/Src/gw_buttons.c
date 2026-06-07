#include "gw_buttons.h"

#include "stm32h7xx_hal.h"
#include "main.h"

#include <stdbool.h>

uint32_t buttons_get() {
    bool left = HAL_GPIO_ReadPin(BTN_Left_GPIO_Port, BTN_Left_Pin) == GPIO_PIN_RESET;
    bool right = HAL_GPIO_ReadPin(BTN_Right_GPIO_Port, BTN_Right_Pin) == GPIO_PIN_RESET;
    bool up = HAL_GPIO_ReadPin(BTN_Up_GPIO_Port, BTN_Up_Pin) == GPIO_PIN_RESET ;
    bool down = HAL_GPIO_ReadPin(BTN_Down_GPIO_Port, BTN_Down_Pin) == GPIO_PIN_RESET;
    bool a = HAL_GPIO_ReadPin(BTN_A_GPIO_Port, BTN_A_Pin) == GPIO_PIN_RESET;
    bool b = HAL_GPIO_ReadPin(BTN_B_GPIO_Port, BTN_B_Pin) == GPIO_PIN_RESET;
    bool time = HAL_GPIO_ReadPin(BTN_TIME_GPIO_Port, BTN_TIME_Pin) == GPIO_PIN_RESET;
    bool game = HAL_GPIO_ReadPin(BTN_GAME_GPIO_Port, BTN_GAME_Pin) == GPIO_PIN_RESET;
    bool pause = HAL_GPIO_ReadPin(BTN_PAUSE_GPIO_Port, BTN_PAUSE_Pin) == GPIO_PIN_RESET;
    bool power = HAL_GPIO_ReadPin(BTN_PWR_GPIO_Port, BTN_PWR_Pin) == GPIO_PIN_RESET;

    bool start = HAL_GPIO_ReadPin(BTN_START_GPIO_Port, BTN_START_Pin) == GPIO_PIN_RESET;
    bool select = HAL_GPIO_ReadPin(BTN_SELECT_GPIO_Port, BTN_SELECT_Pin) == GPIO_PIN_RESET;

    uint32_t buttons = (
        left | (up << 1) | (right << 2) | (down << 3) | (a << 4) | (b << 5) |
        (time << 6) | (game << 7) | (pause << 8) | (power << 9) | (start << 10) | (select << 11)
    );

#ifdef REMOTE_INPUT
    uint32_t remote = *(volatile uint32_t *)SRAM_REMOTE_INPUT_ADDR;
    if (remote & (1 << 0))  buttons |= B_Up;
    if (remote & (1 << 1))  buttons |= B_Down;
    if (remote & (1 << 2))  buttons |= B_Left;
    if (remote & (1 << 3))  buttons |= B_Right;
    if (remote & (1 << 4))  buttons |= B_A;
    if (remote & (1 << 5))  buttons |= B_B;
    if (remote & (1 << 6))  buttons |= B_START;
    if (remote & (1 << 7))  buttons |= B_SELECT;
    if (remote & (1 << 8))  buttons |= B_PAUSE;
    if (remote & (1 << 9))  buttons |= B_GAME;
    if (remote & (1 << 10)) buttons |= B_TIME;
    if (remote & (1 << 11)) buttons |= B_POWER;
#endif

    return buttons;


}
