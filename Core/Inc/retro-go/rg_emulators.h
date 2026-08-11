#pragma once

#include <odroid_sdcard.h>
#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

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
    /* GWHB embedded JPEG: absolute offset/size in the .bin at path.
     * Both 0 → fall back to /covers/<dirname>/<stem>.img.
     * When set, /covers/<dirname>/<stem>.img still wins if present. */
    uint32_t cover_bin_offset;
    uint32_t cover_bin_size;
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
     * SD card. Multiple tabs (systems) can share the same core_path (e.g.
     * PC Engine + PC Engine CD from one pce.bin). Segment sizes are no
     * longer cached here: run_dynamic_core() re-probes the file's
     * gnw_core_meta_t at launch time instead, so this struct doesn't go
     * stale if the file changes between scan and launch.
     * Empty for the compile-time tabs (Homebrew, PICO-8). */
    char core_path[64];

    /* GNW_PARSE_ROM (plain per-file browse, scan_folder_cb) or
     * GNW_PARSE_CDROM (.cue-based folder scan, emulator_scan_cdrom_folder)
     * — see gnw_parse_type_t. Always GNW_PARSE_ROM for compile-time tabs. */
    uint32_t parse_type;

#if CHEAT_CODES == 1
    /* From gnw_core_system_t.cheat_ext; empty = no cheat files for this tab. */
    char cheat_ext[8];
#endif
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

/* Version of the currently running dynamic core (from gnw_core_meta_t),
 * set by run_dynamic_core() at launch. Returns false if no dynamic core
 * is running, or if the packed version is all-zero (unset / old bin). */
bool rg_emulators_get_running_core_version(uint8_t *major, uint8_t *minor, uint8_t *patch);

/* Full Info dialog fields for the running dynamic core. Returns false if
 * no dynamic core is active. `version` is formatted "vX.Y.Z" (or empty if
 * unset). `date` is the core .bin's FatFs mtime "YYYY-MM-DD HH:MM", or
 * "-" if unavailable. Any out_* pointer may be NULL to skip that field. */
bool rg_emulators_get_running_core_info(char *name, size_t name_sz,
                                        char *version, size_t version_sz,
                                        char *path, size_t path_sz,
                                        char *date, size_t date_sz);
