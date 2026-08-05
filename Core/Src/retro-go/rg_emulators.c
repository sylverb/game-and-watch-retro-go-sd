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
 * entry points directly (see emulators_scan_cores() / run_dynamic_core()).
 * main_pico8.h stays: PICO-8 is out of scope for this migration. */
#include "main_pico8.h"
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
// INTERNAL_CORE_BIN_HEADER_VERSION is defined in Makefile.common
// and shall be incremented when the internal cores binary format
// changes
#define INTERNAL_CORE_HEADER_VERSION ((uint16_t)(INTERNAL_CORE_BIN_HEADER_VERSION))

// Minimum version accepted for external cores (e.g. pico8.bin). Bump when
// the engine binary's runtime ABI changes in a way that requires users to
// update the engine — older binaries are rejected with a clear message.
#define EXTERNAL_CORE_HEADER_MIN_VERSION ((uint16_t)1u)

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

/* Consolidated single-fail-label form: ~14 separate cleanup branches
 * collapsed to one. Behavior unchanged — any failure prints a generic
 * load-failed message, frees header_data, closes file, shows the
 * corruption screen (suppressed for external-core failures), returns 0. */
static size_t load_core_bin_with_header(const char *file_path, uint8_t *dest_address)
{
  uint8_t fixed_header[CORE_HEADER_MIN_SIZE];
  uint8_t *header_data = NULL;
  bool is_external_core = false;
  size_t result = 0;

  FILE *file = fopen(file_path, "rb");
  if (!file) goto fail;
  if (fread(fixed_header, 1, sizeof(fixed_header), file) != sizeof(fixed_header)) goto fail;

  bool is_internal_core = (memcmp(fixed_header, CORE_HEADER_MAGIC_INTERNAL, 4) == 0);
  is_external_core      = (memcmp(fixed_header, CORE_HEADER_MAGIC_EXTERNAL, 4) == 0);
  if (!is_internal_core && !is_external_core) goto fail;

  uint16_t header_version = read_u16_le(&fixed_header[4]);
  uint16_t header_length  = read_u16_le(&fixed_header[6]);

  if (header_length > 0) {
    header_data = (uint8_t *)malloc(header_length);
    if (!header_data) goto fail;
    if (fread(header_data, 1, header_length, file) != header_length) goto fail;
  }

  if (is_internal_core) {
    if (header_version != INTERNAL_CORE_HEADER_VERSION) goto fail;
    if (header_length < 1) goto fail;
    uint8_t tag_len = header_data[0];
    size_t expected_tag_len = strlen(GIT_TAG);
    if ((uint16_t)(1u + tag_len) > header_length ||
        tag_len != expected_tag_len ||
        memcmp(&header_data[1], GIT_TAG, tag_len) != 0) goto fail;
  } else if (header_version < EXTERNAL_CORE_HEADER_MIN_VERSION) {
    goto fail;
  }

  uint32_t payload_offset = CORE_HEADER_MIN_SIZE + (uint32_t)header_length;
  long file_size;
  if (fseek(file, 0, SEEK_END) != 0) goto fail;
  file_size = ftell(file);
  if (file_size < 0 || (uint32_t)file_size < payload_offset) goto fail;

  free(header_data);
  fclose(file);
  return odroid_overlay_cache_file_in_ram_with_offset(file_path, dest_address, payload_offset);

fail:
  printf("CORE: load failed '%s'\n", file_path);
  if (header_data) free(header_data);
  if (file) fclose(file);
  if (!is_external_core) show_corrupted_installation_screen();
  return result;
}


/* Exposed for ITCM sentinel patching (main_pico8.c) */
uint8_t *pico8_code_flash_addr = NULL;
uint32_t pico8_code_flash_size = 0;

/**
 * PatchPico8SentinelRefs - Patches 0xBEEF0000-range sentinel addresses
 * in a memory region to point to the actual QSPI XIP flash address.
 */
#define PICO8_CODE_BASE 0xBEEF0000
#define PICO8_CODE_CACHE_SIZE (128 * 1024u)

static int PatchPico8Region(uint32_t *start, uint32_t *end, int32_t offset, uint32_t code_size)
{
  int patched = 0;
  for (uint32_t *ptr = start; ptr < end; ptr++) {
    uint32_t value = *ptr;
    /* Check for sentinel range (including Thumb bit 0) */
    if ((value & ~1) >= PICO8_CODE_BASE && (value & ~1) < PICO8_CODE_BASE + code_size) {
      *ptr = value + offset;
      patched++;
    }
  }
  return patched;
}

/**
 * Pico8CacheCodeToFlash - Cache pico8.ro to XIP flash with sentinel patching.
 *
 * With SD card, /cores/pico8.ro is copied into the normal flash cache and
 * patched in place. With FrogFS, the source file is read-only inside the
 * FrogFS image, so the patched copy is written to a dedicated cache window at
 * the end of the firmware extflash payload region.
 */
static uint8_t *Pico8CacheCodeToFlash(uint32_t *code_size_out)
{
  printf("P8: caching pico8.ro to flash...\n");

  /* Step 1: Cache or map the source file. */
  uint8_t *code_addr = odroid_overlay_cache_file_in_flash("/cores/pico8.ro", code_size_out, false);
  if (!code_addr) {
    printf("P8: pico8.ro cache FAILED (not found on SD?)\n");
    return NULL;
  }
  if (*code_size_out == 0) {
    printf("P8: pico8.ro cache returned size 0\n");
    return NULL;
  }
#if SD_CARD == 1
  int32_t offset = (int32_t)((uint32_t)code_addr - PICO8_CODE_BASE);
  printf("P8: pico8.ro cached at %p, size=%lu, offset=%ld\n",
         code_addr, (unsigned long)*code_size_out, (long)offset);

  uint8_t *target_addr = code_addr;

  /* Step 2: Copy source content to RAM_EMU (temp buffer, overwritten by pico8.bin later). */
  printf("P8: copying %lu bytes from flash to RAM for patching...\n",
         (unsigned long)*code_size_out);
  uint8_t *ram_buf = (uint8_t *)__RAM_EMU_START__;
  memcpy(ram_buf, code_addr, *code_size_out);

  /* Step 3: Patch all sentinel addresses in the RAM copy */
  int patched = PatchPico8Region((uint32_t *)ram_buf,
                                 (uint32_t *)(ram_buf + *code_size_out),
                                 offset, *code_size_out);
  printf("P8: patched %d sentinel refs in code blob\n", patched);

  if (patched > 0) {
    /* Step 4: Program the patched content to XIP flash. */
    uint32_t flash_offset = (uint32_t)target_addr - (uint32_t)&__EXTFLASH_BASE__;
    uint32_t erase_size = (*code_size_out + 4095) & ~4095u;  /* Round up to 4KB */

    printf("P8: reprogramming flash at offset 0x%08lX, erase=%lu, prog=%lu\n",
           (unsigned long)flash_offset, (unsigned long)erase_size,
           (unsigned long)*code_size_out);

    OSPI_DisableMemoryMappedMode();
    OSPI_EraseSync(flash_offset, erase_size);
    OSPI_Program(flash_offset, ram_buf, *code_size_out);
    OSPI_EnableMemoryMappedMode();

    /* Step 5: Verify first word was patched correctly */
    uint32_t first_word = *(uint32_t *)target_addr;
    printf("P8: flash reprogram done. first word: 0x%08lX\n",
           (unsigned long)first_word);
  } else {
    printf("P8: no sentinel refs found (already patched from previous boot)\n");
  }
  return target_addr;
  #else
  return code_addr;
  #endif
}

const unsigned char *ROM_DATA = NULL;
unsigned ROM_DATA_LENGTH;
const char *ROM_EXT = NULL;
retro_emulator_file_t *ACTIVE_FILE = NULL;

static retro_emulator_file_t *shared_files = NULL;

#if !defined(COVERFLOW)
#define COVERFLOW 0
#endif /* COVERFLOW */
// Increase when adding new emulators
#define MAX_EMULATORS 22
static retro_emulator_t *emulators;
static rom_system_t *systems;
static int emulators_count = 0;

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
 * (Homebrew, PICO-8). Several tabs may share the same core_path (one core
 * binary exposing multiple systems, e.g. PC Engine + PC Engine CD — see
 * add_emulator_dynamic()). */
static void add_emulator_ex(const char *system, const char *dirname, const char* ext,
                            int16_t logo_idx, int16_t header_idx, game_data_type_t game_data_type,
                            uint32_t parse_type, const char *core_path)
{
    assert(emulators_count < MAX_EMULATORS);
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
    s->game_data_type = game_data_type;
    s->core_path = p->core_path[0] ? p->core_path : NULL;
    s->parse_type = parse_type;

    gui_add_tab(dirname, logo_idx, header_idx, p, event_handler);
}

static void add_emulator(const char *system, const char *dirname, const char* ext,
                         uint16_t logo_idx, uint16_t header_idx, game_data_type_t game_data_type)
{
    add_emulator_ex(system, dirname, ext, (int16_t)logo_idx, (int16_t)header_idx, game_data_type,
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
#endif
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
 * and PICO-8 are untouched by this migration and keep their explicit
 * blocks below. */

/* --- Universal Homebrew Header (GWHB) loader ---------------------------
 *
 * Lets an out-of-tree homebrew binary run without any firmware-side
 * dispatch-table entry, linker overlay symbols, or appid.h enum: drop a
 * .bin under /roms/homebrew/ and it runs, as long as it starts with a
 * gwhb_header_t (see gwhb.h). The entry point is always at offset
 * sizeof(gwhb_header_t), past the header.
 *
 * Unlike the compile-time dispatch table (run_internal_emu above), a GWHB
 * binary is responsible for zeroing its own BSS and configuring its own
 * LCD mode (RGB565 vs LUT8) via the firmware ABI, since the loader has no
 * compile-time knowledge of its layout or needs, only its total size.
 *
 * Trust model: the file is loaded, unauthenticated, from an SD card, so
 * every firmware-side check below is defensive: refuse rather than jump
 * into a corrupt or incompatible binary. */

static void show_incompatible_homebrew_screen(void)
{
  odroid_dialog_choice_t choices[] = {
    {0, curr_lang->s_Corrupted_Install_1, "", -1, NULL},
    ODROID_DIALOG_CHOICE_SEPARATOR,
    {1, curr_lang->s_OK, "", 1, NULL},
    ODROID_DIALOG_CHOICE_LAST,
  };

  (void)odroid_overlay_dialog(curr_lang->s_Corrupted_Title, choices, 2, NULL, 0);
}

/* `copied` is the byte count already placed at __RAM_EMU_START__ by the
 * Homebrew branch's bounded copy (see emulator_start); this function does
 * not touch storage itself, only validates and dispatches. */
__attribute__((noinline))
static void run_gwhb_homebrew(size_t copied, uint8_t load_state, uint8_t start_paused, int8_t save_slot)
{
    if (copied < sizeof(gwhb_header_t)) {
        show_incompatible_homebrew_screen();
        return;
    }

    const gwhb_header_t *hdr = (const gwhb_header_t *)&__RAM_EMU_START__;

    if (hdr->magic != GWHB_MAGIC)
        return; /* not a GWHB file; nothing to dispatch */

    /* Defense in depth, both against the same fields the app is expected
     * to self-check (see gnw_abi_ok()-style checks in ABI consumers), so a
     * binary built for a newer/bigger ABI than this firmware provides is
     * refused before it ever gets a chance to call through a function
     * pointer past the end of g_firmware_abi.
     *
     * required_abi alone is not enough: append-only ABI growth does not
     * bump GW_FIRMWARE_ABI_VERSION (see the comment above that define), so
     * two firmware builds can report the same version with different
     * actual struct sizes. required_abi_min_size is the field that
     * actually detects "this firmware predates a field I need". Anything
     * built for an older/smaller ABI is fine, hence <=, not ==. */
    if (hdr->required_abi > GW_FIRMWARE_ABI_VERSION ||
        hdr->required_abi_min_size > g_firmware_abi.size) {
        show_incompatible_homebrew_screen();
        return;
    }

    SCB_CleanDCache_by_Addr((uint32_t *)&__RAM_EMU_START__, copied);
    SCB_InvalidateICache();

    /* | 1 keeps the CPU in Thumb mode. The binary zeroes its own BSS on
     * entry. */
    ((void (*)(uint8_t, uint8_t, int8_t))(((uintptr_t)&__RAM_EMU_START__ + sizeof(gwhb_header_t)) | 1))
        (load_state, start_paused, save_slot);
}

/* --- Dynamic external cores (/cores/*.bin, see gnw_core_meta.h) -------
 *
 * A classic emulator core (e.g. Watara Supervision) is built as a
 * standalone ELF against the same firmware ABI as PICO-8/GWHB, linked at
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
 * boot, including pico8.bin/pico8_stub.bin which intentionally don't carry
 * this metadata). */
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
    /* Silent for a non-"CORE" magic (also runs over pico8.bin/pico8_stub.bin
     * etc. which intentionally don't carry this format — logging here would
     * be pure noise on every boot). */
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
        if (out_meta->segments[i].region == GNW_CORE_REGION_DTCM)
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
            img = NULL; /* leaked in the AHB bump pool, reclaimed at next ahb_init() */
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

        if (emulators_count >= MAX_EMULATORS) {
            printf("CORE: '%s' system '%s' ignored, MAX_EMULATORS reached\n", core_path, sys->system_name);
            return;
        }

        int16_t pad_idx = RG_LOGO_EMPTY, header_idx = RG_LOGO_EMPTY;
        if (sys->pad_logo_size)
            pad_idx = rg_register_dynamic_logo(gnw_core_load_logo(core_path, sys->pad_logo_offset, sys->pad_logo_size));
        if (sys->header_logo_size)
            header_idx = rg_register_dynamic_logo(gnw_core_load_logo(core_path, sys->header_logo_offset, sys->header_logo_size));

        add_emulator_ex(sys->system_name, sys->dirname, sys->extensions, pad_idx, header_idx,
                        NO_GAME_DATA, sys->parse_type, core_path);

        printf("CORE: registered '%s' (%s) from %s, parse_type=%lu\n",
              sys->system_name, sys->dirname, core_path, (unsigned long)sys->parse_type);
    }
}

/* Scans /cores/*.bin (FatFs) and registers one tab per system in each
 * probe-able core. Files that aren't a "CORE"/GNW_CORE_META_VERSION
 * container (pico8.bin, pico8_stub.bin, pico8.ro, ...) are silently
 * skipped. */
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
 * length (see gnw_core_region_t / ld/gnw_itcm_core.ld / ld/gnw_ahb_core.ld).
 * Returns NULL (and *out_max_len = 0) for an unsupported region (DTCM is
 * already rejected earlier, by gnw_core_probe()).
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
    case GNW_CORE_REGION_AHB:
        if (out_max_len)
            *out_max_len = (uint32_t)&__AHB_CORE_LENGTH__;
        return (uint8_t *)&__AHB_CORE_START__;
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
 * right file offset into it, zeroes `bss_size` bytes right after. For
 * ITCM/AHB segments, immediately bump-reserves that same code+bss span in
 * the region's runtime allocator (itc_malloc/ahb_malloc, called right after
 * itc_init()/ahb_init() by the caller) so the core's own later runtime
 * allocations never collide with its fixed segment — same technique as
 * PICO-8's ITCM back-page allocation (see docs/PICO8_EXTERNAL_MODULE.md).
 * Segment 0 is always RAM_EMU and owns the entry trampoline at offset 0 —
 * same offset-0-Thumb-jump convention as GWHB and PICO-8; the entry symbol
 * is never resolved at firmware link time, the core provides its own
 * trampoline (see cores/_template) because it is a completely separate ELF. */
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
         * a nonzero constant (0xB5000/0x10000/0x1e000) for the three real
         * regions and is explicitly zeroed only in the `default:` case, so
         * it's an unambiguous invalid-region sentinel. */
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
        } else {
            void *reserved = (seg->region == GNW_CORE_REGION_ITCM)
                ? itc_malloc(seg->code_size + seg->bss_size)
                : ahb_malloc(seg->code_size + seg->bss_size);
            if (reserved != base) {
                show_corrupted_installation_screen();
                return;
            }
        }

        file_offset += seg->code_size;
    }

    ((void (*)(uint8_t, uint8_t, int8_t))((uintptr_t)entry_base | 1))(load_state, start_paused, save_slot);
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
     * emulators[]/systems[]: those arrays are ahb_calloc()'d, and
     * ahb_calloc() tries ram_malloc() (i.e. RAM_EMU, via the global
     * `ram_start`) before falling back to real AHB SRAM — at menu boot
     * `ram_start` is set to __RAM_EMU_START__ (see app_main()), so
     * emulators[]/systems[] actually live IN RAM_EMU, not AHB. The very
     * next thing run_dynamic_core() does is load the new core's segment 0
     * on top of __RAM_EMU_START__, which would silently clobber a
     * dangling pointer into system_name/core_path with the core's own
     * code bytes. Copy the strings out before ahb_init()/ram_start=0 below
     * instead of pointing into memory about to be overwritten.
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
    strncpy(dyn_core_path_buf, newfile->system->core_path, sizeof(dyn_core_path_buf) - 1);
    dyn_core_path_buf[sizeof(dyn_core_path_buf) - 1] = '\0';
    const char *dyn_core_path = dyn_core_path_buf[0] ? dyn_core_path_buf : NULL;

    ACTIVE_FILE = newfile;
#if CHEAT_CODES == 1
    CHOSEN_FILE = newfile;

    emulator_update_cheats_info(CHOSEN_FILE);
#endif

    // Copy game data from SD card to flash if needed
    // dsk files are read from sd card, do not copy them in flash
    // With FrogFS, this maps the file directly from external flash
    if ((newfile->system->game_data_type != NO_GAME_DATA) &&
        (strcasecmp(newfile->ext, "dsk") != 0) && (strcasecmp(newfile->ext, "cdk") != 0)) {
        newfile->address = odroid_overlay_cache_file_in_flash(newfile->path, &(newfile->size), newfile->system->game_data_type == GAME_DATA_BYTESWAP_16);
        ROM_DATA = newfile->address;
        ROM_EXT = newfile->ext;
        ROM_DATA_LENGTH = newfile->size;

        if (newfile->address == NULL) {
            // Rom was not loaded in flash, do not start emulator
            return;
        }
    }

    /* systems[] lives in AHB and is wiped by ahb_init(). In-game code must
     * not touch ACTIVE_FILE->system (use handlers / path instead). */
    newfile->system = NULL;

    // It will free all ram allocated memory for use by emulators
    ahb_init();
    itc_init();
    ram_start = 0;
    emulators = NULL;
    systems = NULL;
    // some pointers were freed, set them to null
    rg_reset_logo_buffers();

    // Refresh watchdog here in case previous actions did not refresh it
    wdog_refresh();

    if (dyn_core_path) {
      run_dynamic_core(dyn_core_path, load_state, start_paused, save_slot);
    } else if(strcmp(system_name, "Homebrew") == 0)  {
      /* Bounded: refuses (returns 0) rather than overrunning RAM_EMU if the
       * file is bigger than the region. This used to be an unchecked
       * odroid_overlay_cache_file_in_ram() call; every branch below
       * shares that fix now. */
      const uint32_t ram_emu_len = ((uint32_t)&__RAM_EMU_END__) - (uint32_t)&__RAM_EMU_START__;
      size_t homebrew_bytes = rg_storage_copy_file_to_ram_bounded(
          ACTIVE_FILE->path, (uint8_t *)&__RAM_EMU_START__, 0, ram_emu_len, NULL);
      if (homebrew_bytes) {
          run_gwhb_homebrew(homebrew_bytes, load_state, start_paused, save_slot);
      } else {
        show_incompatible_homebrew_screen();
      }
    } else if(strcmp(system_name, "PICO-8") == 0) {
      /* PICO-8 engine loads at a FIXED address inside the LCD bonus area
       * (__overlay_pico8_vma = __RAM_UC_START__ + LUT8 framebuffer size).
       * GPL does NOT zero the engine's BSS — the engine trampoline at
       * overlay offset 0 zeroes its own BSS at startup (using its own
       * link-time _OVERLAY_PICO8_BSS_START / _END symbols). TLSF main
       * pool spans engine_BSS_END..__RAM_EMU_END__ — sized by the SD
       * linker, communicated to the engine via its own BSS_END symbol.
       *
       * Two-stage load to avoid an LTDC race with the framebuffer:
       *   1. Read pico8.bin from SD into a temp buffer at __RAM_EMU_START__
       *      (safely outside the LCD pool). SD reads can be slow (~tens of
       *      ms) and are sensitive to debug-induced halts (gnwmanager
       *      monitor); doing them here means LTDC never sees in-flight
       *      writes to the framebuffer region.
       *   2. Switch the LCD to LUT8 mode. lcd_setup_framebuffers zeros the
       *      300 KB framebuffer footprint and schedules an LTDC reload at
       *      the next vertical blanking; afterwards the bonus area at
       *      __overlay_pico8_vma becomes Normal cacheable (MPU reconfig).
       *   3. memcpy from temp into __overlay_pico8_vma. This is a fast
       *      cached write (~µs) and happens AFTER the LCD switch, so the
       *      LTDC is already in LUT8 mode (or about to be) and never
       *      reads our in-flight writes. */
      extern uint8_t __overlay_pico8_vma[];
      uint8_t *pico8_load_addr = (uint8_t *)__overlay_pico8_vma;
      uint8_t *pico8_temp_addr = (uint8_t *)&__RAM_EMU_START__;

      ram_start = (uint32_t)&__RAM_EMU_START__;
      uint32_t pico8_code_size = 0;
      uint8_t *pico8_code_addr = Pico8CacheCodeToFlash(&pico8_code_size);
      ahb_init();  /* reset current_ram_pointer before overlay load */
      ram_start = 0;

      size_t pico8_bin_size = 0;
      if (pico8_code_addr &&
          (pico8_bin_size = load_core_bin_with_header("/cores/pico8.bin", pico8_temp_addr))) {
        /* Sentinel scan covers ONLY loaded code+data, NOT BSS.
         * BSS is zeroed by the engine's own trampoline so no sentinel
         * matches would be possible there anyway, and scanning loaded
         * DATA risks false positives: any fix32 constant in the
         * -16657..-16565 range (0xBEEFxxxx) would be incorrectly
         * "patched" and corrupted. Patch in the temp buffer before the
         * memcpy so the final location lands ready-to-run. */
        int patched = PatchPico8Region((uint32_t *)pico8_temp_addr,
                         (uint32_t *)(pico8_temp_addr + pico8_bin_size),
                         (int32_t)((uint32_t)pico8_code_addr - PICO8_CODE_BASE),
                         pico8_code_size);
        printf("P8: patched %d refs in temp buffer %p (loaded %u bytes)\n",
               patched, pico8_temp_addr, (unsigned)pico8_bin_size);
        /* Expose for ITCM sentinel patching in main_pico8.c (after SD load) */
        pico8_code_flash_addr = pico8_code_addr;
        pico8_code_flash_size = pico8_code_size;

        /* Now safe to switch LCD: SD I/O is done, the next memcpy is fast
         * and goes only to memory the LTDC will not read in LUT8 mode. */
        lcd_setup_framebuffers(LCD_MODE_LUT8);

        memcpy(pico8_load_addr, pico8_temp_addr, pico8_bin_size);

        /* Flush just the loaded code+data so the engine sees our writes;
         * BSS will be zeroed via cached stores by the trampoline. */
        SCB_CleanDCache_by_Addr((uint32_t *)pico8_load_addr, pico8_bin_size);
        SCB_InvalidateICache();
        /* Dispatch via entry trampoline at overlay offset 0 — it zeroes
         * its own BSS then jumps to app_main_pico8. */
        ((void (*)(uint8_t, uint8_t, int8_t))((uintptr_t)pico8_load_addr | 1))(load_state, start_paused, save_slot);
      }
      /* No engine on the SD card: load_core_bin_with_header() already
       * showed an error screen (missing-file case) or the ABI/size check
       * failed silently (see its own checks) — there used to be a
       * firmware-compiled GPL install-prompt fallback here
       * (/cores/pico8_stub.bin, main_pico8_stub.c); it has been removed
       * from the build, so a missing pico8.bin now simply does not
       * launch anything. */
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

    ahb_init();
    itc_init();
    ram_start = 0;
#if SD_CARD == 1
    // some pointers were freed, set them to null
    rg_reset_logo_buffers();
#endif
}

void emulators_init()
{
    if (!emulators) {
        emulators = (retro_emulator_t *)ahb_calloc(MAX_EMULATORS, sizeof(retro_emulator_t));
        systems = (rom_system_t *)ahb_calloc(MAX_EMULATORS, sizeof(rom_system_t));
    }

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
    add_emulator("Homebrew", "homebrew", "bin", RG_LOGO_EMPTY, RG_LOGO_HEADER_HOMEBREW, NO_GAME_DATA);
    /* PICO-8: carts (.p8 / .p8.png) live under /roms/pico8/. The engine
     * itself (pico8.bin) is a separately-distributed overlay loaded at
     * runtime; see the stub in Core/Src/porting/pico8/main_pico8.c. */
    add_emulator("PICO-8", "pico8", "p8 png", RG_LOGO_EMPTY, RG_LOGO_HEADER_PICO8, GAME_DATA);

#if SD_CARD == 1
    /* Migrated systems (Watara Supervision, ...) register themselves here
     * by dropping a packaged .bin under /cores/ on the SD card — no
     * firmware rebuild needed to add/update/remove one. */
    emulators_scan_cores();
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
