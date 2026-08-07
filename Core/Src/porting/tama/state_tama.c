#include <stdio.h>
#include <string.h>

#include "crc32.h"
#include "rg_rtc.h"
#include "state_tama.h"
#include "tamalib.h"

#include "gw_core_bridge.h"

#define STATE_VERSION 1

// Full state of the emulator except inputs & buzzer state
typedef struct
{
    // Header
    char magic[4];
    uint8_t version;
    uint64_t save_time;

    // Registers
    u13_t pc;
    u12_t x;
    u12_t y;
    u4_t a;
    u4_t b;
    u5_t np;
    u8_t sp;
    u4_t flags;

    // Timers
    u64_t tick_counter;
    u64_t clk_timer_timestamp;
    u64_t prog_timer_timestamp;
    bool_t prog_timer_enabled;
    u8_t prog_timer_data;
    u8_t prog_timer_rld;
    // Debug
    u32_t call_depth;

    // Memory
    MEM_BUFFER_TYPE memory[MEM_BUFFER_SIZE];

    // Interrupts
    interrupt_t interrupts[INT_SLOT_NUM];

    // CRC
    unsigned int crc32;
} tama_state_t;

bool tama_state_save(const uint8_t *ptr) {
    state_t *state = tamalib_get_state();

    // Initialize save state
    tama_state_t save_state;
    memset(&save_state, 0, sizeof(save_state));

    // Create header
    save_state.magic[0] = 'T';
    save_state.magic[1] = 'A';
    save_state.magic[2] = 'M';
    save_state.magic[3] = 'A';
    save_state.version = STATE_VERSION;
    save_state.save_time = GW_GetCurrentMillis();

    // Copy registers
    save_state.pc = *state->pc;
    save_state.x = *state->x;
    save_state.y = *state->y;
    save_state.a = *state->a;
    save_state.b = *state->b;
    save_state.np = *state->np;
    save_state.sp = *state->sp;
    save_state.flags = *state->flags;

    // Copy debug state
    save_state.call_depth = *state->call_depth;

    // Copy counters
    save_state.tick_counter = *state->tick_counter;

    // Copy timers
    save_state.clk_timer_timestamp = *state->clk_timer_timestamp;
    save_state.prog_timer_timestamp = *state->prog_timer_timestamp;
    save_state.prog_timer_enabled = *state->prog_timer_enabled;
    save_state.prog_timer_data = *state->prog_timer_data;
    save_state.prog_timer_rld = *state->prog_timer_rld;

    // Copy memory
    memcpy(save_state.memory, state->memory, sizeof(save_state.memory));

    // Copy interrupts
    memcpy(save_state.interrupts, state->interrupts, sizeof(save_state.interrupts));

    // Calculate checksum
    save_state.crc32 = crc32_le(0, (unsigned char *) &save_state, sizeof(save_state));

    printf("tama_state_save crc32: %d\n", save_state.crc32);

    // Save ram
    memcpy((void *) ptr, (const uint8_t *) &save_state, sizeof(save_state));

    return true;
}

bool tama_state_load(const uint8_t *flash_ptr) {
    tama_state_t save_state;

    // Make a writable clone
    memcpy(&save_state, (tama_state_t *) (flash_ptr), sizeof(save_state));

    // Validate header
    if (save_state.magic[0] != 'T' ||
        save_state.magic[1] != 'A' ||
        save_state.magic[2] != 'M' ||
        save_state.magic[3] != 'A') {
        printf("tama_state_load header magic failed\n");
        return false;
    }

    if (save_state.version != STATE_VERSION) {
        printf("tama_state_load version failed, expected: %d, actual %d \n", STATE_VERSION, save_state.version);
        return false;
    }

    unsigned int expected_crc32 = save_state.crc32;
    save_state.crc32 = 0; // CRC32 was calculated on the entire struct including this field
    unsigned int actual_crc32 = crc32_le(0, (unsigned char *) &save_state, sizeof(save_state));
    if (expected_crc32 != actual_crc32) {
        printf("tama_state_load crc32 failed, expected: %d, actual: %d\n", expected_crc32, actual_crc32);
        return false;
    }

    // Restore state
    state_t *state = tamalib_get_state();

    // Copy save timestamp
    *state->save_time = save_state.save_time;

    // Copy registers
    *state->pc = save_state.pc;
    *state->x = save_state.x;
    *state->y = save_state.y;
    *state->a = save_state.a;
    *state->b = save_state.b;
    *state->np = save_state.np;
    *state->sp = save_state.sp;
    *state->flags = save_state.flags;

    // Copy debug state
    *state->call_depth = save_state.call_depth;

    // Copy counters
    *state->tick_counter = save_state.tick_counter;

    // Copy timers
    *state->clk_timer_timestamp = save_state.clk_timer_timestamp;
    *state->prog_timer_timestamp = save_state.prog_timer_timestamp;
    *state->prog_timer_enabled = save_state.prog_timer_enabled;
    *state->prog_timer_data = save_state.prog_timer_data;
    *state->prog_timer_rld = save_state.prog_timer_rld;

    // Copy memory
    memcpy(state->memory, save_state.memory, sizeof(save_state.memory));

    // Copy interrupts
    memcpy(state->interrupts, save_state.interrupts, sizeof(save_state.interrupts));

    return true;
}
