#pragma once

#include <stdbool.h>
#include <stddef.h>
#include "rg_emulators.h"

/* --- SD-file favorites (zero resident RAM) ------------------------------
 * One favorite per line in ODROID_BASE_PATH_CONFIG "/favorites.txt"
 * (i.e. /data/favorites.txt): the full ROM path ("/roms/<system>/<file>").
 * The file is read only on discrete UI events (A-menu open, favorites-tab
 * entry) — NEVER in the list-render hot path. The favorites tab materializes
 * its list into the same shared ROM buffer every emulator tab already
 * reuses, so the feature costs no extra RAM.
 * Implemented in Core/Src/retro-go/rg_favorites.c. */

/** True if path is favorited (one full read of /favorites.txt). */
bool rg_favorites_contains(const char *path);
/** Append path (no-op if already present). */
bool rg_favorites_add(const char *path);
/** Rewrite the file without path (temp file + rename, never in-place). */
bool rg_favorites_remove(const char *path);
/** Delete all favorites. */
bool rg_favorites_reset(void);
/** Register the ★ tab; MUST be the first tab added (tab index 0). */
void rg_favorites_register_tab(void);
/** True when the launcher is currently showing the favorites tab. */
bool rg_favorites_is_current_tab(void);

/* Bridges into rg_emulators.c internals the favorites tab borrows. */
/** The shared per-tab ROM list buffer (NULL before emulators_init). */
retro_emulator_file_t *rg_emulators_shared_file_buffer(int *maxcount);
/** System descriptor for a /roms/<dirname> system, or NULL. */
const rom_system_t *rg_emulators_system_for_dir(const char *dirname, size_t len);
