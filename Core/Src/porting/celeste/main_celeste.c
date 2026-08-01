#include <odroid_system.h>
#include <string.h>
#include <assert.h>

#include "main.h"
#include "bilinear.h"
#include "gw_lcd.h"
#include "gw_linker.h"
#include "rg_i18n.h"
#include "gw_buttons.h"
#include "common.h"
#include "rom_manager.h"
#include "appid.h"
#include "gw_malloc.h"

#include "celeste.h"
#include "tilemap.h"
#include "celeste_audio.h"
#include "celeste_data.h"

#define WIDTH_P8  128
#define PITCH_P8  148
#define HEIGHT_P8 128
#define CELESTE_FPS 30
#define CELESTE_AUDIO_SAMPLE_RATE 22050
#define CELESTE_AUDIO_BUFFER_LENGTH (CELESTE_AUDIO_SAMPLE_RATE/CELESTE_FPS)

// We add 20 pixels height in the buffer because sprites can be drawn outside of the screen, some
// code could be added to control that but it's more cpu efficient to just increase buffer size
static uint8_t fb_celeste [PITCH_P8*(HEIGHT_P8+20)];
static bool enable_screenshake = true;

typedef struct {
    int16_t x, y;
    uint16_t w, h;
} SDL_Rect;

typedef struct SDL_Surface {
   int w, h;
   uint16_t pitch;
   void *pixels;
   SDL_Rect clip_rect;
} SDL_Surface;

static SDL_Surface screen_local;
static SDL_Surface* screen = &screen_local;
static SDL_Surface gfx_local;
static SDL_Surface* gfx = &gfx_local;
static SDL_Surface font_local;
static SDL_Surface* font = &font_local;

static int16_t audioBuffer[CELESTE_AUDIO_BUFFER_LENGTH];

static void blit();

// --- MAIN
static uint16_t buttons_state = 0;

struct track_info {
    int8_t index;
    uint8_t fade;
    uint8_t mask;
};

struct track_info current_track = {-1, 0, 0};

static bool SaveState(const char *savePathName)
{
    uint8_t *data = lcd_get_active_buffer();
    size_t size;

    Celeste_P8_save_state(data);
    size = Celeste_P8_get_state_size();

    FILE *file = fopen(savePathName, "wb");
    if (file == NULL) {
        return false;
    }

    size_t written = fwrite(data, 1, size, file);
    if (written != size) {
        return false;
    }

    written = fwrite((unsigned char *)&current_track, 1, sizeof(current_track), file);
    if (written != sizeof(current_track)) {
        return false;
    }

    fclose(file);
    
    return true;
}

static bool LoadState(const char *savePathName)
{
    // We store data in the not visible framebuffer
    unsigned char *data = (unsigned char *)lcd_get_active_buffer();
    size_t size = Celeste_P8_get_state_size();

    FILE *file = fopen(savePathName, "rb");
    if (file == NULL) {
        return false;
    }

    size_t read = fread(data, 1, size, file);
    if (read != size) {
        return false;
    }

    read = fread((unsigned char *)&current_track, 1, sizeof(current_track), file);
    if (read != sizeof(current_track)) {
        return false;
    }

    fclose(file);

    Celeste_P8_load_state(data);

    celeste_api_music(current_track.index, current_track.fade, current_track.mask);

    lcd_clear_active_buffer();

    return true;
}

#define RGB565(red,green,blue) ((blue >> 3) & 0x1f) | (((green >> 2) & 0x3f) << 5) | (((red >> 3) & 0x1f) << 11)
static const uint16_t base_palette[16] = {
    RGB565(0x00, 0x00, 0x00),
    RGB565(0x1d, 0x2b, 0x53),
    RGB565(0x7e, 0x25, 0x53),
    RGB565(0x00, 0x87, 0x51),
    RGB565(0xab, 0x52, 0x36),
    RGB565(0x5f, 0x57, 0x4f),
    RGB565(0xc2, 0xc3, 0xc7),
    RGB565(0xff, 0xf1, 0xe8),
    RGB565(0xff, 0x00, 0x4d),
    RGB565(0xff, 0xa3, 0x00),
    RGB565(0xff, 0xec, 0x27),
    RGB565(0x00, 0xe4, 0x36),
    RGB565(0x29, 0xad, 0xff),
    RGB565(0x83, 0x76, 0x9c),
    RGB565(0xff, 0x77, 0xa8),
    RGB565(0xff, 0xcc, 0xaa)
};
static uint8_t base_color[16] = {0,1,2,3,4,5,6,7,8,9,10,11,12,13,14,15};
static uint8_t color[16];

static inline uint16_t getcolorid(char idx) {
    return color[idx%16];
}

static void *Screenshot()
{
    lcd_wait_for_vblank();

    lcd_clear_active_buffer();
    blit();
    return lcd_get_active_buffer();
}

static void ResetPalette(void) {
    memcpy(color, base_color, sizeof color);
}

static int gettileflag(int tile, int flag) {
    return tile < sizeof(tile_flags)/sizeof(*tile_flags) && (tile_flags[tile] & (1 << flag)) != 0;
}

static void p8_rectfill(int x0, int y0, int x1, int y1, int col) {
    if (x0 > x1) { int t = x0; x0 = x1; x1 = t; }
    if (y0 > y1) { int t = y0; y0 = y1; y1 = t; }

    /* Clip to screen bounds */
    if (x1 < 0 || y1 < 0 || x0 >= WIDTH_P8 || y0 >= HEIGHT_P8) return;
    x0 = x0 < 0 ? 0 : x0;
    y0 = y0 < 0 ? 0 : y0;
    x1 = x1 >= WIDTH_P8  ? WIDTH_P8  - 1 : x1;
    y1 = y1 >= HEIGHT_P8 ? HEIGHT_P8 - 1 : y1;

    int colorid = getcolorid(col);

    for (int i = y0; i <= y1; i++) {
        for (int j = x0; j <= x1; j++) {
            fb_celeste[i * PITCH_P8 + j] = colorid;
        }
    }
}

#define CLAMP(v,min,max) v = v < min ? min : v >= max ? max-1 : v;

static void p8_line(int x0, int y0, int x1, int y1, unsigned char color) {
    CLAMP(x0, 0, WIDTH_P8);
    CLAMP(y0, 0, HEIGHT_P8);
    CLAMP(x1, 0, WIDTH_P8);
    CLAMP(y1, 0, HEIGHT_P8);

    int sx, sy, dx, dy, err, e2;
    int colorid=getcolorid(color);

    dx = abs(x1 - x0);
    dy = abs(y1 - y0);

    if (!dx && !dy)
        return;

    if (x0 < x1)
        sx = 1;
    else
        sx = -1;

    if (y0 < y1)
        sy = 1;
    else
        sy = -1;

    err = dx - dy;

    if (!dy && !dx) {
        // Single dot
        int index = (y0 * PITCH_P8 + x0);
        fb_celeste[index] = colorid;
        return;
    } else if (!dx) {
        // Vertical line
        for (int y = y0; y != y1; y += sy) {
            int index = (y * PITCH_P8 + x0);
            fb_celeste[index] = colorid;
        }
    } else if (!dy) {
        // Horizontal line
        for (int x = x0; x != x1; x += sx) {
            int index = (y0 * PITCH_P8 + x);
            fb_celeste[index] = colorid;
        }
    }

    while (x0 != x1 || y0 != y1) {
        int index = (y0 * PITCH_P8 + x0);
        fb_celeste[index] = colorid;

        e2 = 2 * err;

        if (e2 > -dy) {
            err -= dy;
            x0 += sx;
        }

        if (e2 < dx) {
            err += dx;
            y0 += sy;
        }
    }
}
#undef CLAMP

static inline void Xblit(SDL_Surface* src, SDL_Rect* srcrect, SDL_Surface* dst, SDL_Rect* dstrect, int color, int flipx, int flipy) {
    SDL_Rect fulldst;
    /* If the destination rectangle is NULL, use the entire dest surface */
    if (!dstrect)
        dstrect = (fulldst = (SDL_Rect){0,0,dst->w,dst->h}, &fulldst);

    int srcx, srcy, w, h;
    
    /* clip the source rectangle to the source surface */
    if (srcrect) {
        int maxw, maxh;

        srcx = srcrect->x;
        w = srcrect->w;
        if (srcx < 0) {
            w += srcx;
            dstrect->x -= srcx;
            srcx = 0;
        }
        maxw = src->w - srcx;
        if (maxw < w)
            w = maxw;

        srcy = srcrect->y;
        h = srcrect->h;
        if (srcy < 0) {
            h += srcy;
            dstrect->y -= srcy;
            srcy = 0;
        }
        maxh = src->h - srcy;
        if (maxh < h)
            h = maxh;

    } else {
        srcx = srcy = 0;
        w = src->w;
        h = src->h;
    }

    /* clip the destination rectangle against the clip rectangle */
    {
        SDL_Rect *clip = &dst->clip_rect;
        int dx, dy;

        dx = clip->x - dstrect->x;
        if (dx > 0) {
            w -= dx;
            dstrect->x += dx;
            srcx += dx;
        }
        dx = dstrect->x + w - clip->x - clip->w;
        if (dx > 0)
            w -= dx;

        dy = clip->y - dstrect->y;
        if (dy > 0) {
            h -= dy;
            dstrect->y += dy;
            srcy += dy;
        }
        dy = dstrect->y + h - clip->y - clip->h;
        if (dy > 0)
            h -= dy;
    }

    if (w && h) {
        unsigned char* srcpix = src->pixels;
        int srcpitch = src->pitch;
        uint8_t* dstpix = dst->pixels;
    #define _blitter(dp, xflip) do                                                                  \
    for (int y = 0; y < h; y++) for (int x = 0; x < w; x++) {                                       \
      unsigned char p = srcpix[!xflip ? srcx+x+(srcy+y)*srcpitch : srcx+(w-x-1)+(srcy+y)*srcpitch]; \
      if (p) dstpix[dstrect->x+x + (dstrect->y+y)*dst->w] = getcolorid(dp);                         \
    } while(0)
        if (color && flipx) _blitter(color, 1);
        else if (!color && flipx) _blitter(p, 1);
        else if (color && !flipx) _blitter(color, 0);
        else if (!color && !flipx) _blitter(p, 0);
        #undef _blitter
    }
}

static void p8_print(const char* str, int x, int y, int col) {
    for (char c = *str; c; c = *(++str)) {
        c &= 0x7F;
        SDL_Rect srcrc = {8*(c%16), 8*(c/16)};
        srcrc.w = srcrc.h = 8;
        
        SDL_Rect dstrc = {x, y, 1, 1};
        Xblit(font, &srcrc, screen, &dstrc, col, 0,0);
        x += 4;
    }
}

int pico8emu(CELESTE_P8_CALLBACK_TYPE call, ...) {
    static int camera_x = 0, camera_y = 0;
    if (!enable_screenshake) {
        camera_x = camera_y = 0;
    }

    va_list args;
    int ret = 0;
    va_start(args, call);
    
    #define INT_ARG() va_arg(args, int)
    #define BOOL_ARG() (Celeste_P8_bool_t)va_arg(args, int)
    #define RET_INT(_i)   do {ret = (_i); goto end;} while (0)
    #define RET_BOOL(_b) RET_INT(!!(_b))

    switch (call) {
        case CELESTE_P8_MUSIC: { //music(idx,fade,mask)
            int index = INT_ARG();
            int fade = INT_ARG();
            int mask = INT_ARG();

            current_track.index = index;
            current_track.fade  = fade;
            current_track.mask  = mask;

            celeste_api_music(index, fade, mask);
        } break;
        case CELESTE_P8_SPR: { //spr(sprite,x,y,cols,rows,flipx,flipy)
            int sprite = INT_ARG();
            int x = INT_ARG();
            int y = INT_ARG();
            int cols = INT_ARG();
            int rows = INT_ARG();
            int flipx = BOOL_ARG();
            int flipy = BOOL_ARG();

            (void)cols;
            (void)rows;

            assert(rows == 1 && cols == 1);

            if (sprite >= 0) {
                SDL_Rect srcrc = {
                    8*(sprite % 16),
                    8*(sprite / 16)
                };
                srcrc.w = srcrc.h = 8;
                SDL_Rect dstrc = {
                    (x - camera_x), (y - camera_y),
                    1, 1
                };
                Xblit(gfx, &srcrc, screen, &dstrc, 0,flipx,flipy);
            }
        } break;
        case CELESTE_P8_BTN: { //btn(b)
            int b = INT_ARG();
            assert(b >= 0 && b <= 5); 
            RET_BOOL(buttons_state & (1 << b));
        } break;
        case CELESTE_P8_SFX: { //sfx(id)
            int id = INT_ARG();
        
            celeste_api_sfx(id, -1, 0);
        } break;
        case CELESTE_P8_PAL: { //pal(a,b)
            int a = INT_ARG();
            int b = INT_ARG();
            if (a >= 0 && a < 16 && b >= 0 && b < 16) {
                //swap palette colors index
                color[a] = b;
            }
        } break;
        case CELESTE_P8_PAL_RESET: { //pal()
            ResetPalette();
        } break;
        case CELESTE_P8_CIRCFILL: { //circfill(x,y,r,col)
            int cx = INT_ARG() - camera_x;
            int cy = INT_ARG() - camera_y;
            int r = INT_ARG();
            int col = INT_ARG();

            if (r <= 1) {
                p8_rectfill(cx-1, cy, cx-1+3, cy+1, col);
                p8_rectfill(cx, cy-1, cx+1, cy+2, col);
            } else if (r <= 2) {
                p8_rectfill(cx-2, cy-1, cx+3, cy+2, col);
                p8_rectfill(cx-1, cy-2, cx+2, cy+3, col);
            } else if (r <= 3) {
                p8_rectfill(cx-3, cy-1, cx+4, cy+2, col);
                p8_rectfill(cx-1, cy-3, cx+2, cy+4, col);
                p8_rectfill(cx-2, cy-2, cx+3, cy+3, col);
            }
        } break;
        case CELESTE_P8_PRINT: { //print(str,x,y,col)
            const char* str = va_arg(args, const char*);
            int x = INT_ARG() - camera_x;
            int y = INT_ARG() - camera_y;
            int col = INT_ARG() % 16;

            if (!strcmp(str, "x+c")) {
                //this is confusing, as G&W uses a+b button, so use this hack to make it more appropiate
                str = "a+b";
            }

            p8_print(str,x,y,col);
        } break;
        case CELESTE_P8_RECTFILL: { //rectfill(x0,y0,x1,y1,col)
            int x0 = INT_ARG() - camera_x;
            int y0 = INT_ARG() - camera_y;
            int x1 = INT_ARG() - camera_x;
            int y1 = INT_ARG() - camera_y;
            int col = INT_ARG();

            p8_rectfill(x0,y0,x1,y1,col);
        } break;
        case CELESTE_P8_LINE: { //line(x0,y0,x1,y1,col)
            int x0 = INT_ARG() - camera_x;
            int y0 = INT_ARG() - camera_y;
            int x1 = INT_ARG() - camera_x;
            int y1 = INT_ARG() - camera_y;
            int col = INT_ARG();

            p8_line(x0,y0,x1,y1,col);
        } break;
        case CELESTE_P8_MGET: { //mget(tx,ty)
            int tx = INT_ARG();
            int ty = INT_ARG();

            RET_INT(tilemap_data[tx+ty*128]);
        } break;
        case CELESTE_P8_CAMERA: { //camera(x,y)
            if (enable_screenshake) {
                camera_x = INT_ARG();
                camera_y = INT_ARG();
            }
        } break;
        case CELESTE_P8_FGET: { //fget(tile,flag)
            int tile = INT_ARG();
            int flag = INT_ARG();

            RET_INT(gettileflag(tile, flag));
        } break;
        case CELESTE_P8_MAP: { //map(mx,my,tx,ty,mw,mh,mask)
            int mx = INT_ARG(), my = INT_ARG();
            int tx = INT_ARG(), ty = INT_ARG();
            int mw = INT_ARG(), mh = INT_ARG();
            int mask = INT_ARG();
            
            for (int x = 0; x < mw; x++) {
                for (int y = 0; y < mh; y++) {
                    int tile = tilemap_data[x + mx + (y + my)*128];
                    //hack
                    if (mask == 0 || (mask == 4 && tile_flags[tile] == 4) || gettileflag(tile, mask != 4 ? mask-1 : mask)) {
                        SDL_Rect srcrc = {
                            8*(tile % 16),
                            8*(tile / 16)
                        };
                        srcrc.w = srcrc.h = 8;
                        SDL_Rect dstrc = {
                            (tx+x*8 - camera_x), (ty+y*8 - camera_y),
                            8, 8
                        };

                        Xblit(gfx, &srcrc, screen, &dstrc, 0, 0, 0);
                    }
                }
            }
        } break;
    }

    end:
    va_end(args);
    return ret;
}

// No scaling
__attribute__((optimize("unroll-loops")))
static inline void blit_normal(uint8_t *src, uint16_t *framebuffer) {
    int offsetx = WIDTH/2-WIDTH_P8;
    int offsety = 4;
    for (int y=0;y<HEIGHT/2;y++)
    {
        for (int x = 0; x < WIDTH_P8-1; x++)
        {
            framebuffer[offsetx+2*y*WIDTH+2*x] = base_palette[src[(y+offsety)*PITCH_P8+x]];
            framebuffer[offsetx+2*y*WIDTH+2*x+1] = base_palette[src[(y+offsety)*PITCH_P8+x]];
            framebuffer[offsetx+2*y*WIDTH+2*x+WIDTH] = base_palette[src[(y+offsety)*PITCH_P8+x]];
            framebuffer[offsetx+2*y*WIDTH+2*x+WIDTH+1] = base_palette[src[(y+offsety)*PITCH_P8+x]];
        }
    }
}

__attribute__((optimize("unroll-loops")))
static inline void screen_blit_nn(uint8_t *src, uint16_t *framebuffer, uint16_t width)
{
    uint16_t w1 = WIDTH_P8-1;
    uint16_t h1 = HEIGHT_P8;
    uint16_t w2 = width;
    uint16_t h2 = HEIGHT;
    uint8_t x_offset = (WIDTH-width)/2;
    int x_ratio = (int)((w1<<16)/w2) +1;
    int y_ratio = (int)((h1<<16)/h2) +1;

    int x2;
    int y2;

    for (int i=0;i<h2;i++) {
        for (int j=0;j<w2;j++) {
            x2 = ((j*x_ratio)>>16) ;
            y2 = ((i*y_ratio)>>16) ;
            uint8_t b2 = src[y2*PITCH_P8+x2];
            framebuffer[(i*WIDTH)+j+x_offset] = base_palette[b2];
        }
    }
}

static void blit()
{
    odroid_display_scaling_t scaling = odroid_display_get_scaling_mode();

    uint8_t *src = fb_celeste;
    uint16_t *framebuffer = lcd_get_active_buffer();
    switch (scaling) {
    case ODROID_DISPLAY_SCALING_OFF:
        // Full height, missing 16 pixels of height on screen
        blit_normal(src, framebuffer);
        break;
    case ODROID_DISPLAY_SCALING_FIT:
        // Full height and width
        screen_blit_nn(src, framebuffer,WIDTH_P8*2);
        break;
    case ODROID_DISPLAY_SCALING_FULL:
    case ODROID_DISPLAY_SCALING_CUSTOM:
        screen_blit_nn(src, framebuffer, WIDTH);
        break;
    default:
        printf("Unknown scaling mode %d\n", scaling);
        assert(!"Unknown scaling mode");
        break;
    }
    common_ingame_overlay();
}

static void update_sound_celeste() {
    celeste_fill_audio_buffer(audioBuffer, 0, CELESTE_AUDIO_BUFFER_LENGTH);

    if (common_emu_sound_loop_is_muted()) {
        return;
    }

    int32_t factor = common_emu_sound_get_volume();
    int16_t* sound_buffer = audio_get_active_buffer();
    uint16_t sound_buffer_length = audio_get_buffer_length();

    for (int i = 0; i < sound_buffer_length; i++) {
        int32_t sample = (audioBuffer[i]);
        sound_buffer[i] = (sample * factor) >> 8;
    }
}

void app_main_celeste(uint8_t load_state, uint8_t start_paused, int8_t save_slot)
{
    odroid_dialog_choice_t options[] = {
        ODROID_DIALOG_CHOICE_LAST
    };

    gfx->w = 128;
    gfx->pitch = 128;
    gfx->h = 64;
    gfx->pixels = gfx_data;

    font->w = 128;
    font->pitch = 128;
    font->h = 85;
    font->pixels = font_data;

    screen->w = PITCH_P8;
    screen->pitch = PITCH_P8;
    screen->h = HEIGHT_P8;
    screen->clip_rect.x = 0;
    screen->clip_rect.y = 0;
    screen->clip_rect.w = WIDTH;
    screen->clip_rect.h = HEIGHT;
    screen->pixels = fb_celeste;

    odroid_gamepad_state_t joystick;

    if (start_paused) {
        common_emu_state.pause_after_frames = 2;
    } else {
        common_emu_state.pause_after_frames = 0;
    }
    common_emu_state.frame_time_10us = (uint16_t)(100000 / CELESTE_FPS + 0.5f);

    odroid_system_init(APPID_HOMEBREW, CELESTE_AUDIO_SAMPLE_RATE);
    odroid_system_emu_init(&LoadState, &SaveState, &Screenshot, NULL, NULL, NULL, NULL);

    // Init Sound
    audio_start_playing(CELESTE_AUDIO_BUFFER_LENGTH);

    if (start_paused) {
        common_emu_state.pause_after_frames = 2;
    } else {
        common_emu_state.pause_after_frames = 0;
    }

    celeste_init_audio();
    int pico8emu(CELESTE_P8_CALLBACK_TYPE call, ...);
    Celeste_P8_set_call_func(pico8emu);
    Celeste_P8_set_rndseed(4);
    Celeste_P8_init();

    if (load_state) {
        odroid_system_emu_load_state(save_slot);
    } else {
        lcd_clear_buffers();
    }

    while (true)
    {
        buttons_state = 0;
        screen->pixels = fb_celeste;

        wdog_refresh();

        bool drawFrame = common_emu_frame_loop();

        odroid_input_read_gamepad(&joystick);
        common_emu_input_loop(&joystick, options, &blit);
        common_emu_input_loop_handle_turbo(&joystick);

        if (joystick.values[ODROID_INPUT_LEFT]) buttons_state |= (1<<0);
        if (joystick.values[ODROID_INPUT_RIGHT]) buttons_state |= (1<<1);
        if (joystick.values[ODROID_INPUT_UP]) buttons_state |= (1<<2);
        if (joystick.values[ODROID_INPUT_DOWN]) buttons_state |= (1<<3);
        if (joystick.values[ODROID_INPUT_A]) buttons_state |= (1<<4);
        if (joystick.values[ODROID_INPUT_B]) buttons_state |= (1<<5);

        Celeste_P8_update();
        Celeste_P8_draw();
        update_sound_celeste();

        if (drawFrame) {
            blit();
        }
        lcd_swap();

        common_emu_sound_sync(false);
    }
}
