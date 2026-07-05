#include <odroid_system.h>
#include <string.h>
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>

#include "gw_linker.h"
#include "gw_malloc.h"
#include "rg_emulators.h"
#include "rg_storage.h"
#include "rg_i18n.h"
#include "bitmaps.h"
#include "gui.h"
#include "rom_manager.h"
#include "gw_lcd.h"
#include "main.h"
#include "main_gb_tgbdual.h"
#include "main_nes_fceu.h"
#include "main_smsplusgx.h"
#include "main_pce.h"
#include "main_msx.h"
#include "main_gw.h"
#include "main_wsv.h"
#include "main_gwenesis.h"
#include "main_a7800.h"
#include "main_amstrad.h"
#include "main_zelda3.h"
#include "main_smw.h"
#include "main_earthbound.h"
#include "main_videopac.h"
#include "main_celeste.h"
#include "main_pico8.h"
#include "main_tama.h"
#include "main_pkmini.h"
#include "main_a2600.h"
#include "rg_rtc.h"
#include "gittag.h"
#include "heap.hpp"
#include "gw_flash.h"
#include "gw_flash_alloc.h"
#if SD_CARD == 0
#include "rg_frogfs.h"
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
#define MAX_EMULATORS 19
static retro_emulator_t emulators[MAX_EMULATORS];
static rom_system_t systems[MAX_EMULATORS];
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

static void add_emulator(const char *system, const char *dirname, const char* ext,
                         uint16_t logo_idx, uint16_t header_idx, game_data_type_t game_data_type)
{
    assert(emulators_count <= MAX_EMULATORS);
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

    s->extension = (char *)ext;
    s->roms = p->roms.files;
    s->roms_count = p->roms.count;
    s->system_name = (char *)system;
    s->game_data_type = game_data_type;

    gui_add_tab(dirname, logo_idx, header_idx, p, event_handler);
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

    if (emu->roms.count + 1 > emu->roms.maxcount)
        return RG_SCANDIR_STOP;

    retro_emulator_file_t *slot = &emu->roms.files[emu->roms.count];
    memset(slot, 0, sizeof(*slot));
    slot->address = 0;
    slot->size = entry->size;
    slot->system = emu->system;
    slot->region = REGION_NTSC;
    strncpy(slot->path, entry->path, sizeof(slot->path) - 1);
    slot->path[sizeof(slot->path) - 1] = '\0';

    if (entry->is_dir)
    {
        strncpy(slot->name, entry->basename, sizeof(slot->name) - 1);
        slot->name[sizeof(slot->name) - 1] = '\0';
        slot->ext = NULL;
        {
            size_t nl = strlen(slot->name);
            if (nl + 3 < sizeof(slot->name))
            {
                memmove(slot->name + 2, slot->name, nl + 1);
                slot->name[0] = '>';
                slot->name[1] = ' ';
            }
        }
#if COVERFLOW != 0
        /* Folders are not ROM-named cover files; avoid fopen on SD for each folder row. */
        slot->img_state = IMG_STATE_NO_COVER;
#endif
    }
    else
    {
        remove_extension(entry->basename, slot->name);
        slot->ext = (char *)get_extension(slot->path);
#if COVERFLOW != 0
        slot->img_state = IMG_STATE_UNKNOWN;
#endif
#if CHEAT_CODES == 1
        slot->cheat_count = 0;
        slot->cheat_codes = NULL;
        slot->cheat_descs = NULL;
#endif
    }

    emu->roms.count++;
    emu->system->roms_count = emu->roms.count;

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
    rg_storage_scandir(folder, scan_folder_cb, emu, 0);
}

void emulator_refresh_list(retro_emulator_t *emu)
{
    char folder[RG_PATH_MAX];

    sprintf(folder, ODROID_BASE_PATH_ROMS "/%s", emu->dirname);
    rg_storage_mkdir(folder);

    emulator_browse_folder_path(emu, folder, sizeof(folder));
    rg_storage_scandir(folder, scan_folder_cb, emu, 0);
}

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
#if SD_CARD != 0 // Can't delete file on FrogFS
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
                odroid_sdcard_unlink(file->path);
                strcpy(file->path, "");
            } else {
                continue;
            }
            break;
        }

        }

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
    strcpy(option->value, is_on ? curr_lang->s_Cheat_Codes_ON : curr_lang->s_Cheat_Codes_OFF);
    return event == ODROID_DIALOG_ENTER;
}

static bool show_cheat_dialog()
{
    static odroid_dialog_choice_t last = ODROID_DIALOG_CHOICE_LAST;

    // +1 for the terminator sentinel
    odroid_dialog_choice_t *choices = rg_alloc((CHOSEN_FILE->cheat_count + 1) * sizeof(odroid_dialog_choice_t), MEM_ANY);
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

    rg_free(choices);
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
//    bool is_fav = favorite_find(file) != NULL;
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
    odroid_dialog_choice_t last = ODROID_DIALOG_CHOICE_LAST;
    odroid_dialog_choice_t cheat_row = {4, curr_lang->s_Cheat_Codes, "", 1, NULL};
    odroid_dialog_choice_t cheat_choice = last; 
    if (CHOSEN_FILE->cheat_count != 0) {
        cheat_choice = cheat_row;
    }
#endif

    odroid_dialog_choice_t choices[] = {
        {0, curr_lang->s_Resume_game, "", (has_save) ? 1:-1, NULL},
        {1, curr_lang->s_New_game, "", 1, NULL},
        ODROID_DIALOG_CHOICE_SEPARATOR,
//        {3, is_fav ? "Del favorite" : "Add favorite", "", 1, NULL},
        {2, curr_lang->s_Delete_save, "", (has_save || has_sram) ? 1 : -1, NULL},
#if CHEAT_CODES == 1
        ODROID_DIALOG_CHOICE_SEPARATOR,
        cheat_choice,
#endif
        ODROID_DIALOG_CHOICE_LAST
    };

#if CHEAT_CODES == 1
    if (CHOSEN_FILE->cheat_count == 0)
        choices[4] = last;
#endif

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
/*    else if (sel == 3) {
        if (is_fav)
            favorite_remove(file);
        else
            favorite_add(file);
    }*/
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

/* Internal-emulator dispatch table. Most emulator launches follow an
 * identical pattern (load core to RAM_EMU_START, zero BSS, cache flush,
 * call entry); consolidating into a helper + table saves ~30 bytes per
 * dispatch in FLASH vs the previously inlined form.
 *
 * cpp_heap_end != 0 triggers cpp_heap_init (C++ emulators: TGB, A2600).
 * Special cases that don't fit (NES_FCEU loads to __RAM_FCEUMM_START__,
 * SMS-family multi-engine, GW with a 2-arg entry, Homebrew with
 * cache_file_in_ram, PICO-8) keep their explicit blocks below. */
typedef struct {
    const char *path;
    void       *bss_start;
    uint32_t    bss_size;
    uint32_t    code_size;
    uint32_t    cpp_heap_end;   /* 0 = no cpp_heap_init() */
    void      (*entry)(uint8_t, uint8_t, int8_t);
} emu_dispatch_t;

__attribute__((noinline))
static void run_internal_emu(const emu_dispatch_t *e,
                             uint8_t load_state, uint8_t start_paused, int8_t save_slot)
{
    if (load_core_bin_with_header(e->path, (uint8_t *)&__RAM_EMU_START__)) {
        memset(e->bss_start, 0, e->bss_size);
        SCB_CleanDCache_by_Addr((uint32_t *)&__RAM_EMU_START__, e->code_size);
        if (e->cpp_heap_end) cpp_heap_init(e->cpp_heap_end);
        e->entry(load_state, start_paused, save_slot);
    }
}

/* Entry-pointer casts: app_main_* signatures vary in return type
 * (void/int) and save_slot type (int8_t/uint8_t). All are ARM
 * calling-convention compatible (same register layout, return value
 * ignored); the cast just satisfies C type strictness. */
#define EMU_ENTRY(fn) ((void (*)(uint8_t, uint8_t, int8_t))(fn))

static const emu_dispatch_t emu_tgb     = { "/cores/tgb.bin",     &_OVERLAY_TGB_BSS_START,     (uint32_t)&_OVERLAY_TGB_BSS_SIZE,     (uint32_t)&_OVERLAY_TGB_SIZE,     (uint32_t)&_OVERLAY_TGB_BSS_END,     EMU_ENTRY(app_main_gb_tgbdual) };
static const emu_dispatch_t emu_pce     = { "/cores/pce.bin",     &_OVERLAY_PCE_BSS_START,     (uint32_t)&_OVERLAY_PCE_BSS_SIZE,     (uint32_t)&_OVERLAY_PCE_SIZE,     0, EMU_ENTRY(app_main_pce) };
static const emu_dispatch_t emu_msx     = { "/cores/msx.bin",     &_OVERLAY_MSX_BSS_START,     (uint32_t)&_OVERLAY_MSX_BSS_SIZE,     (uint32_t)&_OVERLAY_MSX_SIZE,     0, EMU_ENTRY(app_main_msx) };
static const emu_dispatch_t emu_wsv     = { "/cores/wsv.bin",     &_OVERLAY_WSV_BSS_START,     (uint32_t)&_OVERLAY_WSV_BSS_SIZE,     (uint32_t)&_OVERLAY_WSV_SIZE,     0, EMU_ENTRY(app_main_wsv) };
static const emu_dispatch_t emu_md      = { "/cores/md.bin",      &_OVERLAY_MD_BSS_START,      (uint32_t)&_OVERLAY_MD_BSS_SIZE,      (uint32_t)&_OVERLAY_MD_SIZE,      0, EMU_ENTRY(app_main_gwenesis) };
static const emu_dispatch_t emu_a2600   = { "/cores/a2600.bin",   &_OVERLAY_A2600_BSS_START,   (uint32_t)&_OVERLAY_A2600_BSS_SIZE,   (uint32_t)&_OVERLAY_A2600_SIZE,   (uint32_t)&_OVERLAY_A2600_BSS_END, EMU_ENTRY(app_main_a2600) };
static const emu_dispatch_t emu_a7800   = { "/cores/a7800.bin",   &_OVERLAY_A7800_BSS_START,   (uint32_t)&_OVERLAY_A7800_BSS_SIZE,   (uint32_t)&_OVERLAY_A7800_SIZE,   0, EMU_ENTRY(app_main_a7800) };
static const emu_dispatch_t emu_amstrad = { "/cores/amstrad.bin", &_OVERLAY_AMSTRAD_BSS_START, (uint32_t)&_OVERLAY_AMSTRAD_BSS_SIZE, (uint32_t)&_OVERLAY_AMSTRAD_SIZE, 0, EMU_ENTRY(app_main_amstrad) };
static const emu_dispatch_t emu_tama    = { "/cores/tama.bin",    &_OVERLAY_TAMA_BSS_START,    (uint32_t)&_OVERLAY_TAMA_BSS_SIZE,    (uint32_t)&_OVERLAY_TAMA_SIZE,    0, EMU_ENTRY(app_main_tama) };
static const emu_dispatch_t emu_pkmini  = { "/cores/pkmini.bin",  &_OVERLAY_PKMINI_BSS_START,  (uint32_t)&_OVERLAY_PKMINI_BSS_SIZE,  (uint32_t)&_OVERLAY_PKMINI_SIZE,  0, EMU_ENTRY(app_main_pkmini) };

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

    const char *system_name = newfile->system->system_name;

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

    // It will free all ram allocated memory for use by emulators
    ahb_init();
    itc_init();
    ram_start = 0;
    // some pointers were freed, set them to null
    rg_reset_logo_buffers();

    // Refresh watchdog here in case previous actions did not refresh it
    wdog_refresh();

    if((strcmp(system_name, "Nintendo Gameboy") == 0) ||
       (strcmp(system_name, "Nintendo Gameboy Color") == 0)) {
        run_internal_emu(&emu_tgb, load_state, start_paused, save_slot);
    } else if(strcmp(system_name, "Nintendo Entertainment System") == 0) {
        /* NES_FCEU is special: loads to __RAM_FCEUMM_START__ not RAM_EMU_START. */
        if (load_core_bin_with_header("/cores/nes_fceu.bin", (uint8_t *)&__RAM_FCEUMM_START__)) {
            memset(&_OVERLAY_NES_FCEU_BSS_START, 0x0, (size_t)&_OVERLAY_NES_FCEU_BSS_SIZE);
            SCB_CleanDCache_by_Addr((uint32_t *)&__RAM_EMU_START__, (size_t)&_OVERLAY_NES_FCEU_SIZE);
            app_main_nes_fceu(load_state, start_paused, save_slot);
        }
    } else if(strcmp(system_name, "Sega Master System") == 0 ||
              strcmp(system_name, "Sega Game Gear") == 0     ||
              strcmp(system_name, "Sega SG-1000") == 0       ||
              strcmp(system_name, "Colecovision") == 0 ) {
        if (load_core_bin_with_header("/cores/sms.bin", (uint8_t *)&__RAM_EMU_START__)) {
            memset(&_OVERLAY_SMS_BSS_START, 0x0, (size_t)&_OVERLAY_SMS_BSS_SIZE);
            SCB_CleanDCache_by_Addr((uint32_t *)&__RAM_EMU_START__, (size_t)&_OVERLAY_SMS_SIZE);
            if (! strcmp(system_name, "Colecovision")) app_main_smsplusgx(load_state, start_paused, save_slot, SMSPLUSGX_ENGINE_COLECO);
            else
            if (! strcmp(system_name, "Sega SG-1000")) app_main_smsplusgx(load_state, start_paused, save_slot, SMSPLUSGX_ENGINE_SG1000);
            else                                            app_main_smsplusgx(load_state, start_paused, save_slot, SMSPLUSGX_ENGINE_OTHERS);
        }
    } else if(strcmp(system_name, "Game & Watch") == 0 ) {
        if (load_core_bin_with_header("/cores/gw.bin", (uint8_t *)&__RAM_EMU_START__)) {
            memset(&_OVERLAY_GW_BSS_START, 0x0, (size_t)&_OVERLAY_GW_BSS_SIZE);
            SCB_CleanDCache_by_Addr((uint32_t *)&__RAM_EMU_START__, (size_t)&_OVERLAY_GW_SIZE);
            app_main_gw(load_state, save_slot);
        }
    } else if(strcmp(system_name, "PC Engine") == 0) {
        run_internal_emu(&emu_pce, load_state, start_paused, save_slot);
    } else if(strcmp(system_name, "MSX") == 0) {
        run_internal_emu(&emu_msx, load_state, start_paused, save_slot);
    } else if(strcmp(system_name, "Watara Supervision") == 0) {
        run_internal_emu(&emu_wsv, load_state, start_paused, save_slot);
    } else if(strcmp(system_name, "Sega Genesis") == 0)  {
        run_internal_emu(&emu_md, load_state, start_paused, save_slot);
    } else if(strcmp(system_name, "Atari 2600") == 0) {
        run_internal_emu(&emu_a2600, load_state, start_paused, save_slot);
    } else if(strcmp(system_name, "Atari 7800") == 0)  {
        run_internal_emu(&emu_a7800, load_state, start_paused, save_slot);
    } else if(strcmp(system_name, "Amstrad CPC") == 0)  {
        run_internal_emu(&emu_amstrad, load_state, start_paused, save_slot);
#if 0
    } else if(strcmp(system_name, "Philips Vectrex") == 0)  {
#ifdef ENABLE_EMULATOR_VIDEOPAC
      if (load_core_bin_with_header("/cores/videopac.bin", (uint8_t *)&__RAM_EMU_START__)) {
        memset(&_OVERLAY_VIDEOPAC_BSS_START, 0x0, (size_t)&_OVERLAY_VIDEOPAC_BSS_SIZE);
        SCB_CleanDCache_by_Addr((uint32_t *)&__RAM_EMU_START__, (size_t)&_OVERLAY_VIDEOPAC_SIZE);
        app_main_videopac(load_state, start_paused, save_slot);
      }
#endif
#endif
    } else if(strcmp(system_name, "Homebrew") == 0)  {
      if (odroid_overlay_cache_file_in_ram(ACTIVE_FILE->path, (uint8_t *)&__RAM_EMU_START__)) {
        if (strcmp(newfile->name,"celeste") == 0) {
            memset(&_OVERLAY_CELESTE_BSS_START, 0x0, (size_t)&_OVERLAY_CELESTE_BSS_SIZE);
            SCB_CleanDCache_by_Addr((uint32_t *)&__RAM_EMU_START__, (size_t)&_OVERLAY_CELESTE_SIZE);
            app_main_celeste(load_state, start_paused, save_slot);
        } else if (strcmp(newfile->name,"Zelda 3") == 0) {
            memset(&_OVERLAY_ZELDA3_BSS_START, 0x0, (size_t)&_OVERLAY_ZELDA3_BSS_SIZE);
            SCB_CleanDCache_by_Addr((uint32_t *)&__RAM_EMU_START__, (size_t)&_OVERLAY_ZELDA3_SIZE);
            app_main_zelda3(load_state, start_paused, save_slot);
        } else if (strcmp(newfile->name,"Super Mario World") == 0) {
            memset(&_OVERLAY_SMW_BSS_START, 0x0, (size_t)&_OVERLAY_SMW_BSS_SIZE);
            SCB_CleanDCache_by_Addr((uint32_t *)&__RAM_EMU_START__, (size_t)&_OVERLAY_SMW_SIZE);
            app_main_smw(load_state, start_paused, save_slot);
        } else if (strcmp(newfile->name,"EarthBound") == 0) {
            memset(&_OVERLAY_EARTHBOUND_BSS_START, 0x0, (size_t)&_OVERLAY_EARTHBOUND_BSS_SIZE);
            /* entity.o + map_loader.o .bss live in AHBRAM (uncached, separate
             * region — not covered by the RAM_EMU memset above); zero them too. */
            memset(&_EARTHBOUND_AHB_BSS_START, 0x0, (size_t)&_EARTHBOUND_AHB_BSS_SIZE);
            SCB_CleanDCache_by_Addr((uint32_t *)&__RAM_EMU_START__, (size_t)&_OVERLAY_EARTHBOUND_SIZE);
            app_main_earthbound(load_state, start_paused, save_slot);
        }
      }
    } else if(strcmp(system_name, "Tamagotchi") == 0) {
        run_internal_emu(&emu_tama, load_state, start_paused, save_slot);
    } else if(strcmp(system_name, "Pokemon Mini") == 0) {
        run_internal_emu(&emu_pkmini, load_state, start_paused, save_slot);
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
      } else if ((pico8_bin_size = load_core_bin_with_header("/cores/pico8_stub.bin", pico8_temp_addr))) {
        /* Last resort: GPL stub. Same two-stage load. */
        lcd_setup_framebuffers(LCD_MODE_LUT8);
        memcpy(pico8_load_addr, pico8_temp_addr, pico8_bin_size);
        SCB_CleanDCache_by_Addr((uint32_t *)pico8_load_addr, pico8_bin_size);
        SCB_InvalidateICache();
        ((void (*)(uint8_t, uint8_t, int8_t))((uintptr_t)pico8_load_addr | 1))(load_state, start_paused, save_slot);
      }
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
    add_emulator("Nintendo Gameboy", "gb", "gb gbc lzma", RG_LOGO_PAD_GB, RG_LOGO_HEADER_GB, NO_GAME_DATA);
    add_emulator("Nintendo Gameboy Color", "gbc", "gb gbc lzma", RG_LOGO_PAD_GB, RG_LOGO_HEADER_GBC, NO_GAME_DATA);
    add_emulator("Nintendo Entertainment System", "nes", "nes fds nsf lzma", RG_LOGO_PAD_NES, RG_LOGO_HEADER_NES, NO_GAME_DATA);
    add_emulator("Game & Watch", "gw", "gw", RG_LOGO_PAD_GW, RG_LOGO_HEADER_GW, NO_GAME_DATA);
    add_emulator("PC Engine", "pce", "pce lzma", RG_LOGO_PAD_PCE, RG_LOGO_HEADER_PCE, NO_GAME_DATA);
    add_emulator("Sega Game Gear", "gg", "gg lzma", RG_LOGO_PAD_GG, RG_LOGO_HEADER_GG, NO_GAME_DATA);
    add_emulator("Sega Master System", "sms", "sms lzma", RG_LOGO_PAD_SMS, RG_LOGO_HEADER_SMS, NO_GAME_DATA);
    add_emulator("Sega Genesis", "md", "md gen bin lzma", RG_LOGO_PAD_GEN, RG_LOGO_HEADER_GEN, GAME_DATA_BYTESWAP_16);
    add_emulator("Sega SG-1000", "sg", "sg lzma", RG_LOGO_PAD_SG1000, RG_LOGO_HEADER_SG1000, NO_GAME_DATA);
    add_emulator("Colecovision", "col", "col lzma", RG_LOGO_PAD_COL, RG_LOGO_HEADER_COL, NO_GAME_DATA);
    add_emulator("Watara Supervision", "wsv", "wsv sv bin lzma", RG_LOGO_PAD_WSV, RG_LOGO_HEADER_WSV, NO_GAME_DATA);
    add_emulator("MSX", "msx", "dsk rom mx1 mx2 cdk lzma", RG_LOGO_PAD_MSX, RG_LOGO_HEADER_MSX, NO_GAME_DATA);
    add_emulator("Atari 2600", "a2600", "a26 bin lzma", RG_LOGO_PAD_A2600, RG_LOGO_HEADER_A2600, NO_GAME_DATA);
    add_emulator("Atari 7800", "a7800", "a78 bin lzma", RG_LOGO_PAD_A7800, RG_LOGO_HEADER_A7800, NO_GAME_DATA);
    add_emulator("Amstrad CPC", "amstrad", "dsk cdk", RG_LOGO_PAD_AMSTRAD, RG_LOGO_HEADER_AMSTRAD, NO_GAME_DATA);
//    add_emulator("Philips Vectrex", "videopac", "bin lzma", RG_LOGO_PAD_VIDEOPAC, RG_LOGO_HEADER_AMSTRAD, NO_GAME_DATA); // TODO : change graphics
    add_emulator("Tamagotchi", "tama", "b", RG_LOGO_PAD_TAMA, RG_LOGO_HEADER_TAMA, NO_GAME_DATA);
    add_emulator("Pokemon Mini", "mini", "min", RG_LOGO_PAD_PKMINI, RG_LOGO_HEADER_PKMINI, NO_GAME_DATA);
    add_emulator("Homebrew", "homebrew", "bin", RG_LOGO_EMPTY, RG_LOGO_HEADER_HOMEBREW, NO_GAME_DATA);
    /* PICO-8: carts (.p8 / .p8.png) live under /roms/pico8/. The engine
     * itself (pico8.bin) is a separately-distributed overlay loaded at
     * runtime; see the stub in Core/Src/porting/pico8/main_pico8.c. */
    add_emulator("PICO-8", "pico8", "p8 png", RG_LOGO_EMPTY, RG_LOGO_HEADER_PICO8, GAME_DATA);
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
