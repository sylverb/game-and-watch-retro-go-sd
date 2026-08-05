#pragma once

#include <odroid_sdcard.h>
#include <stdint.h>
#include <stdbool.h>

#if !defined(COVERFLOW)
#define COVERFLOW 0
#endif /* COVERFLOW */
#if !defined (CHEAT_CODES)
#define CHEAT_CODES 0
#endif

typedef enum
{
    REGION_NTSC = 0,
    REGION_PAL,
    REGION_SECAM,
    REGION_NTSC50,
    REGION_PAL60,
    REGION_AUTO
} rom_region_t;

typedef struct rom_system_t rom_system_t;

typedef enum
{
    IMG_STATE_UNKNOWN,
    IMG_STATE_NO_COVER,
    IMG_STATE_COVER
} img_state_t;

typedef struct {
    char name[256];
    const char *ext;
    char path[256];
    uint8_t *address;
    uint32_t size;
	#if COVERFLOW != 0
    const uint8_t *img_address;
    img_state_t img_state;
	#endif
    rom_region_t region;
    const rom_system_t *system;
#if CHEAT_CODES == 1
    char** cheat_codes; // Cheat codes to choose from
    char** cheat_descs; // Cheat codes descriptions
    int cheat_count;
#endif
} retro_emulator_file_t;

bool rg_rom_list_arg_is_parent(const void *arg);

typedef struct {
    char system_name[32];
    char dirname[16];
    char exts[32];
	#if COVERFLOW != 0
    size_t cover_width;
    size_t cover_height;
	#endif
    struct {
        retro_emulator_file_t *files;
        int count;
        int maxcount;
    } roms;
    /** Relative path under ROM dir. */
    char browse_subpath[96];
    bool initialized;
    rom_system_t *system;

    /* Non-empty for a dynamically-discovered external core (see
     * emulators_scan_cores() / gnw_core_meta_t): path of the .bin on the
     * SD card, and the code/bss sizes read from its embedded metadata.
     * Empty for the compile-time tabs (Homebrew, PICO-8). */
    char core_path[64];
    uint32_t core_code_size;
    uint32_t core_bss_size;
} retro_emulator_t;


void emulators_init();
void rg_emulators_restore_main_menu_browse_path(void);
void emulator_init(retro_emulator_t *emu);
void emulator_refresh_list(retro_emulator_t *emu);
void emulator_start(retro_emulator_file_t *file, bool load_state, bool start_paused, int8_t save_slot);
bool emulator_show_file_menu(retro_emulator_file_t *file);
void emulator_show_file_info(retro_emulator_file_t *file);
void emulator_crc32_file(retro_emulator_file_t *file);
bool emulator_build_file_object(const char *path, retro_emulator_file_t *out_file);
const char *emu_get_file_path(retro_emulator_file_t *file);
retro_emulator_t *file_to_emu(retro_emulator_file_t *file);
bool emulator_is_file_valid(retro_emulator_file_t *file);
retro_emulator_file_t *emulator_get_file(char *file_path);
