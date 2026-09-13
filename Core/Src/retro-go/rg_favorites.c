/* SD-file favorites: the ★ tab (tab 0) + favorites.txt under
 * ODROID_BASE_PATH_CONFIG. See favorites.h for the design constraints
 * (file IO on discrete events only, zero resident RAM, list materialized
 * into the shared per-tab ROM buffer). */
#include <odroid_system.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>

#include "rg_emulators.h"
#include "rg_utils.h"
#include "favorites.h"
#include "rg_storage.h"
#include "rg_i18n.h"
#include "gw_malloc.h"
#include "bitmaps.h"
#include "gui.h"
#include "ff.h" /* f_unlink/f_rename: newlib rename() has no syscall here */

#define FAVORITES_FILE ODROID_BASE_PATH_CONFIG "/favorites.txt"
/* Temp for the rewrite-on-remove; committed with f_rename so a mid-write
 * power cut can never corrupt the live file. */
#define FAVORITES_TMP  ODROID_BASE_PATH_CONFIG "/favorites.new"

/* Pseudo-emulator behind the ★ tab (tab->arg must be a retro_emulator_t for
 * the generic launcher code that casts it, e.g. gui_save_current_tab).
 * ahb_calloc'd like the tab_t structs
 */
static retro_emulator_t *favorites_emu;

/** fgets + strip trailing CR/LF. Returns false at EOF/error. */
static bool read_favorite_line(FILE *f, char *buf, size_t size)
{
    if (fgets(buf, size, f) == NULL)
        return false;
    buf[strcspn(buf, "\r\n")] = '\0';
    return true;
}

bool rg_favorites_contains(const char *path)
{
    FILE *f = fopen(FAVORITES_FILE, "r");
    if (f == NULL)
        return false;

    char line[RG_PATH_MAX + 1];
    bool found = false;
    while (!found && read_favorite_line(f, line, sizeof(line)))
        found = (strcmp(line, path) == 0);
    fclose(f);
    return found;
}

/* Add appends instead of rewriting: it never touches the existing lines, and
 * a truncated tail line (power cut mid-append) is simply skipped by the
 * parser because it no longer names an existing /roms file. */
bool rg_favorites_add(const char *path)
{
    if (rg_favorites_contains(path))
        return true;

    FILE *f = fopen(FAVORITES_FILE, "a");
    if (f == NULL) {
        printf("favorites: cannot open %s for append\n", FAVORITES_FILE);
        return false;
    }
    bool ok = fprintf(f, "%s\n", path) > 0;
    fclose(f);
    return ok;
}

bool rg_favorites_remove(const char *path)
{
    FILE *in = fopen(FAVORITES_FILE, "r");
    if (in == NULL)
        return false;
    FILE *out = fopen(FAVORITES_TMP, "w");
    if (out == NULL) {
        fclose(in);
        printf("favorites: cannot open %s for rewrite\n", FAVORITES_TMP);
        return false;
    }

    char line[RG_PATH_MAX + 1];
    bool ok = true;
    while (ok && read_favorite_line(in, line, sizeof(line))) {
        if (line[0] == '\0' || strcmp(line, path) == 0)
            continue; /* drop the removed entry (and blank lines) */
        ok = fprintf(out, "%s\n", line) > 0;
    }
    fclose(in);
    fclose(out);

    if (!ok) {
        f_unlink(FAVORITES_TMP);
        return false;
    }
    f_unlink(FAVORITES_FILE); /* may not exist; f_rename needs the name free */
    return f_rename(FAVORITES_TMP, FAVORITES_FILE) == FR_OK;
}

bool rg_favorites_reset(void)
{
    FRESULT res = f_unlink(FAVORITES_FILE);
    return res == FR_OK || res == FR_NO_FILE;
}

/** Map "/roms/<dirname>/..." or "/homebrews/..." to its registered system. */
static const rom_system_t *system_for_path(const char *path)
{
    static const char roms_prefix[] = RG_BASE_PATH_ROMS "/";
    static const char hb_prefix[] = RG_BASE_PATH_HOMEBREWS "/";
    const size_t roms_prefix_len = sizeof(roms_prefix) - 1;
    const size_t hb_prefix_len = sizeof(hb_prefix) - 1;

    if (strncmp(path, hb_prefix, hb_prefix_len) == 0)
        return rg_emulators_system_for_dir("homebrew", strlen("homebrew"));

    if (strncmp(path, roms_prefix, roms_prefix_len) != 0)
        return NULL;
    const char *dirname = path + roms_prefix_len;
    const char *slash = strchr(dirname, '/');
    if (slash == NULL || slash == dirname)
        return NULL;
    size_t dlen = (size_t)(slash - dirname);
    /* Legacy /roms/homebrew/ — homebrews live at /homebrews/ only. */
    if (dlen == 8 && strncmp(dirname, "homebrew", 8) == 0)
        return NULL;
    return rg_emulators_system_for_dir(dirname, dlen);
}

/** Build one launchable list entry from a favorite path (mirrors the file
 * branch of rg_emulators.c scan_folder_cb). */
static void fill_file_slot(retro_emulator_file_t *slot, const char *path,
                           const rom_system_t *system, size_t size)
{
    memset(slot, 0, sizeof(*slot));
    slot->size = size;
    slot->system = system;
    slot->region = REGION_NTSC;
    strncpy(slot->path, path, sizeof(slot->path) - 1);
    slot->path[sizeof(slot->path) - 1] = '\0';

    /* name/ext derive from slot->path (NOT the caller's line buffer — these
     * pointers must stay valid for the lifetime of the list entry). */
    const char *basename = rg_basename(slot->path);
    const char *dot = strrchr(basename, '.');
    size_t name_len = dot ? (size_t)(dot - basename) : strlen(basename);
    if (name_len > sizeof(slot->name) - 1)
        name_len = sizeof(slot->name) - 1;
    memcpy(slot->name, basename, name_len);
    slot->name[name_len] = '\0';
    slot->ext = dot ? dot + 1 : NULL;
#if COVERFLOW != 0
    slot->img_state = IMG_STATE_UNKNOWN;
#endif
}

/** Materialize /favorites.txt into files[]; returns the entry count.
 * Stale lines (ROM gone, unknown system) are hidden but kept in the file so
 * an SD hiccup can't silently destroy the list. */
static int favorites_fill_files(retro_emulator_file_t *files, int maxcount)
{
    FILE *f = fopen(FAVORITES_FILE, "r");
    if (f == NULL)
        return 0;

    char line[RG_PATH_MAX + 1];
    int count = 0;
    while (count < maxcount && read_favorite_line(f, line, sizeof(line))) {
        const rom_system_t *system = system_for_path(line);
        if (system == NULL)
            continue;
        rg_stat_t st = rg_storage_stat(line);
        if (!st.exists || !st.is_file)
            continue;
        fill_file_slot(&files[count], line, system, st.size);
        count++;
    }
    if (count == maxcount && !feof(f))
        printf("favorites: list view capped at %d entries\n", maxcount);
    fclose(f);
    return count;
}

static int favorites_name_cmp(const void *a, const void *b)
{
    return strcasecmp(((const retro_emulator_file_t *)a)->name,
                      ((const retro_emulator_file_t *)b)->name);
}

/** (Re)load the file and rebuild the tab's listbox. */
static void favorites_refresh_tab(tab_t *tab)
{
    int maxcount = 0;
    retro_emulator_file_t *files = rg_emulators_shared_file_buffer(&maxcount);
    if (files == NULL)
        return;

    int n = favorites_fill_files(files, maxcount);

    /* Same ordering rule as the emulator tabs: alphabetical by name. */
    if (n > 1) {
        qsort(files, (size_t)n, sizeof(*files), favorites_name_cmp);
        for (int i = 0; i < n; i++) /* ext points into path, which moved */
            if (files[i].ext != NULL)
                files[i].ext = strrchr(files[i].path, '.') + 1;
    }

    favorites_emu->roms.files = files;
    favorites_emu->roms.count = n;
    favorites_emu->roms.maxcount = maxcount;

    if (n > 0) {
        snprintf(tab->status, sizeof(tab->status), "%s", curr_lang->s_favorite);
        gui_resize_list(tab, n);
        for (int i = 0; i < n; i++) {
            tab->listbox.items[i].text = files[i].name;
            tab->listbox.items[i].arg = &files[i];
        }
        if (tab->listbox.cursor > tab->listbox.length - 1)
            tab->listbox.cursor = tab->listbox.length - 1;
        if (tab->listbox.cursor < 0)
            tab->listbox.cursor = 0;
        tab->is_empty = false;
    } else {
        /* Mark empty so navigation / boot skip this tab. */
        gui_resize_list(tab, 0);
        tab->is_empty = true;
    }
}

/** If the ★ tab just became empty while selected, jump to the next system. */
static void favorites_leave_if_empty(tab_t *tab)
{
    if (!tab->is_empty)
        return;
    if (gui_get_current_tab() != tab)
        return;
    gui_change_tab(+1);
}

static void favorites_event_handler(gui_event_t event, tab_t *tab)
{
    if (event == TAB_INIT || event == TAB_REFRESH_LIST) {
        favorites_refresh_tab(tab);
        return;
    }

    listbox_item_t *item = gui_get_selected_item(tab);
    retro_emulator_file_t *file = item ? (retro_emulator_file_t *)item->arg : NULL;
    if (file == NULL)
        return;

    if (event == KEY_PRESS_A) {
        emulator_show_file_menu(file);
        /* The menu can un-favorite (or reset) — rebuild from the file. */
        favorites_refresh_tab(tab);
        favorites_leave_if_empty(tab);
    }
    else if (event == KEY_PRESS_B) {
        emulator_show_file_info(file);
        if (file->path[0] == '\0') { /* ROM was deleted from the info dialog */
            favorites_refresh_tab(tab);
            favorites_leave_if_empty(tab);
        }
    }
}

void rg_favorites_register_tab(void)
{
    favorites_emu = ahb_calloc(1, sizeof(*favorites_emu));
    strcpy(favorites_emu->system_name, "Favorites");
    strcpy(favorites_emu->dirname, "favorites");
    /* The shared file buffer does not exist yet (first add_emulator call
     * allocates it); favorites_refresh_tab fetches it lazily at TAB_INIT. */
    /* Star icon on the right (pad slot); no text header. */
    gui_add_tab("favorites", RG_LOGO_HEADER_FAVORITES, RG_LOGO_EMPTY,
                favorites_emu, favorites_event_handler);
}

bool rg_favorites_is_current_tab(void)
{
    tab_t *tab = gui_get_current_tab();
    return tab != NULL && favorites_emu != NULL && tab->arg == (void *)favorites_emu;
}
