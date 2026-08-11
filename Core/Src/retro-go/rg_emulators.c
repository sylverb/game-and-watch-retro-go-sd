#include <odroid_system.h>
#include <string.h>
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>

#include "gw_linker.h"
#include "gw_malloc.h"
#include "gw_firmware_abi.h"
#include "gwhb.h"
#include "gnw_core_meta.h"
#include "rg_emulators.h"
#include "rg_storage.h"
#include "rg_i18n.h"
#include "favorites.h"
#include "bitmaps.h"
#include "gui.h"
#include "rom_manager.h"
#include "gw_lcd.h"
#include "main.h"
/* Per-system porting headers (main_gb_tgbdual.h, main_wsv.h, main_gba.h, ...)
 * were removed here while migrating those emulators to standalone
 * cores/<system>/ builds — rg_emulators.c no longer calls their app_main_*
 * entry points directly (see emulators_scan_cores() / run_dynamic_core()). */
#include "rg_rtc.h"
#include "gittag.h"
#include "heap.hpp"
#include "gw_flash.h"
#include "gw_flash_alloc.h"
#if SD_CARD == 0
#include "rg_frogfs.h"
#else
#include "ff.h"
#endif

#define CORE_HEADER_MAGIC_INTERNAL "CORI"
#define CORE_HEADER_MAGIC_EXTERNAL "CORE"
#define CORE_HEADER_MIN_SIZE 8u

static bool gwhb_probe(const char *path, gwhb_meta_t *meta, uint16_t *header_length);

static const char *get_extension(const char *filename);

static uint16_t read_u16_le(const uint8_t *p)
{
  return (uint16_t)(p[0] | ((uint16_t)p[1] << 8));
}

static void show_corrupted_installation_screen(void)
{
  odroid_dialog_choice_t choices[] = {
    {0, curr_lang->s_Corrupted_Install_1, "", -1, NULL},
    {0, curr_lang->s_Corrupted_Install_2, "", -1, NULL},
    ODROID_DIALOG_CHOICE_SEPARATOR,
    {1, curr_lang->s_OK, "", 1, NULL},
    ODROID_DIALOG_CHOICE_LAST,
  };

  (void)odroid_overlay_dialog(curr_lang->s_Corrupted_Title, choices, 3, NULL, 0);
}

const unsigned char *ROM_DATA = NULL;
unsigned ROM_DATA_LENGTH;
const char *ROM_EXT = NULL;
retro_emulator_file_t *ACTIVE_FILE = NULL;

/* Set by run_dynamic_core() from gnw_core_meta_t + core_path; cleared for
 * Homebrew and any non-dynamic launch. */
static char g_running_core_name[24];
static char g_running_core_path[64];
static uint8_t g_running_core_version[3];

static retro_emulator_file_t *shared_files = NULL;

#if !defined(COVERFLOW)
#define COVERFLOW 0
#endif /* COVERFLOW */
/* Builtin launcher systems that are not discovered from /cores/*.bin
 * (Homebrew + Favorites). Capacity for emulators[]/systems[] is sized at boot
 * as BUILTIN_SYSTEM_EMULATORS + systems described by probeable CORE bins. */
#define BUILTIN_SYSTEM_EMULATORS 2
static retro_emulator_t *emulators;
static rom_system_t *systems;
static int emulators_count = 0;
static int emulators_capacity = 0;

#if CHEAT_CODES == 1
static retro_emulator_file_t *CHOSEN_FILE = NULL;
#endif

/* Sentinel "parent" placeholder for the row that says "< /" when navigating
 * inside a subfolder. Stored as a real retro_emulator_file_t so any GUI code
 * that casts item->arg to retro_emulator_file_t* (e.g. cover-drawing) reads a
 * valid struct instead of garbage. img_state pre-set to IMG_STATE_NO_COVER so
 * cover-loading short-circuits without ever calling odroid_system_get_path. */
static retro_emulator_file_t rg_rom_list_parent_placeholder = {
    .ext = NULL,
#if COVERFLOW != 0
    .img_state = IMG_STATE_NO_COVER,
#endif
};

/** Label for list row 0 when inside a ROM subfolder: "< " + basename(parent dir), or "< /" for emulator root. */
static char rg_rom_list_parent_label[48];
static const char *rom_entry_path_segment(const retro_emulator_file_t *f);
static bool browse_subpath_is_safe(const char *s);

static void build_rom_parent_label(const retro_emulator_t *emu)
{
    char tmp[sizeof(emu->browse_subpath)];
    const char *sub = emu->browse_subpath;
    strncpy(tmp, sub, sizeof(tmp) - 1);
    tmp[sizeof(tmp) - 1] = '\0';
    char *last = strrchr(tmp, '/');
    const char *suffix;
    if (last == NULL)
        suffix = "/";
    else
    {
        *last = '\0';
        char *p2 = strrchr(tmp, '/');
        suffix = (p2 == NULL) ? tmp : (p2 + 1);
    }
    snprintf(rg_rom_list_parent_label, sizeof(rg_rom_list_parent_label), "< %s", suffix);
}

bool rg_rom_list_arg_is_parent(const void *arg)
{
    return arg == (void *)&rg_rom_list_parent_placeholder;
}

static void emulator_browse_folder_path(const retro_emulator_t *emu, char *folder, size_t folder_size)
{
    if (emu->browse_subpath[0])
        snprintf(folder, folder_size, "%s/%s/%s", RG_BASE_PATH_ROMS, emu->dirname, emu->browse_subpath);
    else
        snprintf(folder, folder_size, "%s/%s", RG_BASE_PATH_ROMS, emu->dirname);
}

static bool emulator_browse_append(retro_emulator_t *emu, const char *name)
{
    size_t cur = strlen(emu->browse_subpath);
    size_t add = strlen(name);
    if (add == 0 || cur + add + 2 > sizeof(emu->browse_subpath))
        return false;
    if (cur)
    {
        emu->browse_subpath[cur++] = '/';
        emu->browse_subpath[cur] = '\0';
    }
    strcat(emu->browse_subpath, name);
    return true;
}

static void emulator_browse_pop(retro_emulator_t *emu)
{
    char *p = strrchr(emu->browse_subpath, '/');
    if (p)
        *p = '\0';
    else
        emu->browse_subpath[0] = '\0';
}

static const char *browse_subpath_last_segment(const char *subpath)
{
    const char *p = strrchr(subpath, '/');
    return (p && p[1] != '\0') ? (p + 1) : subpath;
}

static int list_cursor_for_folder_name(const tab_t *tab, const char *folder_name)
{
    if (!tab || !folder_name || !folder_name[0])
        return 0;

    for (int i = 0; i < tab->listbox.length; i++)
    {
        void *arg = tab->listbox.items[i].arg;
        if (!arg || rg_rom_list_arg_is_parent(arg))
            continue;
        const retro_emulator_file_t *file = (const retro_emulator_file_t *)arg;
        if (file->ext == NULL && strcmp(rom_entry_path_segment(file), folder_name) == 0)
            return i;
    }
    return 0;
}

bool rg_emulator_browse_pop_if_in_subfolder(tab_t *tab)
{
    retro_emulator_t *emu;
    char child_name[sizeof(emu->browse_subpath)];

    if (!tab)
        return false;
    emu = (retro_emulator_t *)tab->arg;
    if (!emu->browse_subpath[0])
        return false;

    snprintf(child_name, sizeof(child_name), "%s", browse_subpath_last_segment(emu->browse_subpath));
    emulator_browse_pop(emu);
    gui_event(TAB_REFRESH_LIST, tab);
    tab->listbox.cursor = list_cursor_for_folder_name(tab, child_name);
    return true;
}

bool rg_emulator_tab_in_rom_subfolder(const tab_t *tab)
{
    const retro_emulator_t *emu;

    if (!tab || !tab->arg)
        return false;
    emu = (const retro_emulator_t *)tab->arg;
    return emu->browse_subpath[0] != '\0';
}

/** Last path segment; safe for folder rows whose .name may be prefixed for display. */
static const char *rom_entry_path_segment(const retro_emulator_file_t *f)
{
    const char *p = strrchr(f->path, '/');
    return (p && p[1] != '\0') ? (p + 1) : f->name;
}

bool rg_emulator_validate_browse_path_for_tab(tab_t *tab)
{
    if (!tab || !tab->arg)
        return false;

    retro_emulator_t *emu = (retro_emulator_t *)tab->arg;
    if (!emu->browse_subpath[0])
        return false;

    bool must_reset = false;
    if (!browse_subpath_is_safe(emu->browse_subpath))
        must_reset = true;
    else
    {
        char folder[RG_PATH_MAX];
        emulator_browse_folder_path(emu, folder, sizeof(folder));
        rg_stat_t st = rg_storage_stat(folder);
        if (!st.exists || !st.is_dir)
            must_reset = true;
    }

    if (!must_reset)
        return false;

    emu->browse_subpath[0] = '\0';
    odroid_settings_MainMenuBrowseSubpath_set("");
    odroid_settings_commit();
    gui_event(TAB_REFRESH_LIST, tab);
    tab->listbox.cursor = 0;
    return true;
}

static int rom_entries_cmp(const void *a, const void *b)
{
    const retro_emulator_file_t *fa = (const retro_emulator_file_t *)a;
    const retro_emulator_file_t *fb = (const retro_emulator_file_t *)b;
    const int da = (fa->ext == NULL);
    const int db = (fb->ext == NULL);
    if (da != db)
        return db - da;
    return strcasecmp(fa->name, fb->name);
}

static void emulator_fill_tab_list(tab_t *tab, retro_emulator_t *emu)
{
    const bool in_subfolder = emu->browse_subpath[0] != '\0';
    const int n = emu->roms.count;
    const int parent_row = in_subfolder ? 1 : 0;
    const int list_len = n + parent_row;

    if (n > 0) {
        qsort(emu->roms.files, (size_t)n, sizeof(retro_emulator_file_t), rom_entries_cmp);
        // Recalculate ext pointers after sort (they point into .path which moved)
        for (int i = 0; i < n; i++) {
            retro_emulator_file_t *f = &emu->roms.files[i];
            if (f->ext != NULL)
                f->ext = (char *)get_extension(f->path);
        }
    }

    if (in_subfolder)
        snprintf(tab->status, sizeof(tab->status), "%s", emu->browse_subpath);
    else if (n > 0)
        snprintf(tab->status, sizeof(tab->status), "%s", emu->system_name);
    else
    {
        snprintf(tab->status, sizeof(tab->status), " No games");
    }

    if (n > 0 || in_subfolder)
    {
        gui_resize_list(tab, list_len);
        if (in_subfolder)
        {
            build_rom_parent_label(emu);
            tab->listbox.items[0].text = rg_rom_list_parent_label;
            tab->listbox.items[0].arg = (void *)&rg_rom_list_parent_placeholder;
        }
        for (int i = 0; i < n; i++)
        {
            tab->listbox.items[parent_row + i].text = emu->roms.files[i].name;
            tab->listbox.items[parent_row + i].arg = (void *)&emu->roms.files[i];
        }
        tab->listbox.cursor = MIN(tab->listbox.cursor, tab->listbox.length - 1);
        tab->listbox.cursor = MAX(tab->listbox.cursor, 0);
        tab->is_empty = false;
    }
    else
    {
        gui_resize_list(tab, 8);
        tab->listbox.cursor = 3;
        tab->is_empty = true;
    }
}

static void event_handler(gui_event_t event, tab_t *tab)
{
    retro_emulator_t *emu = (retro_emulator_t *)tab->arg;
    listbox_item_t *item = gui_get_selected_item(tab);
    void *sel_arg = item ? item->arg : NULL;

    if (event == TAB_INIT)
    {
        emulator_init(emu);
        emulator_fill_tab_list(tab, emu);
    }
    else if (event == TAB_REFRESH_LIST)
    {
        emu->roms.count = 0;
        emulator_refresh_list(emu);
        emulator_fill_tab_list(tab, emu);
    }

    if (sel_arg == NULL)
        return;

    if (event == KEY_PRESS_A)
    {
        if (rg_rom_list_arg_is_parent(sel_arg))
        {
            rg_emulator_browse_pop_if_in_subfolder(tab);
            return;
        }
        retro_emulator_file_t *file = (retro_emulator_file_t *)sel_arg;
        if (file->ext == NULL)
        {
            if (emulator_browse_append(emu, rom_entry_path_segment(file)))
            {
                tab->listbox.cursor = 0;
                gui_event(TAB_REFRESH_LIST, tab);
            }
            return;
        }
        emulator_show_file_menu(file);
    }
    else if (event == KEY_PRESS_B)
    {
        if (rg_rom_list_arg_is_parent(sel_arg))
            return;
        retro_emulator_file_t *file = (retro_emulator_file_t *)sel_arg;
        emulator_show_file_info(file);

        // Refresh if file was deleted
        if (strlen(file->path) == 0) {
            gui_event(TAB_REFRESH_LIST, tab);
        }
    }
    else if (event == TAB_IDLE)
    {
    }
    else if (event == TAB_REDRAW)
    {
    }
}

retro_emulator_file_t *rg_emulators_shared_file_buffer(int *maxcount)
{
    if (maxcount)
        *maxcount = shared_files ? 1000 : 0;
    return shared_files;
}

const rom_system_t *rg_emulators_system_for_dir(const char *dirname, size_t len)
{
    for (int i = 0; i < emulators_count; i++) {
        if (strlen(emulators[i].dirname) == len &&
            strncmp(emulators[i].dirname, dirname, len) == 0)
            return emulators[i].system;
    }
    return NULL;
}

/* core_path is non-NULL only for a dynamically-discovered external core
 * (see emulators_scan_cores()); pass NULL for the compile-time tabs
 * (Homebrew + Favorites). Several tabs may share the same core_path (one core
 * binary exposing multiple systems, e.g. PC Engine + PC Engine CD — see
 * add_emulator_dynamic()). */
static void add_emulator_ex(const char *system, const char *dirname, const char* ext,
                            int16_t logo_idx, int16_t header_idx,
                            uint32_t parse_type, const char *core_path)
{
    assert(emulators != NULL && emulators_count < emulators_capacity);
    retro_emulator_t *p = &emulators[emulators_count];
    rom_system_t *s = &systems[emulators_count];
    emulators_count++;

    strcpy(p->system_name, system);
    strcpy(p->dirname, dirname);
    snprintf(p->exts, sizeof(p->exts), " %s ", ext);
    p->browse_subpath[0] = '\0';
    p->roms.count = 0;
    p->roms.maxcount = 1000;
    if (shared_files == NULL)
    {
        shared_files = ram_calloc(p->roms.maxcount, sizeof(retro_emulator_file_t));
    }
    p->roms.files = shared_files;
    p->initialized = false;
    p->system = s;
    p->core_path[0] = '\0';
    p->parse_type = parse_type;
    if (core_path)
        strncpy(p->core_path, core_path, sizeof(p->core_path) - 1);

    /* Alias the copies just made above (p->system_name, not the caller's
     * `system`) so these pointers stay valid even when the caller's own
     * string is transient (e.g. a stack-local gnw_core_meta_t while
     * scanning /cores/*.bin — see add_emulator_dynamic()). */
    s->extension = p->exts;
    s->roms = p->roms.files;
    s->roms_count = p->roms.count;
    s->system_name = p->system_name;
    s->core_path = p->core_path[0] ? p->core_path : NULL;
    s->parse_type = parse_type;

    gui_add_tab(dirname, logo_idx, header_idx, p, event_handler);
}

static void add_emulator(const char *system, const char *dirname, const char* ext,
                         uint16_t logo_idx, uint16_t header_idx)
{
    add_emulator_ex(system, dirname, ext, (int16_t)logo_idx, (int16_t)header_idx,
                    GNW_PARSE_ROM, NULL);
}

static void remove_extension(const char *path, char *new_path) {
    // we can assume an extension is always present
    const char *last_dot = strrchr(path, '.');

    size_t new_len = last_dot - path;

    if (!new_path) return;

    memcpy(new_path, path, new_len);
    new_path[new_len] = '\0';
}

static const char *get_extension(const char *filename) {
    const char *extension = strrchr(filename, '.');
    
    if (extension && extension != filename) {
        return extension + 1;
    }

    return NULL;
}

static bool emulator_is_cdrom(const retro_emulator_t *emu)
{
    return emu->parse_type == GNW_PARSE_CDROM;
}

/* Case-insensitive ".cue" — avoid snprintf/strtolower/strstr on every SD entry. */
static bool filename_is_cue(const char *name)
{
    const char *ext = strrchr(name, '.');
    if (!ext || ext == name || !ext[1])
        return false;
    ext++;
    return ((ext[0] | 0x20) == 'c')
        && ((ext[1] | 0x20) == 'u')
        && ((ext[2] | 0x20) == 'e')
        && (ext[3] == '\0');
}

static bool emulator_add_rom_file(retro_emulator_t *emu, const char *path,
                                  const char *basename, uint32_t size)
{
    retro_emulator_file_t *slot;

    if (emu->roms.count + 1 > emu->roms.maxcount)
        return false;

    slot = &emu->roms.files[emu->roms.count];
    memset(slot, 0, sizeof(*slot));
    slot->address = 0;
    slot->size = size;
    slot->system = emu->system;
    slot->region = REGION_NTSC;
    strncpy(slot->path, path, sizeof(slot->path) - 1);
    slot->path[sizeof(slot->path) - 1] = '\0';
    remove_extension(basename, slot->name);
    slot->ext = (char *)get_extension(slot->path);
#if COVERFLOW != 0
    slot->img_state = IMG_STATE_UNKNOWN;
    slot->cover_bin_offset = 0;
    slot->cover_bin_size = 0;
#endif
    /* GWHB v1: prefer display_name from the header; note cover_bin_* for
     * metadata only — coverflow still prefers /covers/homebrew/<stem>.img
     * over the embedded JPEG (see get_coverfile in gui.c). */
    if (emu->dirname[0] && strcmp(emu->dirname, "homebrew") == 0) {
        gwhb_meta_t hb;
        uint16_t hb_len = 0;
        if (gwhb_probe(path, &hb, &hb_len) && hb_len != 0) {
            if (hb.display_name[0]) {
                strncpy(slot->name, hb.display_name, sizeof(slot->name) - 1);
                slot->name[sizeof(slot->name) - 1] = '\0';
            }
#if COVERFLOW != 0
            if (hb.cover_size != 0 && hb.cover_offset != 0) {
                slot->cover_bin_offset = hb.cover_offset;
                slot->cover_bin_size = hb.cover_size;
            }
#endif
        }
    }
#if CHEAT_CODES == 1
    slot->cheat_count = 0;
    slot->cheat_codes = NULL;
    slot->cheat_descs = NULL;
#endif
    emu->roms.count++;
    emu->system->roms_count = emu->roms.count;
    return true;
}

static bool emulator_add_folder_row(retro_emulator_t *emu, const char *path,
                                    const char *basename)
{
    retro_emulator_file_t *slot;
    size_t nl;

    if (emu->roms.count + 1 > emu->roms.maxcount)
        return false;

    slot = &emu->roms.files[emu->roms.count];
    memset(slot, 0, sizeof(*slot));
    slot->address = 0;
    slot->size = 0;
    slot->system = emu->system;
    slot->region = REGION_NTSC;
    strncpy(slot->path, path, sizeof(slot->path) - 1);
    slot->path[sizeof(slot->path) - 1] = '\0';
    strncpy(slot->name, basename, sizeof(slot->name) - 1);
    slot->name[sizeof(slot->name) - 1] = '\0';
    slot->ext = NULL;
    nl = strlen(slot->name);
    if (nl + 3 < sizeof(slot->name))
    {
        memmove(slot->name + 2, slot->name, nl + 1);
        slot->name[0] = '>';
        slot->name[1] = ' ';
    }
#if COVERFLOW != 0
    slot->img_state = IMG_STATE_NO_COVER;
#endif
    emu->roms.count++;
    emu->system->roms_count = emu->roms.count;
    return true;
}

#if SD_CARD == 1
/* Prefer "<dirname>/<dirname>.cue" (Redump / Fullset layout) via f_stat — one
 * lookup instead of readdir through dozens of Track*.bin LFNs per game.
 * Fall back to a directory scan when the cue name differs from the folder. */
static bool cdrom_collapse_game_dir(retro_emulator_t *emu, const char *path)
{
    DIR dir;
    FILINFO fno;
    size_t path_len = strlen(path);
    char fullpath[RG_PATH_MAX];
    const char *base;
    char cue_name[256];
    size_t base_len;

    if (path_len + 6 >= RG_PATH_MAX)
        return false;

    base = strrchr(path, '/');
    base = base ? base + 1 : path;
    base_len = strlen(base);
    if (base_len > 0 && base_len + 4 < sizeof(cue_name)
        && path_len + 1 + base_len + 4 < sizeof(fullpath))
    {
        memcpy(cue_name, base, base_len);
        memcpy(cue_name + base_len, ".cue", 5);
        snprintf(fullpath, sizeof(fullpath), "%s/%s", path, cue_name);
        if (f_stat(fullpath, &fno) == FR_OK && !(fno.fattrib & AM_DIR))
            return emulator_add_rom_file(emu, fullpath, cue_name, (uint32_t)fno.fsize);
    }

    if (f_opendir(&dir, path) != FR_OK)
        return false;

    bool found = false;
    while (emu->roms.count < emu->roms.maxcount)
    {
        wdog_refresh();
        if (f_readdir(&dir, &fno) != FR_OK || fno.fname[0] == 0)
            break;
        if (fno.fname[0] == '.')
            continue;
        if (fno.fattrib & AM_DIR)
            continue;
        if (!filename_is_cue(fno.fname))
            continue;
        if (path_len + 1 + strlen(fno.fname) >= sizeof(fullpath))
            continue;

        snprintf(fullpath, sizeof(fullpath), "%s/%s", path, fno.fname);
        found = emulator_add_rom_file(emu, fullpath, fno.fname, (uint32_t)fno.fsize);
        break;
    }
    f_closedir(&dir);
    return found;
}

/* Scan one CD-ROM browse folder (GNW_PARSE_CDROM systems, e.g. PC Engine CD)
 * without nesting FatFs DIR handles. Parent directory is scanned once, child
 * names are collected, then children are processed after parent close
 * (FatFs LFN safety). */
static void emulator_scan_cdrom_folder(retro_emulator_t *emu, const char *folder)
{
    DIR dir;
    FILINFO fno;
    size_t folder_len = strlen(folder);
    char fullpath[RG_PATH_MAX];
    char **subdirs = NULL;
    int subdir_count = 0;
    int subdir_cap = 0;

    /* Pass 1: process .cue files at this level and collect child directories. */
    if (f_opendir(&dir, folder) == FR_OK)
    {
        while (emu->roms.count < emu->roms.maxcount)
        {
            wdog_refresh();
            if (f_readdir(&dir, &fno) != FR_OK || fno.fname[0] == 0)
                break;
            if (fno.fname[0] == '.')
                continue;
            if (fno.fattrib & AM_DIR)
            {
                char *name_copy;
                if (subdir_count >= subdir_cap)
                {
                    int new_cap = subdir_cap ? (subdir_cap * 2) : 16;
                    char **new_subdirs = realloc(subdirs, (size_t)new_cap * sizeof(*new_subdirs));
                    if (!new_subdirs)
                        break;
                    subdirs = new_subdirs;
                    subdir_cap = new_cap;
                }
                name_copy = strdup(fno.fname);
                if (!name_copy)
                    break;
                subdirs[subdir_count++] = name_copy;
                continue;
            }
            if (!filename_is_cue(fno.fname))
                continue;
            if (folder_len + 1 + strlen(fno.fname) >= sizeof(fullpath))
                continue;
            snprintf(fullpath, sizeof(fullpath), "%s/%s", folder, fno.fname);
            if (!emulator_add_rom_file(emu, fullpath, fno.fname, (uint32_t)fno.fsize))
                break;
        }
        f_closedir(&dir);
    }

    /* Pass 2: process each child dir after parent has been closed. */
    for (int i = 0; i < subdir_count && emu->roms.count < emu->roms.maxcount; i++)
    {
        wdog_refresh();
        if (folder_len + 1 + strlen(subdirs[i]) >= sizeof(fullpath))
            continue;
        snprintf(fullpath, sizeof(fullpath), "%s/%s", folder, subdirs[i]);
        if (!cdrom_collapse_game_dir(emu, fullpath))
            emulator_add_folder_row(emu, fullpath, subdirs[i]);
    }

    for (int i = 0; i < subdir_count; i++)
        free(subdirs[i]);
    free(subdirs);
}
#endif /* SD_CARD == 1 */

static int scan_folder_cb(const rg_scandir_t *entry, void *arg)
{
    retro_emulator_t *emu = (retro_emulator_t *)arg;
    uint8_t is_valid = false;
    char ext_buf[32];

    if (entry->basename[0] == '.')
        return RG_SCANDIR_SKIP;

    if (entry->is_file)
    {
        const char *ext = rg_extension(entry->basename);
        if (ext && ext[0])
        {
            snprintf(ext_buf, sizeof(ext_buf), " %s ", ext);
            is_valid = strstr(emu->exts, rg_strtolower(ext_buf)) != NULL;
        }
    }
    else if (entry->is_dir)
    {
        is_valid = true;
    }

    if (!is_valid)
        return RG_SCANDIR_CONTINUE;

    if (entry->is_dir)
    {
        if (!emulator_add_folder_row(emu, entry->path, entry->basename))
            return RG_SCANDIR_STOP;
        return RG_SCANDIR_CONTINUE;
    }

    if (!emulator_add_rom_file(emu, entry->path, entry->basename, (uint32_t)entry->size))
        return RG_SCANDIR_STOP;
    /* Non-pcecd uses same adder; extension already validated. For non-cue systems
     * get_extension still points at the real ext in path. */
    return RG_SCANDIR_CONTINUE;
}

void emulator_init(retro_emulator_t *emu)
{
    char folder[RG_PATH_MAX];

    if (emu->initialized)
        return;

    emu->initialized = true;
#if COVERFLOW != 0
    /* Cover slot size: set once by gui probe; kept across subfolders (not reset in emulator_refresh_list). */
    emu->cover_height = 0;
    emu->cover_width = 0;
#endif

    printf("Retro-Go: Initializing emulator '%s'\n", emu->system_name);

    sprintf(folder, ODROID_BASE_PATH_SAVES "/%s", emu->dirname);
    rg_storage_mkdir(folder);

    sprintf(folder, ODROID_BASE_PATH_ROMS "/%s", emu->dirname);
    rg_storage_mkdir(folder);

    emulator_browse_folder_path(emu, folder, sizeof(folder));
#if SD_CARD == 1
    if (emulator_is_cdrom(emu))
        emulator_scan_cdrom_folder(emu, folder);
    else
#endif
        rg_storage_scandir(folder, scan_folder_cb, emu, 0);
}

void emulator_refresh_list(retro_emulator_t *emu)
{
    char folder[RG_PATH_MAX];

    sprintf(folder, ODROID_BASE_PATH_ROMS "/%s", emu->dirname);
    rg_storage_mkdir(folder);

    emulator_browse_folder_path(emu, folder, sizeof(folder));
#if SD_CARD == 1
    if (emulator_is_cdrom(emu))
        emulator_scan_cdrom_folder(emu, folder);
    else
#endif
        rg_storage_scandir(folder, scan_folder_cb, emu, 0);
}

#if SD_CARD == 1
/* Copy dirname of `path` into `out` (no trailing slash). */
static void path_dirname_copy(const char *path, char *out, size_t out_size)
{
    const char *slash = path ? strrchr(path, '/') : NULL;
    if (!slash || out_size == 0) {
        if (out_size > 0) {
            out[0] = '.';
            if (out_size > 1)
                out[1] = '\0';
        }
        return;
    }
    size_t len = (size_t)(slash - path);
    if (len >= out_size)
        len = out_size - 1;
    memcpy(out, path, len);
    out[len] = '\0';
}

/* Extracts the "<dirname>" segment out of a "/roms/<dirname>/…" path. Used
 * to generalize the old pcecd-only delete logic to any GNW_PARSE_CDROM
 * system without needing a dirname field on rom_system_t. */
static bool cdrom_extract_dirname(const char *path, char *dirname_out, size_t dirname_size)
{
    static const char root[] = RG_BASE_PATH_ROMS "/";
    const size_t root_len = sizeof(root) - 1;
    const char *p, *slash;
    size_t len;

    if (strncmp(path, root, root_len) != 0)
        return false;
    p = path + root_len;
    slash = strchr(p, '/');
    if (!slash)
        return false;
    len = (size_t)(slash - p);
    if (len == 0 || len >= dirname_size)
        return false;
    memcpy(dirname_out, p, len);
    dirname_out[len] = '\0';
    return true;
}

/* True when cue lives in a per-game folder under /roms/<dirname>/<game>/… */
static bool cdrom_cue_in_game_folder(const char *cue_path, const char *dirname, char *parent_out, size_t parent_size)
{
    char root[RG_PATH_MAX];
    size_t root_len;

    path_dirname_copy(cue_path, parent_out, parent_size);
    snprintf(root, sizeof(root), "%s/%s", RG_BASE_PATH_ROMS, dirname);
    if (strcmp(parent_out, root) == 0)
        return false; /* flat layout: cue directly under /roms/<dirname> */

    root_len = strlen(root);
    if (strncmp(parent_out, root, root_len) != 0 || parent_out[root_len] != '/')
        return false;
    /* Must be exactly one level under dirname (…/<dirname>/<game>), not
     * deeper nested junk we might not want to wipe wholesale — still OK to
     * delete that folder if the cue is there; collapse only uses one level. */
    return true;
}

/* Flat CD-ROM layout: delete FILE "…" siblings referenced by the cue, then the cue. */
static void emulator_delete_cdrom_flat(const char *cue_path)
{
    char parent[RG_PATH_MAX];
    char line[512];
    FILE *cue;

    path_dirname_copy(cue_path, parent, sizeof(parent));
    cue = fopen(cue_path, "rb");
    if (cue) {
        while (fgets(line, sizeof(line), cue)) {
            char *p = line;
            const char *q1, *q2;
            char name[256];
            char binpath[RG_PATH_MAX];
            size_t n;

            while (*p == ' ' || *p == '\t')
                p++;
            if (strncmp(p, "FILE", 4) != 0)
                continue;
            q1 = strchr(p, '"');
            q2 = q1 ? strchr(q1 + 1, '"') : NULL;
            if (!q1 || !q2)
                continue;
            n = (size_t)(q2 - q1 - 1);
            if (n == 0 || n >= sizeof(name))
                continue;
            memcpy(name, q1 + 1, n);
            name[n] = '\0';
            /* Reject path traversal / absolute refs — only same-dir siblings. */
            if (strchr(name, '/') || strchr(name, '\\') || strstr(name, ".."))
                continue;
            snprintf(binpath, sizeof(binpath), "%s/%s", parent, name);
            rg_storage_delete(binpath);
            wdog_refresh();
        }
        fclose(cue);
    }
    rg_storage_delete(cue_path);
}

/* Delete ROM storage for a list entry. CD-ROM games (GNW_PARSE_CDROM, e.g.
 * PC Engine CD) are multi-file (cue+bins, often in a per-game folder); a
 * plain unlink of the .cue would leave orphans. */
static void emulator_delete_rom_storage(retro_emulator_file_t *file)
{
    char parent[RG_PATH_MAX];
    char dirname[16];

    if (!file || !file->path[0])
        return;

    if (file->ext && strcasecmp(file->ext, "cue") == 0 &&
        file->system && file->system->parse_type == GNW_PARSE_CDROM &&
        cdrom_extract_dirname(file->path, dirname, sizeof(dirname))) {
        if (cdrom_cue_in_game_folder(file->path, dirname, parent, sizeof(parent)))
            rg_storage_delete(parent);
        else
            emulator_delete_cdrom_flat(file->path);
        return;
    }

    rg_storage_delete(file->path);
}
#endif /* SD_CARD == 1 */

void emulator_show_file_info(retro_emulator_file_t *file)
{
    char filename_value[128];
    char type_value[32];
    char size_value[32];

    const bool no_delete = (file->ext == NULL);
    odroid_dialog_choice_t choices[] = {
        {-1, curr_lang->s_File, filename_value, 0, NULL},
        {-1, curr_lang->s_Type, type_value, 0, NULL},
        {-1, curr_lang->s_Size, size_value, 0, NULL},
#if SD_CARD == 1 // Can't delete file on FrogFS
        ODROID_DIALOG_CHOICE_SEPARATOR,
        {10, curr_lang->s_Delete_Rom_File, "", no_delete ? -1 : 1, NULL},
#endif
        ODROID_DIALOG_CHOICE_SEPARATOR,
        {1, curr_lang->s_Close, "", 1, NULL},
        ODROID_DIALOG_CHOICE_LAST
    };

    sprintf(choices[0].value, "%.127s", file->name);
    if (file->ext == NULL)
        strcpy(choices[1].value, "Folder");
    else
        sprintf(choices[1].value, "%s", file->ext ? file->ext : "");
    if (no_delete)
        strcpy(choices[2].value, "-");
    else
        sprintf(choices[2].value, "%d KB", (int)(file->size / 1024));

    while (1) {
        int sel = odroid_overlay_dialog(curr_lang->s_GameProp, choices, -1, &gui_redraw_callback, 0);
#if SD_CARD == 1
        switch (sel)
        {
        case 10: {
            char title[160];
            sprintf(title, curr_lang->s_Delete_Rom_File_Confirm, file->name);

            int delete_confirm_sel = odroid_overlay_confirm(
                title,
                false,
                &gui_redraw_callback
            );

            if (delete_confirm_sel == 1) {
                rg_favorites_remove(file->path); /* drop any stale ★ entry */
                emulator_delete_rom_storage(file);
                strcpy(file->path, "");
            } else {
                continue;
            }
            break;
        }

        }
#endif

        break;
    }
}

#if CHEAT_CODES == 1
static bool cheat_update_cb(odroid_dialog_choice_t *option, odroid_dialog_event_t event, uint32_t repeat)
{
    bool is_on = odroid_settings_ActiveGameGenieCodes_is_enabled(CHOSEN_FILE->path, option->id);
    if (event == ODROID_DIALOG_PREV || event == ODROID_DIALOG_NEXT) 
    {
        is_on = is_on ? false : true;
        odroid_settings_ActiveGameGenieCodes_set(CHOSEN_FILE->path, option->id, is_on);
    }
    strcpy(option->value, is_on ? curr_lang->s_Option_ON : curr_lang->s_Option_OFF);
    return event == ODROID_DIALOG_ENTER;
}

static bool show_cheat_dialog()
{
    static odroid_dialog_choice_t last = ODROID_DIALOG_CHOICE_LAST;

    // +1 for the terminator sentinel
    odroid_dialog_choice_t *choices = malloc((CHOSEN_FILE->cheat_count + 1) * sizeof(odroid_dialog_choice_t));
    char svalues[MAX_CHEAT_CODES][10];
    for(int i=0; i<CHOSEN_FILE->cheat_count; i++) 
    {
        const char *label = CHOSEN_FILE->cheat_descs[i];
        if (label == NULL) {
            label = CHOSEN_FILE->cheat_codes[i];
        }
        choices[i].id = i;
        choices[i].label = label;
        choices[i].value = svalues[i];
        choices[i].enabled = 1;
        choices[i].update_cb = cheat_update_cb;
    }
    choices[CHOSEN_FILE->cheat_count] = last;
    odroid_overlay_dialog(curr_lang->s_Cheat_Codes_Title, choices, 0, NULL, 0);

    free(choices);
    odroid_settings_commit();
    return false;
}

void emulator_update_cheats_info(retro_emulator_file_t *file) {
    if (file->cheat_codes) {
        return;
    }

    // Check for pceplus cheat file (PC Engine)
    char *cheat_path = odroid_system_get_path(ODROID_PATH_CHEAT_PCE, file->path);
    if (odroid_sdcard_get_filesize(cheat_path) > 0) {
        printf("Retro-Go: Found cheat file %s\n", cheat_path);
        file->cheat_codes = calloc(MAX_CHEAT_CODES, sizeof(char *));
        file->cheat_descs = calloc(MAX_CHEAT_CODES, sizeof(char *));
        FILE *cheat_file = fopen(cheat_path, "r");
        if (!cheat_file) {
            printf("Retro-Go: Failed to open cheat file %s\n", cheat_path);
            return;
        }
        char line[256];
        while (fgets(line, sizeof(line), cheat_file)) {
            char *trimmed_line = strtok(line, "\n");
            if (!trimmed_line || trimmed_line[0] == '#' ||
                (trimmed_line[0] == '/' && trimmed_line[1] == '/')) {
                continue;
            }

            char *parts[10];
            uint8_t part_count = 0;
            char *token = strtok(trimmed_line, ",");
            while (token && part_count < 10) {
                parts[part_count++] = token;
                token = strtok(NULL, ",");
            }

            if (part_count < 2) {
                continue;
            }

            int cmd_count = 0;
            file->cheat_codes[file->cheat_count] = malloc((size_t)(1 + 4 * (part_count-1)));
            char *codes_ptr = (char *)file->cheat_codes[file->cheat_count];
            *(codes_ptr++)=part_count - 1;
            for (int i = 0; i < part_count - 1; i++) {
                char *part = parts[i];
                int x = (int)strtol(part, NULL, 16);
                printf("x = %x\n", x);
                *(codes_ptr++)=x>>24;
                *(codes_ptr++)=(x>>16)&0xFF;
                *(codes_ptr++)=(x>>8)&0xFF;
                *(codes_ptr++)=x&0xFF;
                cmd_count++;
            }

            char *desc = parts[part_count - 1];
            if (desc) {
                while (*desc == ' ') desc++;
                desc = strndup(desc, 40);
            }

            if (file->cheat_count < MAX_CHEAT_CODES) {
                file->cheat_descs[file->cheat_count] = desc;
                file->cheat_count++;
            } else {
                printf("INFO: More than %d cheat codes...\n", MAX_CHEAT_CODES);
                break;
            }
        }
        fclose(cheat_file);
    }
    free(cheat_path);
    if (file->cheat_count)
        return;

    // Check for ggcodes cheat file (GB/GBC/NES)
    cheat_path = odroid_system_get_path(ODROID_PATH_CHEAT_GAME_GENIE, file->path);
    if (odroid_sdcard_get_filesize(cheat_path) > 0) {
        printf("Retro-Go: Found cheat file %s\n", cheat_path);
        file->cheat_codes = calloc(MAX_CHEAT_CODES, sizeof(char *));
        file->cheat_descs = calloc(MAX_CHEAT_CODES, sizeof(char *));
        FILE *cheat_file = fopen(cheat_path, "r");
        if (!cheat_file) {
            printf("Retro-Go: Failed to open cheat file %s\n", cheat_path);
            return;
        }
        char line[256];
        while (fgets(line, sizeof(line), cheat_file)) {
            char *trimmed_line = strtok(line, "\n");
            if (!trimmed_line || trimmed_line[0] == '#' ||
                (trimmed_line[0] == '/' && trimmed_line[1] == '/')) {
                continue;
            }

            char *parts[10];
            int part_count = 0;
            char *token = strtok(trimmed_line, ",");
            while (token && part_count < 10) {
                parts[part_count++] = token;
                token = strtok(NULL, ",");
            }
            printf("Retro-Go: Part count: %d\n", part_count);
            for (int i = 0; i < part_count; i++) {
                printf("Retro-Go: Part %d: %s\n", i, parts[i]);
            }

            file->cheat_codes[file->cheat_count] = strdup(parts[0]);

            char *desc = parts[part_count - 1];
            if (desc) {
                while (*desc == ' ') desc++; // Remove leading spaces
                desc = strndup(desc, 40);
            }

            if (file->cheat_count < MAX_CHEAT_CODES) {
                file->cheat_descs[file->cheat_count] = desc;
                file->cheat_count++;
            } else {
                printf("INFO: More than %d cheat codes...\n", MAX_CHEAT_CODES);
                break;
            }
        }
        fclose(cheat_file);
    }
    free(cheat_path);
    if (file->cheat_count)
        return;

    // Check for mfc cheat file (MSX)
    cheat_path = odroid_system_get_path(ODROID_PATH_CHEAT_MCF, file->path);
    if (odroid_sdcard_get_filesize(cheat_path) > 0) {
        printf("Retro-Go: Found cheat file %s\n", cheat_path);
        file->cheat_codes = calloc(MAX_CHEAT_CODES, sizeof(char *));
        file->cheat_descs = calloc(MAX_CHEAT_CODES, sizeof(char *));

        FILE *cheat_file = fopen(cheat_path, "r");
        if (!cheat_file) {
            printf("Retro-Go: Failed to open cheat file %s\n", cheat_path);
            return;
        }

        char line[256];
        while (fgets(line, sizeof(line), cheat_file)) {
            if (line[0] == '!') continue;
            char *last_comma = strrchr(line, ',');
            if (!last_comma) continue;
            *last_comma = '\0';

            printf("MFC: cheat: %s\n", line);
            printf("MFC: desc: %s\n", last_comma + 1);
            if (file->cheat_count < MAX_CHEAT_CODES) {
                file->cheat_codes[file->cheat_count] = strdup(line);
                file->cheat_descs[file->cheat_count] = strdup(last_comma + 1);
                file->cheat_count++;
            } else {
                printf("INFO: More than %d cheat codes...\n", MAX_CHEAT_CODES);
                break;
            }
        }
    }
    free(cheat_path);
}
#endif

bool emulator_show_file_menu(retro_emulator_file_t *file)
{
    if (file->ext == NULL)
        return false;

    int slot = -1;
    char *sram_path = odroid_system_get_path(ODROID_PATH_SAVE_SRAM, file->path);
    rg_emu_states_t *savestates = odroid_system_emu_get_states(file->path, 4);
    bool has_save = savestates->used > 0;
    bool has_sram = odroid_sdcard_get_filesize(sram_path) > 0;
    bool force_redraw = false;

#if CHEAT_CODES == 1
    // Free previous cheat codes
    if (CHOSEN_FILE) {
        for (int i = 0; i < CHOSEN_FILE->cheat_count; i++) {
            if (CHOSEN_FILE->cheat_codes[i]) free(CHOSEN_FILE->cheat_codes[i]);
            if (CHOSEN_FILE->cheat_descs[i]) free(CHOSEN_FILE->cheat_descs[i]);
        }
        free(CHOSEN_FILE->cheat_codes);
        free(CHOSEN_FILE->cheat_descs);
    }

    CHOSEN_FILE = file;
    emulator_update_cheats_info(CHOSEN_FILE);
#endif

    /* One /favorites.txt read per menu open — the discrete-event rule. */
    bool is_fav = rg_favorites_contains(file->path);

    /* Built dynamically: the favorites rows vary, and the old fixed-array
     * "overwrite index N with LAST" cheat-row hack broke on every reshuffle. */
    const odroid_dialog_choice_t sep = ODROID_DIALOG_CHOICE_SEPARATOR;
    odroid_dialog_choice_t choices[12];
    int rows = 0;
    choices[rows++] = (odroid_dialog_choice_t){0, curr_lang->s_Resume_game, (char *)"", (has_save) ? 1 : -1, NULL};
    choices[rows++] = (odroid_dialog_choice_t){1, curr_lang->s_New_game, (char *)"", 1, NULL};
    choices[rows++] = sep;
    choices[rows++] = (odroid_dialog_choice_t){2, curr_lang->s_Delete_save, (char *)"", (has_save || has_sram) ? 1 : -1, NULL};
    choices[rows++] = sep;
    choices[rows++] = (odroid_dialog_choice_t){3, is_fav ? curr_lang->s_Del_favorite : curr_lang->s_Add_favorite, (char *)"", 1, NULL};
#if CHEAT_CODES == 1
    if (CHOSEN_FILE->cheat_count != 0) {
        choices[rows++] = sep;
        choices[rows++] = (odroid_dialog_choice_t){4, curr_lang->s_Cheat_Codes, (char *)"", 1, NULL};
    }
#endif
    choices[rows++] = (odroid_dialog_choice_t)ODROID_DIALOG_CHOICE_LAST;

    int sel = odroid_overlay_dialog(file->name, choices, has_save ? 0 : 1, &gui_redraw_callback, 0);

    if (sel == 0) { // Resume game
        if (has_save) {
            if ((slot = odroid_savestate_menu(curr_lang->s_Resume_game, file->path, true, true, &gui_redraw_callback)) != -1) {
                gui_save_current_tab();
                emulator_start(file, true, false, slot);
            }
        } else if (has_sram) {
            gui_save_current_tab();
            emulator_start(file, true, false, -2);
        }
    }
    if (sel == 1) { // New game
        gui_save_current_tab();
        emulator_start(file, false, false, 0);
    }
    else if (sel == 2) {
        while ((savestates->used > 0) &&
               ((slot = odroid_savestate_menu(curr_lang->s_Confirm_del_save, file->path, true, false, &gui_redraw_callback)) != -1))
        {
            odroid_sdcard_unlink(savestates->slots[slot].preview);
            odroid_sdcard_unlink(savestates->slots[slot].file);
            savestates->slots[slot].is_used = false;
            savestates->used--;
        }
        if (has_sram && odroid_overlay_confirm(curr_lang->s_Confirm_del_sram, false, &gui_redraw_callback))
        {
            odroid_sdcard_unlink(sram_path);
        }
    }
    else if (sel == 3) { // Add/remove favorite
        if (is_fav)
            rg_favorites_remove(file->path);
        else
            rg_favorites_add(file->path);
        force_redraw = true;
    }
#if CHEAT_CODES == 1
    else if (sel == 4) {
        if (CHOSEN_FILE->cheat_count != 0)
            show_cheat_dialog();
        force_redraw = true;
    }
#endif

    free(sram_path);
    /* savestates is static — no free needed */

#if CHEAT_CODES == 1
    CHOSEN_FILE = NULL;
#endif

    free(savestates);
    return force_redraw;
}

typedef int func(void);
extern LTDC_HandleTypeDef hltdc;

/* The compile-time dispatch table that used to live here (emu_dispatch_t /
 * run_internal_emu) was removed while migrating every classic emulator to
 * standalone cores/<system>/ builds loaded dynamically from /cores/*.bin
 * (see "Cores externes avec ABI" plan). Its replacement, a header-driven
 * loader, is introduced alongside emulators_scan_cores(). Homebrew (GWHB)
 * is untouched by this migration and keep explicit blocks below. */

/* --- Universal Homebrew Header (GWHB) loader ---------------------------
 *
 * Lets an out-of-tree homebrew binary run without any firmware-side
 * dispatch-table entry or linker overlay symbols: drop a .bin under
 * /roms/homebrew/ and it runs, as long as it starts with a GWHB container
 * (see gwhb.h).
 *
 * v1 meta: firmware loads only the code payload into RAM_EMU, zeroes BSS,
 * and jumps to payload offset 0. Legacy (header_length == 0): whole file
 * was copied into RAM_EMU with entry at offset 64 (binary zeroes its own
 * BSS).
 *
 * Trust model: the file is loaded, unauthenticated, from an SD card, so
 * every firmware-side check below is defensive: refuse rather than jump
 * into a corrupt or incompatible binary. */

static void show_homebrew_error_screen(const char *reason)
{
  /* Distinct from show_corrupted_installation_screen(): that one tells the
   * user to reinstall the whole firmware, which is the wrong advice when
   * only a /roms/homebrew/*.bin failed to load. */
  odroid_dialog_choice_t choices[] = {
    {0, reason ? reason : "Homebrew load failed", "", -1, NULL},
    ODROID_DIALOG_CHOICE_SEPARATOR,
    {1, curr_lang->s_OK, "", 1, NULL},
    ODROID_DIALOG_CHOICE_LAST,
  };

  (void)odroid_overlay_dialog("Homebrew", choices, 2, NULL, 0);
}

/* Read GWHB envelope + meta from `path`. Returns true on a recognizable
 * GWHB file (v1 or legacy). On v1 success, *meta is filled and
 * *header_length is the on-disk header_length field. Legacy: *header_length
 * is 0 and *meta is left untouched. */
static bool gwhb_probe(const char *path, gwhb_meta_t *meta, uint16_t *header_length)
{
    FILE *f = fopen(path, "rb");
    if (!f)
        return false;

    uint8_t envelope[GWHB_HEADER_MIN_SIZE];
    if (fread(envelope, 1, sizeof(envelope), f) != sizeof(envelope)) {
        fclose(f);
        return false;
    }

    uint32_t magic;
    memcpy(&magic, envelope, 4);
    if (magic != GWHB_MAGIC) {
        fclose(f);
        return false;
    }

    uint16_t version, length;
    memcpy(&version, envelope + 4, 2);
    memcpy(&length, envelope + 6, 2);

    /* Legacy fixed 64-byte header: required_abi was a u32 at offset 4, so
     * reading as CORE-style envelope yields header_length == 0. */
    if (length == 0) {
        fclose(f);
        if (header_length)
            *header_length = 0;
        return true;
    }

    if (version != GWHB_META_VERSION || length < sizeof(gwhb_meta_t)) {
        fclose(f);
        return false;
    }

    if (fread(meta, 1, sizeof(*meta), f) != sizeof(*meta)) {
        fclose(f);
        return false;
    }
    fclose(f);

    meta->display_name[sizeof(meta->display_name) - 1] = '\0';
    if (header_length)
        *header_length = length;
    return true;
}

static bool gwhb_abi_ok(uint32_t required_abi, uint32_t required_abi_min_size)
{
    /* required_abi alone is not enough: append-only ABI growth does not
     * bump GW_FIRMWARE_ABI_VERSION, so two firmware builds can report the
     * same version with different actual struct sizes. required_abi_min_size
     * detects "this firmware predates a field I need". Older/smaller ABI
     * binaries are fine, hence <=, not ==. */
    return required_abi <= GW_FIRMWARE_ABI_VERSION
        && required_abi_min_size <= g_firmware_abi.size;
}

__attribute__((noinline))
static void run_gwhb_homebrew(const char *path, uint8_t load_state, uint8_t start_paused, int8_t save_slot)
{
    gwhb_meta_t meta;
    uint16_t header_length = 0;

    if (!gwhb_probe(path, &meta, &header_length)) {
        printf("GWHB: probe failed for '%s'\n", path);
        show_homebrew_error_screen("Not a GWHB .bin");
        return;
    }

    const uint32_t ram_emu_len =
        ((uint32_t)&__RAM_EMU_END__) - (uint32_t)&__RAM_EMU_START__;
    uint8_t *base = (uint8_t *)&__RAM_EMU_START__;

    if (header_length == 0) {
        /* Legacy: whole file already must fit in RAM_EMU; entry at +64. */
        size_t copied = rg_storage_copy_file_to_ram_bounded(
            (char *)path, base, 0, ram_emu_len, NULL);
        if (copied < GWHB_LEGACY_HEADER_SIZE) {
            show_homebrew_error_screen("Legacy header too small");
            return;
        }

        /* Re-read ABI fields from the legacy header layout at RAM. */
        uint32_t required_abi, required_min;
        memcpy(&required_abi, base + 4, 4);
        memcpy(&required_min, base + 8, 4);
        if (!gwhb_abi_ok(required_abi, required_min)) {
            printf("GWHB legacy: ABI %lu/%lu, firmware %u/%lu\n",
                   (unsigned long)required_abi, (unsigned long)required_min,
                   (unsigned)GW_FIRMWARE_ABI_VERSION,
                   (unsigned long)g_firmware_abi.size);
            show_homebrew_error_screen("ABI mismatch — reflash FW");
            return;
        }

        SCB_CleanDCache_by_Addr((uint32_t *)base, copied);
        SCB_InvalidateICache();
        ((void (*)(uint8_t, uint8_t, int8_t))(((uintptr_t)base + GWHB_LEGACY_HEADER_SIZE) | 1))
            (load_state, start_paused, save_slot);
        return;
    }

    if (!gwhb_abi_ok(meta.required_abi_version, meta.required_abi_min_size)) {
        printf("GWHB: ABI req %lu/%lu, firmware %u/%lu\n",
               (unsigned long)meta.required_abi_version,
               (unsigned long)meta.required_abi_min_size,
               (unsigned)GW_FIRMWARE_ABI_VERSION,
               (unsigned long)g_firmware_abi.size);
        show_homebrew_error_screen("ABI mismatch — reflash FW");
        return;
    }

    if (meta.code_size == 0
        || (uint64_t)meta.code_size + meta.bss_size > ram_emu_len) {
        printf("GWHB: code=%lu bss=%lu ram_emu=%lu\n",
               (unsigned long)meta.code_size, (unsigned long)meta.bss_size,
               (unsigned long)ram_emu_len);
        show_homebrew_error_screen("Homebrew too big for RAM");
        return;
    }

    uint32_t payload_off = GWHB_HEADER_MIN_SIZE + (uint32_t)header_length;
    size_t loaded = rg_storage_copy_file_range_to_ram(
        (char *)path, base, payload_off, meta.code_size, NULL);
    if (loaded != meta.code_size) {
        printf("GWHB: loaded %u, expected %lu (off=%lu path=%s)\n",
               (unsigned)loaded, (unsigned long)meta.code_size,
               (unsigned long)payload_off, path);
        show_homebrew_error_screen("SD read failed — re-copy .bin");
        return;
    }

    memset(base + meta.code_size, 0, meta.bss_size);
    SCB_CleanDCache_by_Addr((uint32_t *)base, meta.code_size);
    SCB_InvalidateICache();

    /* Seed ram_malloc past code+bss, same as run_dynamic_core(). */
    ram_start = (uint32_t)(base + meta.code_size + meta.bss_size);

    g_running_core_version[0] = meta.version_major;
    g_running_core_version[1] = meta.version_minor;
    g_running_core_version[2] = meta.version_patch;
    if (meta.display_name[0]) {
        strncpy(g_running_core_name, meta.display_name, sizeof(g_running_core_name) - 1);
        g_running_core_name[sizeof(g_running_core_name) - 1] = '\0';
    } else {
        g_running_core_name[0] = '\0';
    }
    strncpy(g_running_core_path, path, sizeof(g_running_core_path) - 1);
    g_running_core_path[sizeof(g_running_core_path) - 1] = '\0';

    ((void (*)(uint8_t, uint8_t, int8_t))((uintptr_t)base | 1))
        (load_state, start_paused, save_slot);
}

/* --- Dynamic external cores (/cores/*.bin, see gnw_core_meta.h) -------
 *
 * A classic emulator core (e.g. Watara Supervision) is built as a
 * standalone ELF against the same firmware ABI, linked at
 * __RAM_EMU_START__, packaged with a "CORE" header whose header_data is a
 * gnw_core_meta_t (+ optional inline logo blobs). Unlike GWHB, the core
 * does not need to know how to zero its own BSS or pick an LCD mode: the
 * firmware does the former using the size read from metadata at scan
 * time, and classic cores always use the default RGB565 framebuffers.
 *
 * emulators_scan_cores() probes every /cores/*.bin at boot and registers
 * one tab per valid core (see add_emulator_dynamic()); run_dynamic_core()
 * does the load+zero+jump dance at launch time, mirroring the old
 * per-system run_internal_emu() but with metadata read from the file
 * instead of compile-time linker symbols. */

#if SD_CARD == 1

/* Reads only the CORE header + gnw_core_meta_t (not the payload) from
 * `path`. Returns true and fills *out_meta on success, and *out_header_length
 * with the raw header_length field (callers that need to locate the payload
 * — i.e. run_dynamic_core() — compute payload_offset = CORE_HEADER_MIN_SIZE +
 * *out_header_length; pass NULL if not needed). Rejects anything that isn't
 * a "CORE"-magic, GNW_CORE_META_VERSION container, that asks for more ABI
 * than this firmware provides, or whose segments/systems counts or regions
 * are out of range — silently (this runs over every file under /cores/ at
 * boot */
static bool gnw_core_probe(const char *path, gnw_core_meta_t *out_meta, uint16_t *out_header_length)
{
    uint8_t fixed_header[CORE_HEADER_MIN_SIZE];
    bool ok = false;
    uint16_t header_length = 0;

    FILE *file = fopen(path, "rb");
    if (!file)
        return false;

    if (fread(fixed_header, 1, sizeof(fixed_header), file) != sizeof(fixed_header))
        goto done;
    /* Silent for a non-"CORE" magic */
    if (memcmp(fixed_header, CORE_HEADER_MAGIC_EXTERNAL, 4) != 0)
        goto done;

    uint16_t header_version = read_u16_le(&fixed_header[4]);
    header_length = read_u16_le(&fixed_header[6]);
    if (header_version != GNW_CORE_META_VERSION || header_length < sizeof(*out_meta))
        goto done;

    if (fread(out_meta, 1, sizeof(*out_meta), file) != sizeof(*out_meta))
        goto done;

    if (out_meta->required_abi_version > GW_FIRMWARE_ABI_VERSION ||
        out_meta->required_abi_min_size > g_firmware_abi.size) {
        printf("CORE: '%s' needs a newer firmware ABI, skipping\n", path);
        goto done;
    }

    if (out_meta->segments_count < 1 || out_meta->segments_count > GNW_CORE_MAX_SEGMENTS ||
        out_meta->systems_count  < 1 || out_meta->systems_count  > GNW_CORE_MAX_SYSTEMS)
        goto done;
    if (out_meta->segments[0].region != GNW_CORE_REGION_RAM_EMU)
        goto done;
    for (uint32_t i = 0; i < out_meta->segments_count; i++) {
        uint32_t region = out_meta->segments[i].region;
        if (region != GNW_CORE_REGION_RAM_EMU && region != GNW_CORE_REGION_ITCM)
            goto done;
    }

    for (uint32_t i = 0; i < out_meta->systems_count; i++) {
        gnw_core_system_t *sys = &out_meta->systems[i];
        sys->system_name[sizeof(sys->system_name) - 1] = '\0';
        sys->dirname[sizeof(sys->dirname) - 1] = '\0';
        sys->extensions[sizeof(sys->extensions) - 1] = '\0';
    }
    ok = true;

done:
    fclose(file);
    if (ok && out_header_length)
        *out_header_length = header_length;
    return ok;
}

/* Loads a `retro_logo_image` blob (raw width/height/packed-1bpp bytes,
 * see bitmaps.h) at absolute file offset `offset`/`size` into a freshly
 * ahb_calloc'd buffer — persists for the lifetime of the menu, same as
 * the compile-time logos baked into /bios/logo.bin. */
static const retro_logo_image *gnw_core_load_logo(const char *path, uint32_t offset, uint32_t size)
{
    if (size < sizeof(retro_logo_image) || size > 8192)
        return NULL;

    FILE *file = fopen(path, "rb");
    if (!file)
        return NULL;

    retro_logo_image *img = NULL;
    if (fseek(file, (long)offset, SEEK_SET) == 0) {
        img = ahb_calloc(1, size);
        if (img && fread(img, 1, size, file) != size)
            img = NULL; /* AHB malloc; keep for menu lifetime (no heap rewind) */
    }
    fclose(file);
    return img;
}

/* Registers one launcher tab per system described in `meta` (up to
 * GNW_CORE_MAX_SYSTEMS), all sharing the same core_path — this is how one
 * core binary (e.g. pce.bin) can expose several tabs (PC Engine + PC Engine
 * CD), each with its own dirname/extensions/logos/parse_type. The core
 * itself is responsible for telling its systems apart at runtime (typically
 * via ACTIVE_FILE->ext), same as the old compile-time build did. */
static void add_emulator_dynamic(const gnw_core_meta_t *meta, const char *core_path)
{
    for (uint32_t i = 0; i < meta->systems_count; i++) {
        const gnw_core_system_t *sys = &meta->systems[i];

        if (emulators_count >= emulators_capacity) {
            printf("CORE: '%s' system '%s' ignored, emulator table full (%d)\n",
                   core_path, sys->system_name, emulators_capacity);
            return;
        }

        int16_t pad_idx = RG_LOGO_EMPTY, header_idx = RG_LOGO_EMPTY;
        if (sys->pad_logo_size)
            pad_idx = rg_register_dynamic_logo(gnw_core_load_logo(core_path, sys->pad_logo_offset, sys->pad_logo_size));
        if (sys->header_logo_size)
            header_idx = rg_register_dynamic_logo(gnw_core_load_logo(core_path, sys->header_logo_offset, sys->header_logo_size));

        add_emulator_ex(sys->system_name, sys->dirname, sys->extensions, pad_idx, header_idx,
                        sys->parse_type, core_path);

        printf("CORE: registered '%s' (%s) from %s, parse_type=%lu\n",
              sys->system_name, sys->dirname, core_path, (unsigned long)sys->parse_type);
    }
}

/* Scans /cores/*.bin (FatFs) and registers one tab per system in each
 * probe-able core. Files that aren't a "CORE"/GNW_CORE_META_VERSION
 * container are silently skipped. */
static int count_core_systems(void)
{
    DIR dir;
    FILINFO fno;
    gnw_core_meta_t meta;
    char path[128];
    int total = 0;

    if (f_opendir(&dir, "/cores") != FR_OK)
        return 0;

    while (f_readdir(&dir, &fno) == FR_OK && fno.fname[0] != 0) {
        if (fno.fattrib & AM_DIR)
            continue;
        const char *ext = get_extension(fno.fname);
        if (!ext || strcasecmp(ext, "bin") != 0)
            continue;

        snprintf(path, sizeof(path), "/cores/%s", fno.fname);
        if (gnw_core_probe(path, &meta, NULL))
            total += (int)meta.systems_count;
    }

    f_closedir(&dir);
    return total;
}

static void emulators_scan_cores(void)
{
    DIR dir;
    FILINFO fno;
    gnw_core_meta_t meta;
    char path[128];

    if (f_opendir(&dir, "/cores") != FR_OK)
        return;

    while (f_readdir(&dir, &fno) == FR_OK && fno.fname[0] != 0) {
        if (fno.fattrib & AM_DIR)
            continue;
        const char *ext = get_extension(fno.fname);
        if (!ext || strcasecmp(ext, "bin") != 0)
            continue;

        snprintf(path, sizeof(path), "/cores/%s", fno.fname);
        if (gnw_core_probe(path, &meta, NULL))
            add_emulator_dynamic(&meta, path);
    }

    f_closedir(&dir);
}

#endif /* SD_CARD == 1 */

/* Resolves segment region `region` to its fixed base address + max usable
 * length (see gnw_core_region_t / ld/gnw_itcm_core.ld). Returns NULL (and
 * *out_max_len = 0) for an unsupported region.
 *
 * CAUTION: the returned base pointer is NOT a valid "unsupported region"
 * sentinel by itself — GNW_CORE_REGION_ITCM's real base is 0x00000000
 * (Cortex-M7 maps ITCM at address 0), which is bit-identical to the NULL
 * this function returns for a genuinely unsupported region. Callers MUST
 * check *out_max_len == 0, not `!base`, to detect failure (see the bug this
 * comment replaced in run_dynamic_core()). */
static uint8_t *dynamic_core_region_base(uint32_t region, uint32_t *out_max_len)
{
    switch (region) {
    case GNW_CORE_REGION_RAM_EMU:
        if (out_max_len)
            *out_max_len = (uint32_t)&__RAM_EMU_END__ - (uint32_t)&__RAM_EMU_START__;
        return (uint8_t *)&__RAM_EMU_START__;
    case GNW_CORE_REGION_ITCM:
        if (out_max_len)
            *out_max_len = (uint32_t)&__ITCM_CORE_LENGTH__;
        return (uint8_t *)&__ITCM_CORE_START__;
    default:
        if (out_max_len)
            *out_max_len = 0;
        return NULL;
    }
}

/* Re-probes `core_path`'s gnw_core_meta_t at launch time (cheap header-only
 * read, done instead of caching code/bss sizes in retro_emulator_t — see
 * rg_emulators.h) to get the live segment list. For each segment: resolves
 * its fixed region base address, bounded-reads `code_size` bytes from the
 * right file offset into it, zeroes `bss_size` bytes right after. For ITCM
 * segments, immediately bump-reserves that same code+bss span via
 * itc_malloc (after itc_init() by the caller) so the core's own later
 * itc_* allocations never collide with its fixed segment; the entry symbol
 * is never resolved at firmware link time, the core provides its own
 * trampoline (see cores/_template) because it is a completely separate ELF.
 * Also seeds `ram_start` to right past segment 0's code+bss before jumping
 * in — see the comment at that assignment below. */
__attribute__((noinline))
static void run_dynamic_core(const char *core_path, uint8_t load_state, uint8_t start_paused, int8_t save_slot)
{
    gnw_core_meta_t meta;
    uint16_t header_length;
    uint8_t *entry_base = NULL;

    if (!gnw_core_probe(core_path, &meta, &header_length)) {
        show_corrupted_installation_screen();
        return;
    }

    g_running_core_version[0] = meta.version_major;
    g_running_core_version[1] = meta.version_minor;
    g_running_core_version[2] = meta.version_patch;
    meta.core_name[sizeof(meta.core_name) - 1] = '\0';
    strncpy(g_running_core_name, meta.core_name, sizeof(g_running_core_name) - 1);
    g_running_core_name[sizeof(g_running_core_name) - 1] = '\0';
    strncpy(g_running_core_path, core_path, sizeof(g_running_core_path) - 1);
    g_running_core_path[sizeof(g_running_core_path) - 1] = '\0';

    uint32_t file_offset = CORE_HEADER_MIN_SIZE + (uint32_t)header_length;

    for (uint32_t i = 0; i < meta.segments_count; i++) {
        const gnw_core_segment_t *seg = &meta.segments[i];
        uint32_t region_len = 0;
        uint8_t *base = dynamic_core_region_base(seg->region, &region_len);

        /* region_len == 0, not !base: ITCM's legitimate base address is
         * 0x00000000 (Cortex-M7 maps ITCM at address 0), numerically
         * identical to the NULL sentinel dynamic_core_region_base() returns
         * for an actually-unsupported region — a base-pointer check here
         * would reject every valid ITCM segment (which is every core built
         * with CORE_EXTRA_SEGMENTS=itcm:..., e.g. cores/pce) as "too big"
         * and show the corrupted-installation screen. region_len is always
         * a nonzero constant for RAM_EMU/ITCM and is explicitly zeroed only
         * in the `default:` case, so it's an unambiguous invalid-region
         * sentinel. */
        if (region_len == 0 || (uint64_t)seg->code_size + seg->bss_size > region_len) {
            show_corrupted_installation_screen();
            return;
        }

        size_t loaded = seg->code_size
            ? rg_storage_copy_file_range_to_ram((char *)core_path, base, file_offset, seg->code_size, NULL)
            : 0;
        if (seg->code_size && loaded != seg->code_size) {
            show_corrupted_installation_screen();
            return;
        }

        memset(base + seg->code_size, 0, seg->bss_size);
        SCB_CleanDCache_by_Addr((uint32_t *)base, seg->code_size);
        SCB_InvalidateICache();

        if (i == 0) {
            entry_base = base;
            /* Seed the shared RAM_EMU bump pool (ram_start/ram_malloc, see
             * gw_malloc.c) to right past this segment's own code+bss, same
             * value each core used to have to compute itself as
             * &__CORE_BSS_END__ (see e.g. main_wsv.c/main_pce.c) — this
             * firmware-side metadata already carries the exact code_size +
             * bss_size pack_core.py measured off that same symbol, so doing
             * it once here removes the need for every core's own main_*.c
             * to remember to set it, and — unlike a core doing it lazily on
             * its first ROM-data callback — guarantees ram_malloc()/
             * ram_get_free_size() are already valid the moment the entry
             * trampoline is jumped to below, including during a C++ core's
             * global constructors (gw_core_entry.S's .init_array loop runs
             * before CORE_ENTRY, e.g. cores/gb_tgbdual's operator new). */
            ram_start = (uint32_t)(base + seg->code_size + seg->bss_size);
        } else {
            /* Extra loadable segments are ITCM-only. Reserve the span in the
             * ITCM bump so later itc_* allocs start past the loaded code+bss. */
            void *reserved = itc_malloc(seg->code_size + seg->bss_size);
            if (reserved != base) {
                show_corrupted_installation_screen();
                return;
            }
        }

        file_offset += seg->code_size;
    }

    ((void (*)(uint8_t, uint8_t, int8_t))((uintptr_t)entry_base | 1))(load_state, start_paused, save_slot);
}

bool rg_emulators_get_running_core_version(uint8_t *major, uint8_t *minor, uint8_t *patch)
{
    if ((g_running_core_version[0] | g_running_core_version[1] | g_running_core_version[2]) == 0)
        return false;
    if (major)
        *major = g_running_core_version[0];
    if (minor)
        *minor = g_running_core_version[1];
    if (patch)
        *patch = g_running_core_version[2];
    return true;
}

bool rg_emulators_get_running_core_info(char *name, size_t name_sz,
                                        char *version, size_t version_sz,
                                        char *path, size_t path_sz,
                                        char *date, size_t date_sz)
{
    if (g_running_core_path[0] == '\0')
        return false;

    if (name && name_sz > 0) {
        if (g_running_core_name[0])
            snprintf(name, name_sz, "%s", g_running_core_name);
        else
            snprintf(name, name_sz, "%s", "-");
    }

    if (version && version_sz > 0) {
        if ((g_running_core_version[0] | g_running_core_version[1] | g_running_core_version[2]) != 0)
            snprintf(version, version_sz, "v%u.%u.%u",
                     g_running_core_version[0], g_running_core_version[1], g_running_core_version[2]);
        else
            snprintf(version, version_sz, "%s", "-");
    }

    if (path && path_sz > 0)
        snprintf(path, path_sz, "%s", g_running_core_path);

    if (date && date_sz > 0) {
        date[0] = '\0';
#if SD_CARD == 1
        FILINFO fno;
        if (f_stat(g_running_core_path, &fno) == FR_OK && fno.fdate != 0) {
            /* FatFs: fdate = YYYYYYYMMMMDDDDD (year since 1980),
             *        ftime = HHHHHMMMMMMSSSSS (seconds/2). */
            unsigned year = 1980 + (fno.fdate >> 9);
            unsigned month = (fno.fdate >> 5) & 0x0F;
            unsigned day = fno.fdate & 0x1F;
            unsigned hour = fno.ftime >> 11;
            unsigned min = (fno.ftime >> 5) & 0x3F;
            snprintf(date, date_sz, "%04u-%02u-%02u %02u:%02u",
                     year, month, day, hour, min);
        }
#endif
        if (date[0] == '\0')
            snprintf(date, date_sz, "%s", "-");
    }

    return true;
}

void emulator_start(retro_emulator_file_t *file, bool load_state, bool start_paused, int8_t save_slot)
{
    if (file->ext == NULL)
        return;

    printf("Retro-Go: Starting game: %s\n", file->name);
    // odroid_settings_StartAction_set(load_state ? ODROID_START_ACTION_RESUME : ODROID_START_ACTION_NEWGAME);
    // odroid_settings_commit();

    // create a copy in heap ram as ram used by ram_malloc will be erase by emulator
    retro_emulator_file_t *newfile = calloc(sizeof(retro_emulator_file_t),1);
    memcpy(newfile,file,sizeof(retro_emulator_file_t));
    strcpy((char *)newfile->name,file->name);
    strcpy(newfile->path,file->path);
    newfile->ext = get_extension(newfile->path);

    /* Snapshotted into local stack buffers, NOT kept as pointers into
     * emulators[]/systems[]: those arrays are dtc_calloc()'d and live in
     * the DTCM bump. dtc_init() below forgets that bump and the core may
     * immediately allocate over the same addresses, so dangling pointers
     * into system_name/core_path would be clobbered. Copy the strings out
     * before dtc_init()/ram_start=0.
     *
     * newfile->system is a rom_system_t*, whose system_name/core_path
     * fields are `char *`/`const char *` (pointers aliasing the real
     * fixed-size arrays in retro_emulator_t, see rom_manager.h) — sizing
     * these buffers off newfile->system->system_name/core_path directly
     * would take sizeof(a pointer) and truncate the copy after 3-4 bytes.
     * Use retro_emulator_t's actual array sizes instead. */
    char system_name[sizeof(((retro_emulator_t *)0)->system_name)];
    char dyn_core_path_buf[sizeof(((retro_emulator_t *)0)->core_path)];
    strncpy(system_name, newfile->system->system_name, sizeof(system_name) - 1);
    system_name[sizeof(system_name) - 1] = '\0';
    dyn_core_path_buf[0] = '\0';
    /* Homebrew (and any system without an external CORE) keeps core_path
     * NULL on rom_system_t — never strncpy from a NULL pointer. */
    if (newfile->system->core_path && newfile->system->core_path[0]) {
        strncpy(dyn_core_path_buf, newfile->system->core_path, sizeof(dyn_core_path_buf) - 1);
        dyn_core_path_buf[sizeof(dyn_core_path_buf) - 1] = '\0';
    }
    const char *dyn_core_path = dyn_core_path_buf[0] ? dyn_core_path_buf : NULL;

    ACTIVE_FILE = newfile;
#if CHEAT_CODES == 1
    CHOSEN_FILE = newfile;

    emulator_update_cheats_info(CHOSEN_FILE);
#endif

    /* Cleared here; run_dynamic_core() re-fills after a successful probe.
     * Homebrew leave it unset so the pause menu hides Info. */
    g_running_core_name[0] = '\0';
    g_running_core_path[0] = '\0';
    g_running_core_version[0] = g_running_core_version[1] = g_running_core_version[2] = 0;

    /* systems[] lives in the DTCM bump and is wiped by dtc_init(). In-game
     * code must not touch ACTIVE_FILE->system (use handlers / path instead). */
    newfile->system = NULL;

    // It will free all ram allocated memory for use by emulators
    ram_init();
    itc_init();
    dtc_init();
    ram_start = 0;
    emulators = NULL;
    systems = NULL;
    // some pointers were freed, set them to null
    rg_reset_logo_buffers();

    // Refresh watchdog here in case previous actions did not refresh it
    wdog_refresh();

    if (dyn_core_path) {
      run_dynamic_core(dyn_core_path, load_state, start_paused, save_slot);
    } else if (strcmp(system_name, "Homebrew") == 0
               || strstr(ACTIVE_FILE->path, "/homebrew/") != NULL) {
      run_gwhb_homebrew(ACTIVE_FILE->path, load_state, start_paused, save_slot);
    }

#if CHEAT_CODES == 1
    for (int i = 0; i < newfile->cheat_count; i++) {
        if (newfile->cheat_codes[i]) free(newfile->cheat_codes[i]);
        if (newfile->cheat_descs[i]) free(newfile->cheat_descs[i]);
    }
    if (newfile->cheat_codes) free(newfile->cheat_codes);
    if (newfile->cheat_descs) free(newfile->cheat_descs);
#endif
#ifdef GNW_DISABLE_COMPRESSION
// we need to keep extension for compression detection
    free(newfile);
#endif

    ram_init();
    itc_init();
    dtc_init();
    ram_start = 0;
#if SD_CARD == 1
    // some pointers were freed, set them to null
    rg_reset_logo_buffers();
#endif
}

void emulators_init()
{
    int from_cores = 0;
#if SD_CARD == 1
    from_cores = count_core_systems();
#endif
    /* Exact fit: builtins + every system described by CORE headers on the
     * SD card. AHB is a bump allocator (no realloc), so size once up front. */
    emulators_capacity = BUILTIN_SYSTEM_EMULATORS + from_cores;
    if (emulators_capacity < BUILTIN_SYSTEM_EMULATORS)
        emulators_capacity = BUILTIN_SYSTEM_EMULATORS;

    /* After dtc_init() (cold boot or return from a core) the previous
     * emulators[]/gui.tabs allocations are gone — drop dangling pointers
     * before allocating fresh tables. */
    emulators = NULL;
    systems = NULL;
    emulators_count = 0;
    gui.tabs = NULL;
    gui.tab_capacity = 0;
    gui.tabcount = 0;
    gui.selected = 0;

    emulators = (retro_emulator_t *)dtc_calloc((size_t)emulators_capacity, sizeof(retro_emulator_t));
    systems = (rom_system_t *)dtc_calloc((size_t)emulators_capacity, sizeof(rom_system_t));

    /* Favorites tab + one launcher tab per emulator slot. */
    gui_ensure_tab_capacity(1 + emulators_capacity);

    /* ★ Favorites must be the FIRST tab (index 0), before every system tab. */
    rg_favorites_register_tab();

    /* Every classic emulator (gb, gba, nes, sms family, msx, genesis, pce,
     * wsv, atari family, tama, pkmini, amstrad, gw) and the legacy
     * zelda3/smw/celeste "homebrew" used to be registered here via
     * add_emulator(...) with compile-time logos/extensions/dirname. They
     * are being migrated to standalone cores/<system>/ builds, discovered
     * dynamically at boot from /cores/*.bin (see emulators_scan_cores(),
     * "Cores externes avec ABI" plan). Until a system is migrated it has
     * no tab at all. */
    add_emulator("Homebrew", "homebrew", "bin", RG_LOGO_EMPTY, RG_LOGO_HEADER_HOMEBREW);

#if SD_CARD == 1
    /* Migrated systems (Watara Supervision, ...) register themselves here
     * by dropping a packaged .bin under /cores/ on the SD card — no
     * firmware rebuild needed to add/update/remove one. Capacity was
     * sized from count_core_systems() above so new cores are not dropped. */
    emulators_scan_cores();
    printf("CORE: %d system tab(s) (%d from /cores, capacity %d)\n",
           emulators_count, from_cores, emulators_capacity);
#endif
}

static bool browse_subpath_is_safe(const char *s)
{
    if (s == NULL || s[0] == '\0')
        return true;
    if (s[0] == '/')
        return false;
    if (strstr(s, "..") != NULL)
        return false;
    return true;
}

void rg_emulators_restore_main_menu_browse_path(void)
{
    char buf[40];

    if (!odroid_settings_MainMenuBrowseSubpath_get(buf, sizeof(buf)))
        return;
    if (!browse_subpath_is_safe(buf)) {
        odroid_settings_MainMenuBrowseSubpath_set("");
        odroid_settings_commit();
        return;
    }

    uint16_t tab_idx = odroid_settings_MainMenuSelectedTab_get();
    tab_t *t = gui_get_tab(tab_idx);
    if (t == NULL || t->arg == NULL)
        return;

    retro_emulator_t *emu = (retro_emulator_t *)t->arg;
    strncpy(emu->browse_subpath, buf, sizeof(emu->browse_subpath) - 1);
    emu->browse_subpath[sizeof(emu->browse_subpath) - 1] = '\0';

    char folder[RG_PATH_MAX];
    if (emu->browse_subpath[0])
        snprintf(folder, sizeof(folder), "%s/%s/%s", RG_BASE_PATH_ROMS, emu->dirname, emu->browse_subpath);
    else
        snprintf(folder, sizeof(folder), "%s/%s", RG_BASE_PATH_ROMS, emu->dirname);

    rg_stat_t st = rg_storage_stat(folder);
    if (!st.exists || !st.is_dir)
        emu->browse_subpath[0] = '\0';

}

bool emulator_is_file_valid(retro_emulator_file_t *file)
{
    for (int i = 0; i < emulators_count; i++) {
        for (int j = 0; j < emulators[i].roms.count; j++) {
            if (&emulators[i].roms.files[j] == file) {
                return true;
            }
        }
    }

    return false;
}

retro_emulator_file_t *emulator_get_file(char *file_path)
{
    for (int i = 0; i < emulators_count; i++) {
        char prefix[RG_PATH_MAX + 24];
        snprintf(prefix, sizeof(prefix), "%s/%s/", RG_BASE_PATH_ROMS, emulators[i].dirname);
        size_t plen = strlen(prefix);
        if (strncmp(file_path, prefix, plen) != 0)
            continue;

        bool was_initialized = emulators[i].initialized;
        char saved_sub[sizeof(emulators[i].browse_subpath)];
        memcpy(saved_sub, emulators[i].browse_subpath, sizeof(saved_sub));
        const char *rel = file_path + plen;
        const char *slash = strrchr(rel, '/');
        if (slash) {
            size_t sublen = (size_t)(slash - rel);
            if (sublen >= sizeof(emulators[i].browse_subpath))
                continue;
            memcpy(emulators[i].browse_subpath, rel, sublen);
            emulators[i].browse_subpath[sublen] = '\0';
        } else {
            emulators[i].browse_subpath[0] = '\0';
        }

        emulators[i].roms.count = 0;
        emulator_refresh_list(&emulators[i]);
        for (int j = 0; j < emulators[i].roms.count; j++) {
            if (strcmp(emulators[i].roms.files[j].path, file_path) == 0)
                return &emulators[i].roms.files[j];
        }
        memcpy(emulators[i].browse_subpath, saved_sub, sizeof(saved_sub));
        if (was_initialized) {
            emulators[i].roms.count = 0;
            emulator_refresh_list(&emulators[i]);
        }
    }
    return NULL;
}
