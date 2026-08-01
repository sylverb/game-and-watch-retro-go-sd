#include <odroid_system.h>

#include <assert.h>
#include "gw_lcd.h"
#include "gw_linker.h"
#include "gw_buttons.h"
#include "rom_manager.h"
#include "common.h"
#ifndef GNW_DISABLE_COMPRESSION
#include "lzma.h"
#endif
#include "gw_malloc.h"
#include "rg_storage.h"
#include "odroid_overlay.h"
#include "appid.h"
#include "bilinear.h"
#include "rg_i18n.h"

#include "wsv_sound.h"
#include "memorymap.h"
#include "supervision.h"
#include "controls.h"
#include "types.h"

#define WIDTH 320
#define WSV_WIDTH (160)
#define WSV_HEIGHT (160)
static uint8_t wsv_framebuffer[WSV_WIDTH*WSV_HEIGHT*2];

static odroid_video_frame_t video_frame = {WSV_WIDTH, WSV_HEIGHT, WSV_WIDTH * 2, 2, 0xFF, -1, NULL, NULL, 0, {}};

#define WSV_FPS 50 // Real hardware is 50.81fps
#define WSV_AUDIO_BUFFER_LENGTH (SV_SAMPLE_RATE / WSV_FPS)
static int8 audioBuffer_wsv[WSV_AUDIO_BUFFER_LENGTH*2]; // *2 as emulator is filling stereo buffer
#define WSV_ROM_BUFF_LENGTH 0x80000 // Largest Watara Supervision Rom is 512kB (Journey to the West)
#ifndef GNW_DISABLE_COMPRESSION
// Memory to handle compressed roms
static uint8 wsv_rom_memory[WSV_ROM_BUFF_LENGTH];
#endif

#define STATE_SAVE_BUFFER_LENGTH (24741)

static void blit_emulator(void);

static bool LoadState(const char *savePathName) {
    // We store data in the not visible framebuffer
    unsigned char *data = (unsigned char *)lcd_get_active_buffer();

    FILE *file = fopen(savePathName, "rb");
    if (file == NULL) {
        return false;
    }

    size_t read = fread(data, STATE_SAVE_BUFFER_LENGTH, 1, file);

    fclose(file);

    if (!read) {
        return false;
    }

    supervision_load_state(data);

    lcd_clear_active_buffer();

    return true;
}

static bool SaveState(const char *savePathName) {
    // We store data in the not visible framebuffer
    lcd_wait_for_vblank();
    unsigned char *data = (unsigned char *)lcd_get_active_buffer();

    int size = supervision_save_state(data);
    assert(size == STATE_SAVE_BUFFER_LENGTH);
    FILE *file = fopen(savePathName, "wb");
    if (file == NULL) {
        return false;
    }

    size_t written = fwrite(data, size, 1, file);

    fclose(file);

    if (!written) {
        return false;
    }

    return true;
}

static void *Screenshot()
{
    lcd_wait_for_vblank();

    lcd_clear_active_buffer();
    blit_emulator();
    return lcd_get_active_buffer();
}

void wsv_pcm_submit() {
    supervision_update_sound((uint8 *)audioBuffer_wsv,WSV_AUDIO_BUFFER_LENGTH*2);

    if (common_emu_sound_loop_is_muted()) {
        return;
    }

    int32_t factor = common_emu_sound_get_volume() / 2; // Divide by 2 to prevent overflow in stereo mixing
    int16_t* sound_buffer = audio_get_active_buffer();
    uint16_t sound_buffer_length = audio_get_buffer_length();

    for (int i = 0; i < sound_buffer_length; i++) {
        /* mix left & right */
        int32_t sample = audioBuffer_wsv[2 * i] + audioBuffer_wsv[2 * i + 1];
        sound_buffer[i] = (sample * factor);
    }
}

__attribute__((optimize("unroll-loops")))
static inline void screen_blit_nn(int32_t dest_width, int32_t dest_height)
{
    static uint32_t lastFPSTime = 0;
    static uint32_t frames = 0;
    uint32_t currentTime = HAL_GetTick();
    uint32_t delta = currentTime - lastFPSTime;

    frames++;

    if (delta >= 1000) {
        int fps = (10000 * frames) / delta;
        printf("FPS: %d.%d, frames %ld, delta %ld ms, skipped %d\n", fps / 10, fps % 10, delta, frames, common_emu_state.skipped_frames);
        frames = 0;
        common_emu_state.skipped_frames = 0;
        lastFPSTime = currentTime;
    }

    int w1 = video_frame.width;
    int h1 = video_frame.height;
    int w2 = dest_width;
    int h2 = dest_height;

    int x_ratio = (int)((w1<<16)/w2) +1;
    int y_ratio = (int)((h1<<16)/h2) +1;
    int hpad = (320 - dest_width) / 2;
    int wpad = (240 - dest_height) / 2;

    int x2;
    int y2;

    uint16_t *screen_buf = (uint16_t*)video_frame.buffer;
    uint16_t *dest = lcd_get_active_buffer();

    PROFILING_INIT(t_blit);
    PROFILING_START(t_blit);

    for (int i=0;i<h2;i++) {
        for (int j=0;j<w2;j++) {
            x2 = ((j*x_ratio)>>16) ;
            y2 = ((i*y_ratio)>>16) ;
            uint16_t b2 = screen_buf[(y2*w1)+x2];
            dest[((i+wpad)*WIDTH)+j+hpad] = b2;
        }
    }

    PROFILING_END(t_blit);

#ifdef PROFILING_ENABLED
    printf("Blit: %d us\n", (1000000 * PROFILING_DIFF(t_blit)) / t_blit_t0.SecondFraction);
#endif
}

static void screen_blit_bilinear(int32_t dest_width)
{
    static uint32_t lastFPSTime = 0;
    static uint32_t frames = 0;
    uint32_t currentTime = HAL_GetTick();
    uint32_t delta = currentTime - lastFPSTime;

    frames++;

    if (delta >= 1000) {
        int fps = (10000 * frames) / delta;
        printf("FPS: %d.%d, frames %ld, delta %ld ms, skipped %d\n", fps / 10, fps % 10, delta, frames, common_emu_state.skipped_frames);
        frames = 0;
        common_emu_state.skipped_frames = 0;
        lastFPSTime = currentTime;
    }

    int w1 = video_frame.width;
    int h1 = video_frame.height;

    int w2 = dest_width;
    int h2 = 240;
    int stride = 320;
    int hpad = (320 - dest_width) / 2;

    uint16_t *dest = lcd_get_active_buffer();

    image_t dst_img;
    dst_img.w = dest_width;
    dst_img.h = 240;
    dst_img.bpp = 2;
    dst_img.pixels = ((uint8_t *) dest) + hpad * 2;

    if (hpad > 0) {
        memset(dest, 0x00, hpad * 2);
    }

    image_t src_img;
    src_img.w = video_frame.width;
    src_img.h = video_frame.height;
    src_img.bpp = 2;
    src_img.pixels = video_frame.buffer;

    float x_scale = ((float) w2) / ((float) w1);
    float y_scale = ((float) h2) / ((float) h1);



    PROFILING_INIT(t_blit);
    PROFILING_START(t_blit);

    imlib_draw_image(&dst_img, &src_img, 0, 0, stride, x_scale, y_scale, NULL, -1, 255, NULL,
                     NULL, IMAGE_HINT_BILINEAR, NULL, NULL);

    PROFILING_END(t_blit);

#ifdef PROFILING_ENABLED
    printf("Blit: %d us\n", (1000000 * PROFILING_DIFF(t_blit)) / t_blit_t0.SecondFraction);
#endif
}

static inline void screen_blit_v3to5(void) {
    static uint32_t lastFPSTime = 0;
    static uint32_t frames = 0;
    uint32_t currentTime = HAL_GetTick();
    uint32_t delta = currentTime - lastFPSTime;

    frames++;

    if (delta >= 1000) {
        int fps = (10000 * frames) / delta;
        printf("FPS: %d.%d, frames %ld, delta %ld ms, skipped %d\n", fps / 10, fps % 10, delta, frames, common_emu_state.skipped_frames);
        frames = 0;
        common_emu_state.skipped_frames = 0;
        lastFPSTime = currentTime;
    }

    uint16_t *dest = lcd_get_active_buffer();

    PROFILING_INIT(t_blit);
    PROFILING_START(t_blit);

#define CONV(_b0)    (((0b11111000000000000000000000&_b0)>>10) | ((0b000001111110000000000&_b0)>>5) | ((0b0000000000011111&_b0)))
#define EXPAND(_b0)  (((0b1111100000000000 & _b0) << 10) | ((0b0000011111100000 & _b0) << 5) | ((0b0000000000011111 & _b0)))

    int y_src = 0;
    int y_dst = 0;
    int w = video_frame.width;
    int h = video_frame.height;
    for (; y_src < h; y_src += 3, y_dst += 5) {
        int x_src = 0;
        int x_dst = 0;
        for (; x_src < w; x_src += 1, x_dst += 2) {
            uint16_t *src_col = &((uint16_t *)video_frame.buffer)[(y_src * w) + x_src];
            uint32_t b0 = EXPAND(src_col[w * 0]);
            uint32_t b1 = EXPAND(src_col[w * 1]);
            uint32_t b2 = EXPAND(src_col[w * 2]);

            dest[((y_dst + 0) * WIDTH) + x_dst] = CONV(b0);
            dest[((y_dst + 1) * WIDTH) + x_dst] = CONV((b0+b1)>>1);
            dest[((y_dst + 2) * WIDTH) + x_dst] = CONV(b1);
            dest[((y_dst + 3) * WIDTH) + x_dst] = CONV((b1+b2)>>1);
            dest[((y_dst + 4) * WIDTH) + x_dst] = CONV(b2);

            dest[((y_dst + 0) * WIDTH) + x_dst + 1] = CONV(b0);
            dest[((y_dst + 1) * WIDTH) + x_dst + 1] = CONV((b0+b1)>>1);
            dest[((y_dst + 2) * WIDTH) + x_dst + 1] = CONV(b1);
            dest[((y_dst + 3) * WIDTH) + x_dst + 1] = CONV((b1+b2)>>1);
            dest[((y_dst + 4) * WIDTH) + x_dst + 1] = CONV(b2);
        }
    }

    PROFILING_END(t_blit);

#ifdef PROFILING_ENABLED
    printf("Blit: %d us\n", (1000000 * PROFILING_DIFF(t_blit)) / t_blit_t0.SecondFraction);
#endif
}


static inline void screen_blit_jth(void) {
    static uint32_t lastFPSTime = 0;
    static uint32_t frames = 0;
    uint32_t currentTime = HAL_GetTick();
    uint32_t delta = currentTime - lastFPSTime;

    frames++;

    if (delta >= 1000) {
        int fps = (10000 * frames) / delta;
        printf("FPS: %d.%d, frames %ld, delta %ld ms, skipped %d\n", fps / 10, fps % 10, delta, frames, common_emu_state.skipped_frames);
        frames = 0;
        common_emu_state.skipped_frames = 0;
        lastFPSTime = currentTime;
    }


    uint16_t* screen_buf = (uint16_t*)video_frame.buffer;
    uint16_t *dest = lcd_get_active_buffer();

    PROFILING_INIT(t_blit);
    PROFILING_START(t_blit);


    int w1 = video_frame.width;
    int h1 = video_frame.height;
    int w2 = 320;
    int h2 = 240;

    const int border = 24;

    // Iterate on dest buf rows
    for(int y = 0; y < border; ++y) {
        uint16_t *src_row  = &screen_buf[y * w1];
        uint16_t *dest_row = &dest[y * w2];
        for (int x = 0, xsrc=0; x < w2; x+=2,xsrc++) {
            dest_row[x]     = src_row[xsrc];
            dest_row[x + 1] = src_row[xsrc];
        }
    }

    for (int y = border, src_y = border; y < h2-border; y+=2, src_y++) {
        uint16_t *src_row  = &screen_buf[src_y * w1];
        uint32_t *dest_row0 = (uint32_t *) &dest[y * w2];
        for (int x = 0, xsrc=0; x < w2; x++,xsrc++) {
            uint32_t col = src_row[xsrc];
            dest_row0[x] = (col | (col << 16));
        }
    }

    for (int y = border, src_y = border; y < h2-border; y+=2, src_y++) {
        uint16_t *src_row  = &screen_buf[src_y * w1];
        uint32_t *dest_row1 = (uint32_t *)&dest[(y + 1) * w2];
        for (int x = 0, xsrc=0; x < w2; x++,xsrc++) {
            uint32_t col = src_row[xsrc];
            dest_row1[x] = (col | (col << 16));
        }
    }

    for(int y = 0; y < border; ++y) {
        uint16_t *src_row  = &screen_buf[(h1-border+y) * w1];
        uint16_t *dest_row = &dest[(h2-border+y) * w2];
        for (int x = 0, xsrc=0; x < w2; x+=2,xsrc++) {
            dest_row[x]     = src_row[xsrc];
            dest_row[x + 1] = src_row[xsrc];
        }
    }


    PROFILING_END(t_blit);

#ifdef PROFILING_ENABLED
    printf("Blit: %d us\n", (1000000 * PROFILING_DIFF(t_blit)) / t_blit_t0.SecondFraction);
#endif
}

static void blit_emulator(void)
{
    odroid_display_scaling_t scaling = odroid_display_get_scaling_mode();
    odroid_display_filter_t filtering = odroid_display_get_filter_mode();

    switch (scaling) {
    case ODROID_DISPLAY_SCALING_OFF:
        // Original Resolution
        screen_blit_nn(160, 160);
        break;
    case ODROID_DISPLAY_SCALING_FIT:
        // Full height, borders on the side
        switch (filtering) {
        case ODROID_DISPLAY_FILTER_OFF:
            /* fall-through */
        case ODROID_DISPLAY_FILTER_SHARP:
            // crisp nearest neighbor scaling
            screen_blit_nn(266, 240);
            break;
        case ODROID_DISPLAY_FILTER_SOFT:
            // soft bilinear scaling
            screen_blit_bilinear(266);
            break;
        default:
            printf("Unknown filtering mode %d\n", filtering);
            assert(!"Unknown filtering mode");
        }
        break;
    case ODROID_DISPLAY_SCALING_FULL:
        // full height, full width
        switch (filtering) {
        case ODROID_DISPLAY_FILTER_OFF:
            // crisp nearest neighbor scaling
            screen_blit_nn(320, 240);
            break;
        case ODROID_DISPLAY_FILTER_SHARP:
            // sharp bilinear-ish scaling
            screen_blit_v3to5();
            break;
        case ODROID_DISPLAY_FILTER_SOFT:
            // soft bilinear scaling
            screen_blit_bilinear(320);
            break;
        default:
            printf("Unknown filtering mode %d\n", filtering);
            assert(!"Unknown filtering mode");
        }
        break;
    case ODROID_DISPLAY_SCALING_CUSTOM:
        // compressed top and bottom sections, full width
        screen_blit_jth();
        break;
    default:
        printf("Unknown scaling mode %d\n", scaling);
        assert(!"Unknown scaling mode");
        break;
    }
}

static void blit(void) {
    blit_emulator();
    common_ingame_overlay();
}


static bool palette_update_cb(odroid_dialog_choice_t *option, odroid_dialog_event_t event, uint32_t repeat)
{

    const char *palette_names[] = {
        curr_lang->s_wsv_palette_Default, 
        curr_lang->s_wsv_palette_Amber, 
        curr_lang->s_wsv_palette_Green, 
        curr_lang->s_wsv_palette_Blue, 
        curr_lang->s_wsv_palette_BGB, 
        curr_lang->s_wsv_palette_Wataroo};
    
    int8 wsv_pal = supervision_get_color_scheme();
    int max = SV_COLOR_SCHEME_COUNT - 1;

    if (event == ODROID_DIALOG_PREV) wsv_pal = wsv_pal > 0 ? wsv_pal - 1 : max;
    if (event == ODROID_DIALOG_NEXT) wsv_pal = wsv_pal < max ? wsv_pal + 1 : 0;

    if (event == ODROID_DIALOG_PREV || event == ODROID_DIALOG_NEXT) {
        odroid_settings_Palette_set(wsv_pal);
        supervision_set_color_scheme(wsv_pal);
    }

    strcpy(option->value, palette_names[wsv_pal]);

    return event == ODROID_DIALOG_ENTER;
}

void wsv_input_read(odroid_gamepad_state_t *joystick) {
    uint8 controls_state = 0x00;
    if (joystick->values[ODROID_INPUT_LEFT])   controls_state|=0x02;
    if (joystick->values[ODROID_INPUT_RIGHT])  controls_state|=0x01;
    if (joystick->values[ODROID_INPUT_UP])     controls_state|=0x08;
    if (joystick->values[ODROID_INPUT_DOWN])   controls_state|=0x04;
    if (joystick->values[ODROID_INPUT_A])      controls_state|=0x20;
    if (joystick->values[ODROID_INPUT_B])      controls_state|=0x10;
    if (joystick->values[ODROID_INPUT_START] || joystick->values[ODROID_INPUT_X])  controls_state|=0x80;
    if (joystick->values[ODROID_INPUT_SELECT] || joystick->values[ODROID_INPUT_Y]) controls_state|=0x40;
    supervision_set_input(controls_state);
}

size_t wsv_getromdata(unsigned char **data) {
    /* src pointer to the ROM data in the external flash (raw or LZ4) */
#ifndef GNW_DISABLE_COMPRESSION
#if SD_CARD == 1
#error "Roms compression is not supported on SD Card"
#else
    uint32_t src_size = 0;
    const unsigned char *src = odroid_overlay_cache_file_in_flash(ACTIVE_FILE->path, &src_size, false);
    unsigned char *dest = (unsigned char *)wsv_rom_memory;
    if (src == NULL || src_size == 0) {
        *data = NULL;
        return 0;
    }

    if(strcmp(ACTIVE_FILE->ext, "lzma") == 0){
        size_t n_decomp_bytes;
        n_decomp_bytes = lzma_inflate(dest, WSV_ROM_BUFF_LENGTH, src, src_size);
        *data = dest;
        return n_decomp_bytes;
    }
    else
    {
        *data = (unsigned char *)src;
        return src_size;
    }
#endif
#else
    ram_start = (uint32_t)&_OVERLAY_WSV_BSS_END;
    uint32_t size = ACTIVE_FILE->size;
    if (size > ram_get_free_size()) {
        *data = odroid_overlay_cache_file_in_flash(ACTIVE_FILE->path, &size, false);
    } else {
        *data = ram_malloc(size);
        if (*data != NULL) {
            odroid_overlay_cache_file_in_ram(ACTIVE_FILE->path, *data);
        }
    }
    return size;
#endif
}

int app_main_wsv(uint8_t load_state, uint8_t start_paused, int8_t save_slot)
{
    char pal_name[16];
    uint32 rom_length = 0;
    uint8 *rom_ptr = NULL;
    odroid_gamepad_state_t joystick;
    odroid_dialog_choice_t options[] = {
        {100, curr_lang->s_Palette, pal_name, 1, &palette_update_cb},
        ODROID_DIALOG_CHOICE_LAST
    };

    if (start_paused) {
        common_emu_state.pause_after_frames = 2;
        odroid_audio_mute(true);
    } else {
        common_emu_state.pause_after_frames = 0;
    }
    common_emu_state.frame_time_10us = (uint16_t)(100000 / WSV_FPS + 0.5f);
    lcd_set_refresh_rate(50);

    video_frame.buffer = wsv_framebuffer;

    odroid_system_init(APPID_WSV, SV_SAMPLE_RATE);
    odroid_system_emu_init(&LoadState, &SaveState, &Screenshot, NULL, NULL, NULL, NULL);

    // Init Sound
    audio_start_playing(WSV_AUDIO_BUFFER_LENGTH);

    supervision_set_color_scheme(SV_COLOR_SCHEME_DEFAULT);

    supervision_init(); //Init the emulator

    rom_length = wsv_getromdata(&rom_ptr);
    supervision_load(rom_ptr, rom_length);

    if (load_state) {
        odroid_system_emu_load_state(save_slot);
    } else {
        lcd_clear_buffers();
    }
    while(1)
    {
        wdog_refresh();

        bool drawFrame = common_emu_frame_loop();

        odroid_input_read_gamepad(&joystick);
        common_emu_input_loop(&joystick, options, &blit);
        common_emu_input_loop_handle_turbo(&joystick);

        wsv_input_read(&joystick);

        supervision_exec((uint16 *)wsv_framebuffer);

        if (drawFrame) {
            blit();
            lcd_swap();
        }

        wsv_pcm_submit();

        common_emu_sound_sync(false);
    }

    return 0;
}
