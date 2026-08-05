#pragma once

#include <stdint.h>

#include "rg_emulators.h"
#if !defined(COVERFLOW)
#define COVERFLOW 0
#endif /* COVERFLOW */

typedef enum {
    NO_GAME_DATA,
    GAME_DATA,
    GAME_DATA_BYTESWAP_16
} game_data_type_t;

struct rom_system_t {
    char *system_name;
    retro_emulator_file_t *roms;
    char *extension;
	#if COVERFLOW != 0
    size_t cover_width;
    size_t cover_height;
	#endif    
    uint32_t roms_count;
    game_data_type_t game_data_type;

    /* Mirrors retro_emulator_t.core_path/core_*_size for the owning
     * emulator, aliased (not copied) so it survives the AHB-pool reset in
     * emulator_start(). NULL/"" for compile-time tabs. */
    const char *core_path;
    uint32_t core_code_size;
    uint32_t core_bss_size;
};

typedef struct {
    const rom_system_t **systems;
    uint32_t systems_count;
} rom_manager_t;

extern const rom_manager_t rom_mgr;
extern const unsigned char *ROM_DATA;
extern const char *ROM_EXT;
extern unsigned ROM_DATA_LENGTH;
extern retro_emulator_file_t *ACTIVE_FILE;

const rom_system_t *rom_manager_system(const rom_manager_t *mgr, char *name);
int   rom_get_ext_count(const rom_system_t *system, char *ext);
const retro_emulator_file_t *rom_get_ext_file_at_index(const rom_system_t *system, char *ext, int index);
int rom_get_index_for_file_ext(const rom_system_t *system, retro_emulator_file_t *file);
void  rom_manager_set_active_file(retro_emulator_file_t *file);
const retro_emulator_file_t *rom_manager_get_file(const rom_system_t *system, const char *name);
