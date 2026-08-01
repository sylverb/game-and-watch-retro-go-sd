#include <assert.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>

#include "odroid_system.h"
#include "odroid_settings.h"
#include "main.h"
#include "rg_i18n.h"
#include "rg_storage.h"
#include "appid.h"
#include "gui.h"
#include "rom_manager.h"
#include "gw_sdcard.h"

#define CONFIG_MAGIC 0xcafef00d
#define ODROID_APPID_COUNT 4

#if !defined  (COVERFLOW)
  #define COVERFLOW 0
#endif /* COVERFLOW */
// Global
#if !defined (CHEAT_CODES)
#define CHEAT_CODES 0
#endif
#if !defined (CODEPAGE)
#define CODEPAGE 1252
#endif
#if !defined (UICODEPAGE)
#define UICODEPAGE 1252
#endif
static const char* Key_RomFilePath  = "RomFilePath";
static const char* Key_AudioSink    = "AudioSink";

// Per-app
static const char* Key_DispRotation = "DistRotation";

typedef struct app_config {
    uint8_t region;
    uint8_t palette;
    uint8_t disp_scaling;
    uint8_t disp_filter;
    uint8_t disp_overscan;
    uint8_t sprite_limit;
} app_config_t;

#if CHEAT_CODES == 1
typedef struct {
    char game_path[RG_PATH_MAX + 1];
    uint32_t active_cheat_codes;
    bool is_cached;
} CheatCache;

static CheatCache cheat_cache = {{0}, 0, false};
#endif

typedef struct persistent_config {
    uint32_t magic;
    uint8_t version;

    uint8_t backlight;
    uint8_t start_action;
    uint8_t volume;
    uint8_t font_size;
    uint8_t theme;
    uint8_t colors;
    uint8_t turbo_buttons;
    uint8_t font;
    uint8_t lang;
    uint8_t startup_app;
    uint8_t cpu_oc_level;
    char    startup_file[256];

    uint16_t main_menu_timeout_s;
    uint16_t main_menu_selected_tab;
    uint16_t main_menu_cursor;
    char main_menu_browse_subpath[96];

    bool debug_clock_always_on;

    /** Welcome prompt: 0 = not anchored, 1 = message already shown, else YYYYMMDD anchor (RTC >= 2026). */
    uint32_t welcome_prompt;

    app_config_t app[APPID_COUNT];

    uint32_t crc32;
} persistent_config_t;

static const persistent_config_t persistent_config_default = {
    .magic = CONFIG_MAGIC,
    .version = 8,

    .backlight = ODROID_BACKLIGHT_LEVEL6,
    .start_action = ODROID_START_ACTION_RESUME,
    .volume = ODROID_AUDIO_VOLUME_MAX / 2, // Too high volume can cause brown out if the battery isn't connected.
    .font_size = 8,
    .theme = 2, //use as theme index
    .colors = 0,
    .turbo_buttons = 0,
    .font = 0,
#if CODEPAGE==12521
    .lang = 1,
#elif CODEPAGE==12522
    .lang = 2,
#elif CODEPAGE==12523
    .lang = 3,
#elif CODEPAGE==12524
    .lang = 4,
#elif CODEPAGE==12525
    .lang = 5,
#elif CODEPAGE==12526
    .lang = 6,
#elif CODEPAGE==12511
    .lang = 7,
#elif CODEPAGE==932
    .lang = 11,
#elif CODEPAGE==936
    .lang = 8,
#elif CODEPAGE==949
    .lang = 10,
#elif CODEPAGE==950
    .lang = 9,
#else
    .lang = 0,
#endif
    .startup_app = 0,
    .cpu_oc_level = 0,
    .main_menu_timeout_s = 60 * 10, // Turn off after 10 minutes of idle time in the main menu
    .main_menu_selected_tab = 0,
    .main_menu_cursor = 0,
    .main_menu_browse_subpath = {0},
    .debug_clock_always_on = false,
    .welcome_prompt = 0,
    .app = {
        {0}, // Launcher
        {
            .region = 0,
            .palette = 2,
            .disp_scaling = ODROID_DISPLAY_SCALING_FULL,
            .disp_filter = ODROID_DISPLAY_FILTER_SHARP,
            .disp_overscan = 0,
            .sprite_limit = 0,
        }, // GB
        {
            .disp_scaling = ODROID_DISPLAY_SCALING_CUSTOM,
            .disp_filter = ODROID_DISPLAY_FILTER_SHARP,
        }, // NES
        {0}, // SMS
        {0}, // PCE
        {0}, // GW
        {0}, // MD Genesis
    },
};

persistent_config_t persistent_config_ram;

static bool file_exists(const char *file_path) {
    struct stat st;
    return stat(file_path, &st) == 0;
}

void odroid_settings_init()
{
    if (fs_mounted && file_exists("/CONFIG")) {
        FILE *file = fopen("/CONFIG", "rb");
        if (file) {
            size_t bytes_read = fread((unsigned char *)&persistent_config_ram, 1, sizeof(persistent_config_t), file);
            fclose(file);
            if (bytes_read != sizeof(persistent_config_t))
                memset(&persistent_config_ram, 0, sizeof(persistent_config_t));
        }
    }

    if (persistent_config_ram.magic != CONFIG_MAGIC) {
        printf("Config: Magic mismatch. Expected 0x%08x, got 0x%08lx\n", CONFIG_MAGIC, persistent_config_ram.magic);
        odroid_settings_reset();
        return;
    }

    if (persistent_config_ram.version != persistent_config_default.version) {
        printf("Config: New config version, resetting settings.\n");
        odroid_settings_reset();
        return;
    }

    // Calculate crc32 of the whole struct with the crc32 value set to 0
    uint32_t loaded_crc32 = persistent_config_ram.crc32;
    persistent_config_ram.crc32 = 0;
    persistent_config_ram.crc32 = crc32_le(0, (unsigned char *) &persistent_config_ram, sizeof(persistent_config_t));

    if (persistent_config_ram.crc32 != loaded_crc32) {
        printf("Config: CRC32 mismatch. Expected 0x%08lx, got 0x%08lx\n", persistent_config_ram.crc32, loaded_crc32);
        odroid_settings_reset();
        return;
    }
    //set colors;
    curr_colors = (colors_t *)(&gui_colors[persistent_config_ram.colors]);
    gui_apply_colors_to_overlay_clut();
    //set font
    set_font(odroid_settings_font_get());
    //set lang — load strings from /lang/xx_xx.bin on SD, fall back to
    //  baked lang_en_us if the file is missing or corrupt.
    curr_lang = i18n_load_language(odroid_settings_lang_get());
}

void odroid_settings_commit()
{
    // Calculate crc32 of the whole struct with the crc32 value set to 0
    persistent_config_ram.crc32 = 0;
    persistent_config_ram.crc32 = crc32_le(0, (unsigned char *) &persistent_config_ram, sizeof(persistent_config_t));

    if (fs_mounted) {
        FILE *file = fopen("/CONFIG", "wb");
        if (file) {
            fwrite((const void *)&persistent_config_ram, 1, sizeof(persistent_config_t), file);
            fclose(file);
        }
    }

    /* Settings dialog has just closed. Re-load the committed language
     * (no-op / already active when the user left the picker on the
     * same idx; otherwise swaps the single SD language slot). */
    curr_lang = i18n_load_language(odroid_settings_lang_get());
}

#if CHEAT_CODES == 1
static int delete_cheat_state_file_cb(const rg_scandir_t *file, void *arg)
{
    (void)arg;

    if (file->is_file) {
        const char *ext = strrchr(file->basename, '.');
        if (ext && strcmp(ext, ".state") == 0)
            rg_storage_delete(file->path);
    }

    return RG_SCANDIR_CONTINUE;
}

static void odroid_settings_delete_cheat_state_files()
{
    rg_storage_scandir(ODROID_BASE_PATH_SAVES, delete_cheat_state_file_cb, NULL,
                       RG_SCANDIR_FILES | RG_SCANDIR_RECURSIVE);
}
#endif

void odroid_settings_reset()
{
#if CHEAT_CODES == 1
    // Delete all cheat state files
    odroid_settings_delete_cheat_state_files();
    // Reset cheat cache
    cheat_cache.is_cached = false;
    cheat_cache.game_path[0] = '\0';
    cheat_cache.active_cheat_codes = 0;
#endif
    memcpy(&persistent_config_ram, &persistent_config_default, sizeof(persistent_config_t));

    odroid_settings_commit();
}

char* odroid_settings_string_get(const char *key, const char *default_value)
{
    return (char *) default_value;
}

void odroid_settings_string_set(const char *key, const char *value)
{
}

int32_t odroid_settings_int32_get(const char *key, int32_t default_value)
{
    return default_value;
}

void odroid_settings_int32_set(const char *key, int32_t value)
{
}

void odroid_settings_cpu_oc_level_set(uint8_t oc)
{
    oc = (oc < 0) ? 0 : ((oc > 2) ? 2 : oc);
    persistent_config_ram.cpu_oc_level = oc;
}

uint8_t odroid_settings_cpu_oc_level_get(void)
{
    return persistent_config_ram.cpu_oc_level;
}

int8_t odroid_settings_colors_get()
{
    int colors = persistent_config_ram.colors;
    if (colors < 0)
        persistent_config_ram.colors = 0;
    else if (colors >= gui_colors_count)
        persistent_config_ram.colors = gui_colors_count - 1;
    return persistent_config_ram.colors;
}

void odroid_settings_colors_set(int8_t colors)
{
    if (colors < 0)
        colors = 0;
    else if (colors >= gui_colors_count)
        colors = gui_colors_count - 1;
    persistent_config_ram.colors = colors;
}


int8_t odroid_settings_font_get()
{
    int font = persistent_config_ram.font;
    if (font < 0)
        persistent_config_ram.font = 0;
    else if (font >= gui_font_count)
        persistent_config_ram.font = gui_font_count - 1;
    return persistent_config_ram.font;
}

void odroid_settings_font_set(int8_t font)
{
    if (font < 0)
        font = 0;
    else if (font >= gui_font_count)
        font = gui_font_count - 1;
    persistent_config_ram.font = font;
}

int8_t odroid_settings_turbo_buttons_get()
{
    int turbo_buttons = persistent_config_ram.turbo_buttons;
    if (turbo_buttons < 0)
        persistent_config_ram.turbo_buttons = 0;
    else if (turbo_buttons >= 3)
        persistent_config_ram.turbo_buttons = 3;
    return persistent_config_ram.turbo_buttons;
}

void odroid_settings_turbo_buttons_set(int8_t turbo_buttons)
{
    if (turbo_buttons < 0)
        turbo_buttons = 0;
    else if (turbo_buttons >= 3)
        turbo_buttons = 3;
    persistent_config_ram.turbo_buttons = turbo_buttons;
}


int8_t odroid_settings_get_next_lang(uint8_t cur)
{
    /* gui_lang[] removed — all entries were non-NULL (only present-when-
     * INCLUDED languages were in the array), so the original while-loop
     * always exited on its first iteration. Reduce to simple wrap. */
    int ret = cur + 1;
    if (ret >= gui_lang_count)
        ret = 0;
    return ret;
}

int8_t odroid_settings_get_prior_lang(uint8_t cur)
{
    int ret = (int)cur - 1;
    if (ret < 0)
        ret = gui_lang_count - 1;
    return ret;
}

int8_t odroid_settings_lang_get()
{
    int lang = persistent_config_ram.lang;
    if(lang >= gui_lang_count){
        // This can happen if a language is set, then the device is reflashed with fewer languages.
        lang = 0;
    }
    return odroid_settings_get_prior_lang(lang + 1);
}


void odroid_settings_lang_set(int8_t lang)
{
    if (lang < 0)
        lang = 0;
    else if (lang >= gui_lang_count)
        lang = gui_lang_count - 1;
    persistent_config_ram.lang = lang;
}

#if COVERFLOW != 0
int8_t odroid_settings_theme_get()
{
    int theme = persistent_config_ram.theme;
    if (theme < 0)
        persistent_config_ram.theme = 0;
    else if (theme > 4)
        persistent_config_ram.theme = 4;
    return persistent_config_ram.theme;
}
void odroid_settings_theme_set(int8_t theme)
{
    if (theme < 0)
        theme = 0;
    else if (theme > 4)
        theme = 4;
    persistent_config_ram.theme = theme;
}
#endif

int32_t odroid_settings_app_int32_get(const char *key, int32_t default_value)
{
    return default_value;
}

void odroid_settings_app_int32_set(const char *key, int32_t value)
{
    char app_key[16];
    sprintf(app_key, "%.12s.%ld", key, odroid_system_get_app()->id);
    odroid_settings_int32_set(app_key, value);
}


int32_t odroid_settings_FontSize_get()
{
    return persistent_config_ram.font_size;
}
void odroid_settings_FontSize_set(int32_t value)
{
    persistent_config_ram.font_size = value;
}

/*
char* odroid_settings_RomFilePath_get()
{
    static char filepath_buffer[FS_MAX_PATH_SIZE];  // Being static is fine since the name is immediately used.
    snprintf(filepath_buffer,
             sizeof(filepath_buffer),
             "%s/%s.savestate",
             ACTIVE_FILE->system->system_name,
             ACTIVE_FILE->name);
    return filepath_buffer;
}
*/
void odroid_settings_RomFilePath_set(const char* value)
{
  odroid_settings_string_set(Key_RomFilePath, value);
}


int32_t odroid_settings_Volume_get()
{
    return persistent_config_ram.volume;
}
void odroid_settings_Volume_set(int32_t value)
{
    persistent_config_ram.volume = value;
}


int32_t odroid_settings_AudioSink_get()
{
  return odroid_settings_int32_get(Key_AudioSink, ODROID_AUDIO_SINK_SPEAKER);
}
void odroid_settings_AudioSink_set(int32_t value)
{
  odroid_settings_int32_set(Key_AudioSink, value);
}



int32_t odroid_settings_Backlight_get()
{
    return persistent_config_ram.backlight;
}
void odroid_settings_Backlight_set(int32_t value)
{
    persistent_config_ram.backlight = value;
}


ODROID_START_ACTION odroid_settings_StartAction_get()
{
    return persistent_config_ram.start_action;
}
void odroid_settings_StartAction_set(ODROID_START_ACTION value)
{
    persistent_config_ram.start_action = value;
}


int32_t odroid_settings_StartupApp_get()
{
    return persistent_config_ram.startup_app;
}
void odroid_settings_StartupApp_set(int32_t value)
{
    persistent_config_ram.startup_app = value;
}


char* odroid_settings_StartupFile_get()
{
    return persistent_config_ram.startup_file;
}
void odroid_settings_StartupFile_set(retro_emulator_file_t *file)
{
    // We save only file path and we'll try to find it in built list at next startup
    if (file)
    {
        memcpy(&persistent_config_ram.startup_file, file->path, sizeof(persistent_config_ram.startup_file));
    }
    else
    {
        memset(&persistent_config_ram.startup_file,0,sizeof(persistent_config_ram.startup_file));
    }
}

uint16_t odroid_settings_MainMenuTimeoutS_get()
{
    return ((MIN(persistent_config_ram.main_menu_timeout_s, 3600) + 59) / 60) * 60; // > 0 : Round to whole minutes max one hour
}
void odroid_settings_MainMenuTimeoutS_set(uint16_t value)
{
    persistent_config_ram.main_menu_timeout_s = value;
}

uint16_t odroid_settings_MainMenuSelectedTab_get()
{
    return persistent_config_ram.main_menu_selected_tab;
}
void odroid_settings_MainMenuSelectedTab_set(uint16_t value)
{
    persistent_config_ram.main_menu_selected_tab = value;
}

uint16_t odroid_settings_MainMenuCursor_get()
{
    return persistent_config_ram.main_menu_cursor;
}
void odroid_settings_MainMenuCursor_set(uint16_t value)
{
    persistent_config_ram.main_menu_cursor = value;
}

void odroid_settings_MainMenuBrowseSubpath_set(const char *subpath)
{
    if (subpath && subpath[0]) {
        strncpy(persistent_config_ram.main_menu_browse_subpath,
                subpath,
                sizeof(persistent_config_ram.main_menu_browse_subpath) - 1);
        persistent_config_ram.main_menu_browse_subpath[sizeof(persistent_config_ram.main_menu_browse_subpath) - 1] = '\0';
    } else {
        persistent_config_ram.main_menu_browse_subpath[0] = '\0';
    }
}

bool odroid_settings_MainMenuBrowseSubpath_get(char *buf, size_t buf_size)
{
    if (buf == NULL || buf_size == 0)
        return false;

    strncpy(buf, persistent_config_ram.main_menu_browse_subpath, buf_size - 1);
    buf[buf_size - 1] = '\0';
    return buf[0] != '\0';
}


int32_t odroid_settings_Palette_get()
{
    return persistent_config_ram.app[odroid_system_get_app()->id].palette;
}
void odroid_settings_Palette_set(int32_t value)
{
    persistent_config_ram.app[odroid_system_get_app()->id].palette = value;
}


int32_t odroid_settings_SpriteLimit_get()
{
    return persistent_config_ram.app[odroid_system_get_app()->id].sprite_limit;
}
void odroid_settings_SpriteLimit_set(int32_t value)
{
    persistent_config_ram.app[odroid_system_get_app()->id].sprite_limit = value;
}


ODROID_REGION odroid_settings_Region_get()
{
    return persistent_config_ram.app[odroid_system_get_app()->id].region;
}
void odroid_settings_Region_set(ODROID_REGION value)
{
    persistent_config_ram.app[odroid_system_get_app()->id].region = value;
}


int32_t odroid_settings_DisplayScaling_get()
{
    return persistent_config_ram.app[odroid_system_get_app()->id].disp_scaling;
}
void odroid_settings_DisplayScaling_set(int32_t value)
{
    persistent_config_ram.app[odroid_system_get_app()->id].disp_scaling = value;
}


int32_t odroid_settings_DisplayFilter_get()
{
    return persistent_config_ram.app[odroid_system_get_app()->id].disp_filter;
}
void odroid_settings_DisplayFilter_set(int32_t value)
{
    persistent_config_ram.app[odroid_system_get_app()->id].disp_filter = value;
}


int32_t odroid_settings_DisplayRotation_get()
{
  return odroid_settings_app_int32_get(Key_DispRotation, ODROID_DISPLAY_ROTATION_AUTO);
}
void odroid_settings_DisplayRotation_set(int32_t value)
{
  odroid_settings_app_int32_set(Key_DispRotation, value);
}


int32_t odroid_settings_DisplayOverscan_get()
{
    return persistent_config_ram.app[odroid_system_get_app()->id].disp_overscan;
}
void odroid_settings_DisplayOverscan_set(int32_t value)
{
    persistent_config_ram.app[odroid_system_get_app()->id].disp_overscan = value;
}


#if CHEAT_CODES == 1

static uint32_t read_active_cheats(char *game_path) {
    if (cheat_cache.is_cached && cheat_cache.game_path[0] && strcmp(cheat_cache.game_path, game_path) == 0) {
        return cheat_cache.active_cheat_codes;
    }

    uint32_t active_cheat_codes = 0;
    char *cheat_state_path = odroid_system_get_path(ODROID_PATH_CHEAT_STATE, game_path);
    if (odroid_sdcard_get_filesize(cheat_state_path) > 0) {
        FILE *cheat_state_file = fopen(cheat_state_path, "r");
        if (!cheat_state_file) {
            printf("Failed to open cheat state file %s\n", cheat_state_path);
            free(cheat_state_path);
            return 0;
        }
        fread(&active_cheat_codes, 1, sizeof(uint32_t), cheat_state_file);
        fclose(cheat_state_file);
    }
    free(cheat_state_path);

    // Update cache
    strncpy(cheat_cache.game_path, game_path, RG_PATH_MAX);
    cheat_cache.game_path[RG_PATH_MAX] = '\0';
    cheat_cache.active_cheat_codes = active_cheat_codes;
    cheat_cache.is_cached = true;

    return active_cheat_codes;
}

static void write_active_cheats(char *game_path, uint32_t active_cheat_codes) {
    char *cheat_state_path = odroid_system_get_path(ODROID_PATH_CHEAT_STATE, game_path);
    FILE *cheat_state_file = fopen(cheat_state_path, "w");
    if (!cheat_state_file) {
        printf("Failed to open cheat state file %s\n", cheat_state_path);
        free(cheat_state_path);
        return;
    }
    fwrite(&active_cheat_codes, 1, sizeof(uint32_t), cheat_state_file);
    fclose(cheat_state_file);
    free(cheat_state_path);

    // Update cache
    strncpy(cheat_cache.game_path, game_path, RG_PATH_MAX);
    cheat_cache.game_path[RG_PATH_MAX] = '\0';
    cheat_cache.active_cheat_codes = active_cheat_codes;
    cheat_cache.is_cached = true;
}

bool odroid_settings_ActiveGameGenieCodes_is_enabled(char *game_path, int code_index) {
    if (code_index > MAX_CHEAT_CODES) {
        return false;
    }

    return ((read_active_cheats(game_path) >> code_index) & 0x1) == 1;
}

bool odroid_settings_ActiveGameGenieCodes_set(char *game_path, int code_index, bool enable) {
    if (code_index > MAX_CHEAT_CODES) {
        return false;
    }

    uint32_t active_cheat_codes = read_active_cheats(game_path);
    if (enable) {
        active_cheat_codes |= (1 << code_index);
    } else {
        active_cheat_codes &= ~(1 << code_index);
    }
    write_active_cheats(game_path, active_cheat_codes);

    return true;
}
#endif

bool odroid_settings_DebugMenuDebugClockAlwaysOn_get()
{
    return persistent_config_ram.debug_clock_always_on;
}
void odroid_settings_DebugMenuDebugClockAlwaysOn_set(bool value)
{
    persistent_config_ram.debug_clock_always_on = value;
}

uint32_t odroid_settings_WelcomePrompt_get(void)
{
    return persistent_config_ram.welcome_prompt;
}

void odroid_settings_WelcomePrompt_set(uint32_t value)
{
    persistent_config_ram.welcome_prompt = value;
}

