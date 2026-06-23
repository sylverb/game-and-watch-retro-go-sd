/* LittleFS backing store on external flash (__FILESYSTEM_*); FrogFS holds ROMs/covers. */
#include "gw_flash.h"
#include "gw_linker.h"
#include "gw_lcd.h"
#include <string.h>
#include <time.h>
#include "gw_littlefs.h"
#if SD_CARD == 0
#include "gw_layout_superblock.h"
#endif
#include "gw_sleep.h"
#include "porting/lib/littlefs/lfs.h"
#include "tamp/compressor.h"
#include "tamp/decompressor.h"
#include "stm32h7xx.h"

#include "main.h"
#include "bitmaps.h"
#include "odroid_colors.h"
#include "odroid_overlay.h"
#include "odroid_input.h"
#include "gittag.h"
#include "config.h"
#include "gui.h"
#include "error_screens.h"

#define LFS_CACHE_SIZE 256
#define LFS_LOOKAHEAD_SIZE 16
#define LFS_NUM_ATTRS 1  // Number of atttached file attributes; currently just 1 for "time".

#ifndef LFS_NO_MALLOC
    #error "GW does not support malloc"
#endif


#define MAX_OPEN_FILES 1  // Cannot be >8
#if MAX_OPEN_FILES > 8 || MAX_OPEN_FILES < 1
    #error "MAX_OPEN_FILES must be in range [1, 8]"
#endif
typedef struct{
    lfs_file_t file;  // This MUST be the first attribute of this struct.
    uint8_t buffer[LFS_CACHE_SIZE];
    struct lfs_attr file_attrs[LFS_NUM_ATTRS];
    struct lfs_file_config config;
    uint32_t open_time;
} fs_file_handle_t;

static fs_file_handle_t file_handles[MAX_OPEN_FILES];
static uint8_t file_handles_used_bitmask = 0;
static int8_t file_index_using_compression = -1;  //negative value indicates that compressor/decompressor is available.

/********************************************
 * Tamp Compressor/Decompressor definitions *
 ********************************************/

#define TAMP_WINDOW_BUFFER_BITS 10  // 1KB
// TODO: if we want to save RAM, we could reuse the inactive lcd frame buffer.
static unsigned char tamp_window_buffer[1 << TAMP_WINDOW_BUFFER_BITS];
typedef union{
    TampDecompressor d;
    TampCompressor c;
} tamp_compressor_or_decompressor_t;
static tamp_compressor_or_decompressor_t tamp_engine;
static bool tamp_is_compressing;


/******************************
 * LittleFS Driver Definition *
 ******************************/
static inline bool is_dcache_enabled() {
    return (SCB->CCR & SCB_CCR_DC_Msk) != 0;
}

lfs_t lfs = {0};
lfs_dir_t dir[2]; // Allow to open 2 folders in parallel

bool fs_mounted = false;

static uint8_t read_buffer[LFS_CACHE_SIZE] = {0};
static uint8_t prog_buffer[LFS_CACHE_SIZE] = {0};
static uint8_t lookahead_buffer[LFS_LOOKAHEAD_SIZE] __attribute__((aligned(4))) = {0};


static int littlefs_api_read(const struct lfs_config *c, lfs_block_t block,
        lfs_off_t off, void *buffer, lfs_size_t size) {
    unsigned char *address = (unsigned char *)c->context - ((block+1) * c->block_size) + off;
    memcpy(buffer, address, size);
    return 0;
}

static int littlefs_api_prog(const struct lfs_config *c, lfs_block_t block,
        lfs_off_t off, const void *buffer, lfs_size_t size) {
    bool toggle_dcache = is_dcache_enabled();
    uint32_t address = (uint32_t)c->context - ((block+1) * c->block_size) + off;
    
    if(toggle_dcache){
        SCB_CleanDCache_by_Addr((uint32_t *)address, size);
        SCB_InvalidateDCache_by_Addr((uint32_t *)address, size);
        SCB_DisableDCache();
    }

    OSPI_DisableMemoryMappedMode();
    OSPI_Program(address - (uint32_t)&__EXTFLASH_BASE__, buffer, size);
    OSPI_EnableMemoryMappedMode();

    if(toggle_dcache){
        SCB_InvalidateDCache_by_Addr((uint32_t *)address, size);
        SCB_EnableDCache();
    }

    return 0;
}

static int littlefs_api_erase(const struct lfs_config *c, lfs_block_t block) {
    bool toggle_dcache = is_dcache_enabled();
    uint32_t address = (uint32_t)c->context - ((block+1) * c->block_size);
    assert((address & (4*1024 - 1)) == 0);

    if(toggle_dcache){
        SCB_CleanDCache_by_Addr((uint32_t *)address, c->block_size);
        SCB_InvalidateDCache_by_Addr((uint32_t *)address, c->block_size);
        SCB_DisableDCache();
    }

    OSPI_DisableMemoryMappedMode();
    OSPI_EraseSync(address - (uint32_t)&__EXTFLASH_BASE__, c->block_size);
    OSPI_EnableMemoryMappedMode();

    if(toggle_dcache){
        SCB_InvalidateDCache_by_Addr((uint32_t *)address, c->block_size);
        SCB_EnableDCache();
    }

    return 0;
}

static int littlefs_api_sync(const struct lfs_config *c) {
    return 0;
}

__attribute__((section (".littlefs_cfg"))) static struct lfs_config lfs_cfg = {
    // block device operations
    .read  = littlefs_api_read,
    .prog  = littlefs_api_prog,
    .erase = littlefs_api_erase,
    .sync  = littlefs_api_sync,

    // statically allocated buffers
    .read_buffer = read_buffer,
    .prog_buffer = prog_buffer,
    .lookahead_buffer = lookahead_buffer,

    // block device configuration
    .cache_size = LFS_CACHE_SIZE,
    .read_size = LFS_CACHE_SIZE,
    .prog_size = LFS_CACHE_SIZE,
    .lookahead_size = LFS_LOOKAHEAD_SIZE,
    //.block_size will be set later
    //.block_count will be set later
    .block_cycles = 500,
};

/**
 * @brief Recursively make all parent directories for a file.
 * @param[in] dir Path of directories to make up to. The last element
 * of the path is assumed to be the file and IS NOT created.
 *   e.g.
 *       "foo/bar/baz"
 *   will create directories "foo" and "bar"
 */
static void mkdirs(const char *path) {
    char partial_path[FS_MAX_PATH_SIZE];
    char *p = NULL;

    strlcpy(partial_path, path, sizeof(partial_path));
    for(p = partial_path + 1; *p; p++) {
        if(*p == '/') {
            *p = '\0';
            lfs_mkdir(&lfs, partial_path);
            *p = '/';
        }
    }
}

static bool file_is_using_compression(fs_file_t *file){
    for(uint8_t i=0; i < MAX_OPEN_FILES; i++){
        if(file == &(file_handles[i].file) && i == file_index_using_compression)
            return true;
    }
    return false;
}

/**
 * Get a file handle from the statically allocated file handles.
 * Not responsible for initializing the file handle.
 *
 * If we want to use dynamic allocation in the future, malloc inside this function.
 */
static fs_file_handle_t *acquire_file_handle(bool use_compression){
    uint8_t test_bit = 0x01;

    for(uint8_t i=0; i < MAX_OPEN_FILES; i++){
        if(!(file_handles_used_bitmask & test_bit)){
            fs_file_handle_t *handle;
            // Set the bit, indicating this file_handle is in use.
            file_handles_used_bitmask |= test_bit;

            if(use_compression){
                // Check if the compressor/decompressor is available.
                if(file_index_using_compression >= 0)
                    return NULL;
                // Indicate that this file is using the compressor/decompressor.
                file_index_using_compression = i;
            }
            handle = &file_handles[i];
            memset(handle, 0, sizeof(fs_file_handle_t));
            return handle;
        }
        test_bit <<= 1;
    }

    return NULL;
}

/**
 * Release the file handle.
 * Not responsible for closing the file handle.
 *
 * If we want to use dynamic allocation in the future, free inside this function.
 */
static void release_file_handle(fs_file_t *file){
    uint8_t test_bit = 0x01;

    for(uint8_t i=0; i < MAX_OPEN_FILES; i++){
        if(file == &(file_handles[i].file)){
            // Clear the bit, indicating this file_handle is no longer in use.
            file_handles_used_bitmask &= ~test_bit;
            if(file_is_using_compression(file)){
                file_index_using_compression = -1;
            }
            return;
        }
    }
    assert(0);  // Should never reach here.
}

/* gnw-web-builder DIAGNOSTIC: mount-failure detail, rendered on the corrupt screen. */
static char g_fs_diag1[48] = "";
static char g_fs_diag2[48] = "";

static void corrupt_filesystem_screen(void){
    char buf[64];
    int idle_s = uptime_get();

    draw_error_screen("FILESYSTEM ERROR", "LittleFS is corrupted", "Reinstall the system");

    while (1)
    {
        odroid_gamepad_state_t joystick;
        wdog_refresh();
        int steps = uptime_get() - idle_s;
        sprintf(buf, "%ds to sleep", 600 - steps);
        odroid_overlay_draw_text_line(4, 29 * 8 - 4, strlen(buf) * 8, buf, C_RED, curr_colors->bg_c);
        if (g_fs_diag1[0])
            odroid_overlay_draw_text_line(4, 26 * 8 - 4, strlen(g_fs_diag1) * 8, g_fs_diag1, C_RED, curr_colors->bg_c);
        if (g_fs_diag2[0])
            odroid_overlay_draw_text_line(4, 27 * 8 - 4, strlen(g_fs_diag2) * 8, g_fs_diag2, C_RED, curr_colors->bg_c);

        lcd_sync();
        lcd_swap();
        HAL_Delay(10);
        if (steps >= 600)
            break;
        odroid_input_read_gamepad(&joystick);
        if (joystick.values[ODROID_INPUT_POWER]){
            break;
        }
    }
    app_sleep_logo();
    GW_EnterDeepSleep(true, NULL, NULL);
}

/*************************
 * Filesystem Public API (LittleFS on ext. flash __FILESYSTEM_START__..__FILESYSTEM_END__) *
 *************************/

/**
 * Initialize and mount the filesystem. Format the filesystem if unmountable (and then reattempt mount).
 */
void fs_init(void){
#if SD_CARD == 0
    // FrogFS (flash) build: the LittleFS partition geometry comes from the layout
    // superblock (host-determined from the probe-detected chip size + the FrogFS
    // image), falling back to the linker symbols when unpatched. This is what lets
    // one binary serve any extflash size. See gw_layout_superblock.h.
    lfs_cfg.context = (void *)gw_layout_littlefs_top();
#else
    lfs_cfg.context = &__FILESYSTEM_END__;  // We work "backwards"
#endif
    lfs_cfg.block_size = OSPI_GetSmallestEraseSize();
    lfs_cfg.block_count = 0;

    // reformat if we can't mount the fs
    // this should only happen on the first boot
    int mount_err = lfs_mount(&lfs, &lfs_cfg);
    if (mount_err) {
#if SD_CARD == 0
        /* gnw-web-builder DIAGNOSTIC: show the mount code + the geometry actually used.
         * err -22 = LFS_ERR_INVAL (config/block_size); -84 = LFS_ERR_CORRUPT (location/data).
         * valid=1 means the layout superblock took effect (else baked linker fallback). */
        snprintf(g_fs_diag1, sizeof(g_fs_diag1), "err=%d valid=%d bs=%lu",
                 mount_err, (int)gw_layout_valid(), (unsigned long)lfs_cfg.block_size);
        snprintf(g_fs_diag2, sizeof(g_fs_diag2), "top=%08lX sz=%lu",
                 (unsigned long)gw_layout_littlefs_top(),
                 (unsigned long)gw_layout_littlefs_size());
#endif
        corrupt_filesystem_screen();
#if SD_CARD == 0
        lfs_cfg.block_count = gw_layout_littlefs_size() / lfs_cfg.block_size;
#else
        lfs_cfg.block_count = (&__FILESYSTEM_END__ - &__FILESYSTEM_START__) / lfs_cfg.block_size;
#endif
        // If we get here, it means that the user chose to reformat the filesystem.
        assert(lfs_mount(&lfs, &lfs_cfg) == 0);
    }
    fs_mounted = true;
    printf("filesytem mounted.\n");
    printf("%ld/%ld blocks allocated (block_size=%ld)\n", lfs_fs_size(&lfs), lfs.block_count, lfs_cfg.block_size);
}

/**
 * Only 1 tamp-compressed file can be open at a time.
 *
 * In write mode, directories are created as necessary.
 *
 * If:
 *   * write_mode==true: Opens the file for writing; creates file if it doesn't exist.
 *   * write_mode==false: Opens the file for reading; erroring (returning NULL) if it doesn't exist.
 */
fs_file_t *fs_open(const char *path, bool write_mode, bool use_compression){
    int res;
    int flags = write_mode ? LFS_O_WRONLY | LFS_O_CREAT | LFS_O_TRUNC: LFS_O_RDONLY;
    
    fs_file_handle_t *fs_file_handle = acquire_file_handle(use_compression);

    if(!fs_file_handle){
        printf("Unable to allocate file handle.");
        return NULL;
    }

    tamp_is_compressing = write_mode;

    if(use_compression){
        // TODO: initialize tamp; it's globally already been reserved.
        if(write_mode){
            TampConf conf = {.window=TAMP_WINDOW_BUFFER_BITS, .literal=8, .use_custom_dictionary=false};
            assert(TAMP_OK == tamp_compressor_init(&tamp_engine.c, &conf, tamp_window_buffer));
        }
        else{
            assert(TAMP_OK == tamp_decompressor_init(&tamp_engine.d, NULL, tamp_window_buffer, TAMP_WINDOW_BUFFER_BITS));
        }
    }

    if(write_mode)
        mkdirs(path);

    fs_file_handle->config.buffer = fs_file_handle->buffer;
    fs_file_handle->config.attrs = fs_file_handle->file_attrs;
    fs_file_handle->config.attr_count = LFS_NUM_ATTRS;

    // Add time attribute; may be useful for deleting oldest savestates to make room for new ones.
    fs_file_handle->open_time = time(NULL);
    fs_file_handle->file_attrs[0].type = 't';  // 't' for "time"
    fs_file_handle->file_attrs[0].buffer = &fs_file_handle->open_time;
    fs_file_handle->file_attrs[0].size = sizeof(fs_file_handle->open_time);

    // TODO: add error handling; maybe delete oldest file(s) to make room
    res = lfs_file_opencfg(&lfs, &fs_file_handle->file, path, flags, &fs_file_handle->config);
    if(res != 0)
        goto error;

    return &fs_file_handle->file;

error:
    release_file_handle(&fs_file_handle->file);
    return NULL;
}

fs_file_t *fs_open_flags(const char *path, int flags){
    int res;

    fs_file_handle_t *fs_file_handle = acquire_file_handle(FS_RAW);

    if(!fs_file_handle){
        printf("Unable to allocate file handle.");
        return NULL;
    }

    if(flags & (LFS_O_WRONLY | LFS_O_RDWR | LFS_O_CREAT))
        mkdirs(path);

    fs_file_handle->config.buffer = fs_file_handle->buffer;
    fs_file_handle->config.attrs = fs_file_handle->file_attrs;
    fs_file_handle->config.attr_count = LFS_NUM_ATTRS;

    fs_file_handle->open_time = time(NULL);
    fs_file_handle->file_attrs[0].type = 't';
    fs_file_handle->file_attrs[0].buffer = &fs_file_handle->open_time;
    fs_file_handle->file_attrs[0].size = sizeof(fs_file_handle->open_time);

    res = lfs_file_opencfg(&lfs, &fs_file_handle->file, path, flags, &fs_file_handle->config);
    if(res != 0)
        goto error;

    return &fs_file_handle->file;

error:
    release_file_handle(&fs_file_handle->file);
    return NULL;
}

int fs_delete(const char *path){
    return lfs_remove(&lfs,path);
}

int fs_mkdir(const char *path){
    return lfs_mkdir(&lfs, path);
}

int fs_write(fs_file_t *file, unsigned char *data, size_t size){
    // TODO: do we want to put delete-oldest-savestate-logic in here?
    if(!file_is_using_compression(file))
        return lfs_file_write(&lfs, file, data, size);
    int output_written_size = 0;
    unsigned char output_buffer[4];
    while(size){
        size_t consumed;

        // Sink Data into input buffer.
        tamp_compressor_sink(&tamp_engine.c, data, size, &consumed);
        data += consumed;
        output_written_size += consumed;
        size -= consumed;

        // Run the engine; remaining size indicates that the compressor input buffer is full
        if(TAMP_LIKELY(size)){
            size_t chunk_output_written_size;
            assert(TAMP_OK == tamp_compressor_compress_poll(
                    &tamp_engine.c,
                    output_buffer,
                    sizeof(output_buffer),
                    &chunk_output_written_size
                    ));
            // TODO: better error-handling if the disk is full.
            assert(chunk_output_written_size == lfs_file_write(&lfs, file, output_buffer, chunk_output_written_size));
        }
    }
    // Note: we really return the number of consumed bytes, not the number of
    // compressed-bytes.
    return output_written_size;
}

int fs_read(fs_file_t *file, unsigned char *buffer, size_t size){
    if(!file_is_using_compression(file))
        return lfs_file_read(&lfs, file, buffer, size);

    tamp_res res;
    int output_written_size = 0;
    size_t input_chunk_consumed_size = 0;
    size_t output_chunk_written_size;
    int read_size = 0;
    unsigned char input_byte;
    bool file_exhausted = false;

    while(true){
        res = tamp_decompressor_decompress(
                &tamp_engine.d,
                buffer,
                size,
                &output_chunk_written_size,
                &input_byte,
                read_size,
                &input_chunk_consumed_size
                );
        assert(res >= TAMP_OK);  // i.e. an "ok" result
        assert(input_chunk_consumed_size == read_size);
                                 
        output_written_size += output_chunk_written_size;
        buffer += output_chunk_written_size;
        size -= output_chunk_written_size;

        if(res == TAMP_OUTPUT_FULL)
            break;
        if(res == TAMP_INPUT_EXHAUSTED && file_exhausted)
            break;

        read_size = lfs_file_read(&lfs, file, &input_byte, 1);
        file_exhausted = read_size == 0;
    }
    return output_written_size;
}

void fs_close(lfs_file_t *file){
    if(file_is_using_compression(file) && tamp_is_compressing){
        // flush the compressor
        unsigned char output_buffer[20];
        size_t output_written_size;
        tamp_res res;
        res = tamp_compressor_flush(
                &tamp_engine.c,
                output_buffer,
                sizeof(output_buffer),
                &output_written_size,
                false
        );
        assert(res == TAMP_OK);
        assert(output_written_size == lfs_file_write(&lfs, file, output_buffer, output_written_size));
    }
    assert(lfs_file_close(&lfs, file) >= 0);
    release_file_handle(file);
}

int fs_seek(lfs_file_t *file, lfs_soff_t off, int whence){
    if(file_is_using_compression(file)){
        assert(0);  // This code block is unused/untested.
        if(whence == LFS_SEEK_CUR){
            unsigned char byte_buffer;
            assert(off > 0);
            for(int i=0; i < off; i++){
                fs_read(file, &byte_buffer, 1);
            }
            return 0;  // WARNING: this is not a compliant value!
        }
        else{
            assert(0); // not implemented
        }
    }
    return lfs_file_seek(&lfs, file, off, whence);
}

int fs_sync(lfs_file_t *file){
    return lfs_file_sync(&lfs, file);
}

int fs_stat(const char *path, struct lfs_info *info){
    return lfs_stat(&lfs, path, info);
}

int fs_get_file_time(const char *path, uint32_t *timestamp){
    return lfs_getattr(&lfs, path, 't', timestamp, sizeof(*timestamp));
}

bool fs_exists(const char *path){
    struct lfs_info info;
    return lfs_stat(&lfs, path, &info) == LFS_ERR_OK;
}


uint32_t fs_free_blocks() {
    return (uint32_t)lfs_cfg.block_count - (uint32_t)lfs_fs_size(&lfs);
}

static void convertFileSize(size_t size, char *result) {
    const char *suffix = "B";

    if (size >= 1024) {
        size /= 1024;
        suffix = "KB";
    }

    sprintf(result, "%d %s", size, suffix);
}

int fs_dir_open(uint8_t dir_index, const char *path) {
    return lfs_dir_open(&lfs, &dir[dir_index], path);
}

int fs_dir_read(uint8_t dir_index, fs_folder_entry *entry) {
    struct lfs_info info;
    int ret;
    ret = lfs_dir_read(&lfs, &dir[dir_index], &info);
    if (ret) {
        strcpy(entry->name, info.name);
        convertFileSize(info.size,entry->size);
        entry->is_folder = info.type == LFS_TYPE_DIR;
    }
    return ret;
}

int fs_dir_close(uint8_t dir_index) {
    return lfs_dir_close(&lfs, &dir[dir_index]);
}
