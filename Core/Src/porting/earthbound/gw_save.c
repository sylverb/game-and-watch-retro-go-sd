/*
 * G&W save platform — fopen/fread/fwrite against the launcher's SD card.
 *
 * EarthBound's save format is a flat SAVE_FILE_SIZE (7680) byte buffer:
 * 3 slots x 2 copies x 1280 bytes. We store it at the path returned by
 * odroid_system_get_path(ODROID_PATH_SAVE_SRAM, ...), which puts it next
 * to the running ROM's directory (e.g. /saves/homebrew/EarthBound.srm).
 *
 * Read/write offsets are byte offsets into that flat buffer, so we just
 * fseek + fread/fwrite. We open the file lazily on first access and
 * cache the handle for the session.
 */

#include <stdio.h>
#include <string.h>

#include "platform/platform.h"
#include "odroid_system.h"
#include "rom_manager.h"

static char save_path[256];
static bool path_resolved = false;

static const char *resolve_save_path(void)
{
    if (!path_resolved) {
        char *p = odroid_system_get_path(ODROID_PATH_SAVE_SRAM, ACTIVE_FILE->path);
        if (p) {
            strncpy(save_path, p, sizeof(save_path) - 1);
            save_path[sizeof(save_path) - 1] = '\0';
            free(p);
            path_resolved = true;
        }
    }
    return path_resolved ? save_path : NULL;
}

bool platform_save_init(void)
{
    return resolve_save_path() != NULL;
}

size_t platform_save_read(void *dst, size_t offset, size_t size)
{
    const char *path = resolve_save_path();
    if (!path) return 0;
    FILE *f = fopen(path, "rb");
    if (!f) return 0;
    if (fseek(f, (long)offset, SEEK_SET) != 0) {
        fclose(f);
        return 0;
    }
    size_t n = fread(dst, 1, size, f);
    fclose(f);
    return n;
}

bool platform_save_write(const void *src, size_t offset, size_t size)
{
    const char *path = resolve_save_path();
    if (!path) return false;
    /* "r+b" preserves bytes outside [offset, offset+size); fall back to
     * "wb" if the file doesn't exist yet. */
    FILE *f = fopen(path, "r+b");
    if (!f) f = fopen(path, "wb");
    if (!f) return false;
    if (fseek(f, (long)offset, SEEK_SET) != 0) {
        fclose(f);
        return false;
    }
    size_t n = fwrite(src, 1, size, f);
    fclose(f);
    return n == size;
}
