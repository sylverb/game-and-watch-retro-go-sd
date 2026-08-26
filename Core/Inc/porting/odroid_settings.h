#pragma once

#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>
#include "rg_emulators.h"

typedef enum
{
    ODROID_START_ACTION_RESUME = 0,
    ODROID_START_ACTION_NEWGAME,
    ODROID_START_ACTION_NETPLAY
} ODROID_START_ACTION;

typedef enum
{
    ODROID_REGION_AUTO = 0,
    ODROID_REGION_NTSC,
    ODROID_REGION_PAL
} ODROID_REGION;

void odroid_settings_init(void);
void odroid_settings_reset(void);
void odroid_settings_commit(void);

/* Bind per-core / per-homebrew settings to /data/<stem>.cfg (or
 * /data/homebrew/<stem>.cfg). Pass NULL to unbind (launcher). Stem is the
 * .bin basename without extension (e.g. "md" for /cores/md.bin). */
void odroid_settings_bind_core_cfg(const char *core_stem);
void odroid_settings_bind_homebrew_cfg(const char *game_stem);
void odroid_settings_unbind_core_cfg(void);

int32_t odroid_settings_FontSize_get();
void odroid_settings_FontSize_set(int32_t);

int32_t odroid_settings_Volume_get();
void odroid_settings_Volume_set(int32_t value);

char* odroid_settings_RomFilePath_get();
void odroid_settings_RomFilePath_set(const char* value);

int32_t odroid_settings_Backlight_get();
void odroid_settings_Backlight_set(int32_t value);

int32_t odroid_settings_StartupApp_get();
void odroid_settings_StartupApp_set(int32_t value);

char* odroid_settings_StartupFile_get(void);
void odroid_settings_StartupFile_set(retro_emulator_file_t *value);

uint16_t odroid_settings_MainMenuTimeoutS_get(void);
void odroid_settings_MainMenuTimeoutS_set(uint16_t value);

uint16_t odroid_settings_MainMenuSelectedTab_get(void);
void odroid_settings_MainMenuSelectedTab_set(uint16_t value);

uint16_t odroid_settings_MainMenuCursor_get(void);
void odroid_settings_MainMenuCursor_set(uint16_t value);

/** Persisted ROM browser subfolder (relative to system ROM dir); empty = root. */
void odroid_settings_MainMenuBrowseSubpath_set(const char *subpath);
bool odroid_settings_MainMenuBrowseSubpath_get(char *buf, size_t buf_size);

ODROID_START_ACTION odroid_settings_StartAction_get();
void odroid_settings_StartAction_set(ODROID_START_ACTION value);

int32_t odroid_settings_AudioSink_get();
void odroid_settings_AudioSink_set(int32_t value);

ODROID_REGION odroid_settings_Region_get();
void odroid_settings_Region_set(ODROID_REGION value);

int32_t odroid_settings_Palette_get();
void odroid_settings_Palette_set(int32_t value);

int32_t odroid_settings_SpriteLimit_get();
void odroid_settings_SpriteLimit_set(int32_t value);

int32_t odroid_settings_DisplayScaling_get();
void odroid_settings_DisplayScaling_set(int32_t value);

int32_t odroid_settings_DisplayFilter_get();
void odroid_settings_DisplayFilter_set(int32_t value);

int32_t odroid_settings_DisplayRotation_get();
void odroid_settings_DisplayRotation_set(int32_t value);

int32_t odroid_settings_DisplayOverscan_get();
void odroid_settings_DisplayOverscan_set(int32_t value);

#if CHEAT_CODES == 1
bool odroid_settings_ActiveGameGenieCodes_is_enabled(char *game_path, int code_index);
bool odroid_settings_ActiveGameGenieCodes_set(char *game_path, int code_index, bool enable);
#endif

bool odroid_settings_DebugMenuDebugClockAlwaysOn_get();
void odroid_settings_DebugMenuDebugClockAlwaysOn_set(bool value);

/** Welcome prompt state: 0 = not anchored, 1 = shown, else YYYYMMDD anchor date. */
uint32_t odroid_settings_WelcomePrompt_get(void);
void odroid_settings_WelcomePrompt_set(uint32_t value);

/*** Generic functions ***/

void odroid_settings_string_set(const char *key, const char *value);
char* odroid_settings_string_get(const char *key, const char *default_value);

int32_t odroid_settings_int32_get(const char *key, int32_t value_default);
void odroid_settings_int32_set(const char *key, int32_t value);

int32_t odroid_settings_app_int32_get(const char *key, int32_t value_default);
void odroid_settings_app_int32_set(const char *key, int32_t value);

uint8_t odroid_settings_cpu_oc_level_get(void);
void odroid_settings_cpu_oc_level_set(uint8_t oc);
