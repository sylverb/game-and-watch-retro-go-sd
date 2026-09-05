#pragma once

#include <stdint.h>

#include "rg_emulators.h"
#if !defined(COVERFLOW)
#define COVERFLOW 0
#endif /* COVERFLOW */

struct rom_system_t {
    char *system_name;
    retro_emulator_file_t *roms;
    char *extension;
	#if COVERFLOW != 0
    size_t cover_width;
    size_t cover_height;
	#endif    
    uint32_t roms_count;

    /* Mirrors retro_emulator_t.core_path/parse_type for the owning
     * emulator, aliased (not copied) so it survives the AHB-pool reset in
     * emulator_start(). core_path is NULL/"" for compile-time tabs. */
    const char *core_path;
    uint32_t parse_type;
#if CHEAT_CODES == 1
    /* Aliased to retro_emulator_t.cheat_ext (may be empty). */
    const char *cheat_ext;
#endif
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
