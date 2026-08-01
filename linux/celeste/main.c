#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>

#include <SDL2/SDL.h>

#include <odroid_system.h>

#include "porting.h"
#include "crc32.h"

#include <string.h>
#include "celeste.h"
#include "tilemap.h"
#include "celeste_audio.h"
#include "celeste_data.h"

#define WIDTH_P8 128
#define PITCH_P8  148
#define HEIGHT_P8 128
#define WIDTH  320
#define HEIGHT 240
#define BPP      2
#define SCALE    2


#define APP_ID 30

#define SAMPLE_RATE   (22050)

SDL_Window *window;
SDL_Renderer *renderer;
SDL_Texture *fb_texture;
uint16_t fb_data[WIDTH * HEIGHT * BPP];
SDL_AudioSpec wanted;

static uint16_t buttons_state = 0;

// We add 20 pixels height in the buffer because sprites can be drawn outside of the screen, some
// code could be added to control that but it's more cpu efficient to just increase buffer size
static uint8_t fb_celeste[(128+20)*(128+20)];

// current track info for savestates
struct track_info {
    uint8_t index;
    uint8_t fade;
    uint8_t mask;
};

struct track_info current_track = {-1, 0, 0};

/*-------------------------------*/

static bool SaveState(const char *savePathName)
{
    printf("Celeste_P8_get_state_size()= %ld\n",Celeste_P8_get_state_size());
    uint8_t *buffer = malloc(Celeste_P8_get_state_size());

	Celeste_P8_save_state(buffer);

    FILE *fp = fopen(savePathName, "wb");
    fwrite(buffer, 1, Celeste_P8_get_state_size(), fp);
	fwrite(&current_track, 1, sizeof(current_track), fp);
    fclose(fp);

    free(buffer);

    return true;
}

static bool LoadState(const char *savePathName)
{
    uint8_t *buffer = malloc(Celeste_P8_get_state_size());

    FILE *fp = fopen(savePathName, "rb");
    fread(buffer, 1, Celeste_P8_get_state_size(), fp);
	fread(&current_track, 1, sizeof(current_track),fp);
    fclose(fp);

    Celeste_P8_load_state(buffer);
    free(buffer);

	celeste_api_music(current_track.index, current_track.fade, current_track.mask);
    return true;
}

static uint32_t fceu_joystick; /* player input data, 1 byte per player (1-4) */

static bool run_loop = true;
void input_read_gamepad()
{
    SDL_Event event;
    uint8_t input_buf  = fceu_joystick;

    if (SDL_PollEvent(&event)) {
        if (event.type == SDL_KEYDOWN) {
            switch (event.key.keysym.sym) {
            case SDLK_c:
                buttons_state |= (1<<4);
                break;
            case SDLK_x:
                buttons_state |= (1<<5);
                break;
            case SDLK_UP:
                buttons_state |= (1<<2);
                break;
            case SDLK_DOWN:
                buttons_state |= (1<<3);
                break;
            case SDLK_LEFT:
                buttons_state |= (1<<0);
                break;
            case SDLK_RIGHT:
                buttons_state |= (1<<1);
                break;
            case SDLK_ESCAPE:
                run_loop = false;
                break;
            case SDLK_F3:
                SaveState("./celeste.sav");
                break;
            case SDLK_F4:
                LoadState("./celeste.sav");
                break;
            case SDLK_v:
                LoadState("./celeste.sav");
                break;
            default:
                break;
            }
        }
        if (event.type == SDL_KEYUP) {
            switch (event.key.keysym.sym) {
            case SDLK_c:
                buttons_state &= ~(1<<4);
                break;
            case SDLK_x:
                buttons_state &= ~(1<<5);
                break;
            case SDLK_UP:
                buttons_state &= ~(1<<2);
                break;
            case SDLK_DOWN:
                buttons_state &= ~(1<<3);
                break;
            case SDLK_LEFT:
                buttons_state &= ~(1<<0);
                break;
            case SDLK_RIGHT:
                buttons_state &= ~(1<<1);
                break;
            case SDLK_ESCAPE:
                run_loop = false;
                break;
            default:
                break;
            }
        }
    }

    fceu_joystick = input_buf;
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

static void ResetPalette(void) {
	memcpy(color, base_color, sizeof color);
}

static int gettileflag(int tile, int flag) {
	return tile < sizeof(tile_flags)/sizeof(*tile_flags) && (tile_flags[tile] & (1 << flag)) != 0;
}

static void p8_rectfill(int x0, int y0, int x1, int y1, int col) {
    x0 = (x0 < 0) ? 0 : (x0 >= WIDTH_P8) ? WIDTH_P8 - 1 : x0;
    y0 = (y0 < 0) ? 0 : (y0 >= HEIGHT_P8) ? HEIGHT_P8 - 1 : y0;
    x1 = (x1 < 0) ? 0 : (x1 >= WIDTH_P8) ? WIDTH_P8 - 1 : x1;
    y1 = (y1 < 0) ? 0 : (y1 >= HEIGHT_P8) ? HEIGHT_P8 - 1 : y1;

    int w = x1 - x0;
    int h = y1 - y0;

    int colorid = getcolorid(col);

    int start_x = (x0 < 0) ? 0 : x0;
    int start_y = (y0 < 0) ? 0 : y0;

    if (w > 0 && h > 0) {
        for (int i = start_y; i < y1 && i < HEIGHT_P8; i++) {
            for (int j = start_x; j < x1 && j < WIDTH_P8; j++) {
                int index = (i * PITCH_P8 + j);
                fb_celeste[index] = colorid;
            }
        }
    }
}

#define CLAMP(v,min,max) v = v < min ? min : v >= max ? max-1 : v;

void p8_line(int x0, int y0, int x1, int y1, unsigned char color) {
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

SDL_Surface* screen = NULL;
SDL_Surface* gfx = NULL;
SDL_Surface* font = NULL;

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

bool enable_screenshake = true;
int scale = 1;

int pico8emu(CELESTE_P8_CALLBACK_TYPE call, ...) {
	static int camera_x = 0, camera_y = 0;
	if (!enable_screenshake) {
		camera_x = camera_y = 0;
	}

	va_list args;
	int ret = 0;
	va_start(args, call);
	
	#define   INT_ARG() va_arg(args, int)
	#define  BOOL_ARG() (Celeste_P8_bool_t)va_arg(args, int)
	#define RET_INT(_i)   do {ret = (_i); goto end;} while (0)
	#define RET_BOOL(_b) RET_INT(!!(_b))

	switch (call) {
		case CELESTE_P8_MUSIC: { //music(idx,fade,mask)
			int index = INT_ARG();
			int fade = INT_ARG();
			int mask = INT_ARG();

			current_track.index = index;
			current_track.fade = fade;
			current_track.mask = mask;
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
				srcrc.x *= scale;
				srcrc.y *= scale;
				srcrc.w = srcrc.h = scale*8;
				SDL_Rect dstrc = {
					(x - camera_x)*scale, (y - camera_y)*scale,
					scale, scale
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
                p8_rectfill(cx-1, cy-3, cx+2, cx+4, col);
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
						srcrc.x *= scale;
						srcrc.y *= scale;
						srcrc.w = srcrc.h = scale*8;
						SDL_Rect dstrc = {
							(tx+x*8 - camera_x)*scale, (ty+y*8 - camera_y)*scale,
							scale*8, scale*8
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

void celeste_blitscreen()
{
	int offsetx = WIDTH/2-WIDTH_P8;
	int offsety = 4;
	for (int y=0; y < HEIGHT/2; y++)
	{
        for (int x = 0; x < WIDTH_P8-1; x++)
		{
            fb_data[offsetx+2*y*WIDTH+2*x] = base_palette[fb_celeste[(y+offsety)*PITCH_P8+x]];
            fb_data[offsetx+2*y*WIDTH+2*x+1] = base_palette[fb_celeste[(y+offsety)*PITCH_P8+x]];
            fb_data[offsetx+2*y*WIDTH+2*x+WIDTH] = base_palette[fb_celeste[(y+offsety)*PITCH_P8+x]];
            fb_data[offsetx+2*y*WIDTH+2*x+WIDTH+1] = base_palette[fb_celeste[(y+offsety)*PITCH_P8+x]];
        }
    }

    SDL_UpdateTexture(fb_texture, NULL, fb_data, WIDTH * BPP);
    SDL_RenderCopy(renderer, fb_texture, NULL, NULL);
    SDL_RenderPresent(renderer);
}

void FillAudioDeviceBuffer(void* UserData, Uint8* DeviceBuffer, int Length)
{
//    memset(DeviceBuffer, 0, Length);
    celeste_fill_audio_buffer(DeviceBuffer, 0, Length / 2);
}

SDL_AudioDeviceID audio_device;
Uint8 *audio_buf;
Uint32 audio_buf_len;

int init_window(int width, int height)
{
	SDL_AudioSpec want, have;

    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO) != 0)
        return 0;

    window = SDL_CreateWindow("emulator",
        SDL_WINDOWPOS_UNDEFINED, SDL_WINDOWPOS_UNDEFINED,
        width * SCALE, height * SCALE,
        0);
    if (!window)
        return 0;

    renderer = SDL_CreateRenderer(window, -1,
        SDL_RENDERER_PRESENTVSYNC);
    if (!renderer)
        return 0;

    fb_texture = SDL_CreateTexture(renderer,
        SDL_PIXELFORMAT_RGB565, SDL_TEXTUREACCESS_STREAMING,
        width, height);
    if (!fb_texture)
        return 0;

    want.freq = SAMPLE_RATE;
    want.format = AUDIO_S16;
    want.channels = 1;
    want.samples = 1024;
    want.callback = FillAudioDeviceBuffer;
    want.userdata = NULL;

    audio_device = SDL_OpenAudioDevice(NULL, 0, &want, &have, SDL_AUDIO_ALLOW_ANY_CHANGE);
    if (audio_device < 0) {
        printf("Failed to open audio device: %s\n", SDL_GetError());
        exit(1);
    }

    celeste_init_audio();

    return 1;
}

int main(int argc, char *argv[])
{
    gfx = malloc(sizeof(SDL_Surface));
    gfx->w = 128;
    gfx->pitch = 128;
    gfx->h = 64;
    gfx->pixels = gfx_data;

    font = malloc(sizeof(SDL_Surface));
    font->w = 128;
    font->pitch = 128;
    font->h = 85;
    font->pixels = font_data;

    screen = malloc(sizeof(SDL_Surface));
    screen->w = PITCH_P8;
    screen->pitch = PITCH_P8;
    screen->h = HEIGHT_P8;
    screen->clip_rect.x = 0;
    screen->clip_rect.y = 0;
    screen->clip_rect.w = WIDTH;
    screen->clip_rect.h = HEIGHT;
    screen->pixels = fb_celeste;

    init_window(WIDTH, HEIGHT);

    odroid_system_init(APP_ID, SAMPLE_RATE);
    odroid_system_emu_init(&LoadState, &SaveState, NULL, NULL, NULL, NULL, NULL);

	int pico8emu(CELESTE_P8_CALLBACK_TYPE call, ...);
	Celeste_P8_set_call_func(pico8emu);
    Celeste_P8_set_rndseed(4);
    Celeste_P8_init();

    SDL_PauseAudioDevice(audio_device, 0);

    int i = 0;
    buttons_state = 0;
    while(run_loop) {
        input_read_gamepad();
        Celeste_P8_update();
        Celeste_P8_draw();
        celeste_blitscreen();

    	static int t = 0;
        static unsigned frame_start = 0;
        unsigned frame_end = SDL_GetTicks();
        unsigned frame_time = frame_end-frame_start;
        unsigned target_millis;
        // frame timing for 30fps is 33.333... ms, but we only have integer granularity
        // so alternate between 33 and 34 ms, like [33,33,34,33,33,34,...] which averages out to 33.333...
        if (t < 2) target_millis = 33;
        else       target_millis = 34;

        if (++t == 3) t = 0;

        if (frame_time < target_millis) {
            SDL_Delay(target_millis - frame_time);
        }
        frame_start = SDL_GetTicks();

    }

    SDL_Quit();

    free(gfx);
    free(font);
    free(screen);
    return 0;
}
