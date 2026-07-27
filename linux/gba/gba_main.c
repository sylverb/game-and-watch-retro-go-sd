/*
 * Linux desktop harness for gpSP / GBA (SDL2).
 *
 * Usage:
 *   make -f Makefile.gba
 *   ./build-gba/retro-go-gba.elf <rom.gba>
 *   ./build-gba/retro-go-gba.elf            (if built with an embedded ROM)
 *
 * Controls (same as other linux/ harnesses):
 *   Arrows  D-pad     X/Z  A/B     Shift/Ctrl  Start/Select
 *   Q/S     L/R       Esc  quit
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <stdint.h>

#include <SDL.h>

#include "odroid_input.h"
#include "porting.h"
#include "gba_savestate_abi.h"

#define GBA_WIDTH   240
#define GBA_HEIGHT  160
#define SCALE       3
#define GBA_FPS     59.7275
#define GBA_SAMPLE_RATE 48000
#define GBA_AUDIO_FRAMES  804   /* ~48000/59.7275 */

#define FEAT_AUTODETECT    (-1)
#define FEAT_DISABLE         0
#define SERIAL_MODE_DISABLED 0

#define GBA_KEY_A      0x0001
#define GBA_KEY_B      0x0002
#define GBA_KEY_SELECT 0x0004
#define GBA_KEY_START  0x0008
#define GBA_KEY_RIGHT  0x0010
#define GBA_KEY_LEFT   0x0020
#define GBA_KEY_UP     0x0040
#define GBA_KEY_DOWN   0x0080
#define GBA_KEY_R      0x0100
#define GBA_KEY_L      0x0200

#define GBA_STATE_MAGIC   0x41425347u  /* 'GBAS' — same as device main_gba.c */
#define GBA_STATE_VER     1

typedef struct {
    uint32_t magic;
    uint32_t version;
    uint32_t slim_len;
    uint32_t bulk_len;
} gba_state_header_t;

/* gpsp entry points / globals — declared by hand (same reason as main_gba.c). */
extern uint16_t *gba_screen_pixels;
extern uint32_t  execute_cycles;
extern uint32_t  skip_next_frame;
extern uint32_t  idle_loop_target_pc;
extern uint32_t  idle_loop_cond;
extern uint8_t   bios_rom[16 * 1024];
extern uint8_t   gamepak_backup[128 * 1024];
extern const uint8_t open_gba_bios_rom[];
extern uint32_t  reg[64];
extern uint16_t  io_registers[512];
extern uint32_t  backup_type;
extern uint32_t  backup_type_reset;
extern uint8_t   iwram[];
extern uint8_t   ewram[];

void     init_main(void);
void     init_memory(void);
void     init_sound(void);
void     init_gamepak_buffer(void);
void     reset_gba(void);
void     execute_arm(uint32_t cycles);
void     gba_set_xip_rom(uint8_t *base, uint32_t size);
void     gba_set_keys(uint32_t keys);
uint32_t load_gamepak(const void *info, const char *name, int rtc, int rumble, int serial);
uint32_t sound_read_samples(int16_t *out, uint32_t frames);

uint32_t gba_idle_loop_lookup(const char *gamepak_code);

/* libretro FF override — gpsp input.c calls this; no-op on the desktop harness. */
void set_fastforward_override(bool fastforward)
{
    (void)fastforward;
}

/* Optional embedded ROM (update_gba_rom.sh). */
extern const unsigned char gba_embedded_rom[];
extern const uint32_t gba_embedded_rom_size;
extern const char gba_embedded_rom_source[];

extern int linux_savestate_req;
extern int linux_loadstate_req;

static SDL_Window *window;
static SDL_Renderer *renderer;
static SDL_Texture *texture;
static SDL_AudioDeviceID audio_dev;

static uint16_t *framebuffer;
static uint8_t *rom_data;
static uint32_t rom_size;
static char rom_path[512];
static char state_path[540];

static int16_t audio_buf[GBA_AUDIO_FRAMES * 2];
static uint8_t slim_buf[GBA_STATE_SLIM_SIZE];

static void fatal(const char *msg)
{
    fprintf(stderr, "gba: %s\n", msg);
    exit(1);
}

static uint8_t *load_rom_file(const char *path, uint32_t *out_size)
{
    FILE *fp = fopen(path, "rb");
    if (!fp)
        return NULL;
    if (fseek(fp, 0, SEEK_END) != 0) {
        fclose(fp);
        return NULL;
    }
    long sz = ftell(fp);
    if (sz <= 0) {
        fclose(fp);
        return NULL;
    }
    rewind(fp);
    uint8_t *buf = (uint8_t *)malloc((size_t)sz);
    if (!buf) {
        fclose(fp);
        return NULL;
    }
    if (fread(buf, 1, (size_t)sz, fp) != (size_t)sz) {
        free(buf);
        fclose(fp);
        return NULL;
    }
    fclose(fp);
    *out_size = (uint32_t)sz;
    return buf;
}

static void gba_input_from_pad(const odroid_gamepad_state_t *js)
{
    uint32_t keys = 0;
    if (js->values[ODROID_INPUT_UP])     keys |= GBA_KEY_UP;
    if (js->values[ODROID_INPUT_DOWN])   keys |= GBA_KEY_DOWN;
    if (js->values[ODROID_INPUT_LEFT])   keys |= GBA_KEY_LEFT;
    if (js->values[ODROID_INPUT_RIGHT])  keys |= GBA_KEY_RIGHT;
    if (js->values[ODROID_INPUT_A])      keys |= GBA_KEY_A;
    if (js->values[ODROID_INPUT_B])      keys |= GBA_KEY_B;
    if (js->values[ODROID_INPUT_START])  keys |= GBA_KEY_START;
    if (js->values[ODROID_INPUT_SELECT]) keys |= GBA_KEY_SELECT;
    if (js->values[ODROID_INPUT_X])      keys |= GBA_KEY_L;
    if (js->values[ODROID_INPUT_Y])      keys |= GBA_KEY_R;
    gba_set_keys(keys);
}

static void present_frame(void)
{
    if (SDL_UpdateTexture(texture, NULL, framebuffer, GBA_WIDTH * 2) != 0)
        return;
    SDL_RenderClear(renderer);
    SDL_RenderCopy(renderer, texture, NULL, NULL);
    SDL_RenderPresent(renderer);
}

static void submit_audio(void)
{
    uint32_t got = sound_read_samples(audio_buf, GBA_AUDIO_FRAMES);
    /* Mono fold like the device path — SDL still wants stereo interleaved. */
    for (uint32_t i = 0; i < got; i++) {
        int32_t m = ((int32_t)audio_buf[i * 2] + (int32_t)audio_buf[i * 2 + 1]) / 2;
        audio_buf[i * 2] = (int16_t)m;
        audio_buf[i * 2 + 1] = (int16_t)m;
    }
    if (got < GBA_AUDIO_FRAMES)
        memset(audio_buf + got * 2, 0, (GBA_AUDIO_FRAMES - got) * 2 * sizeof(int16_t));
    SDL_QueueAudio(audio_dev, audio_buf, GBA_AUDIO_FRAMES * 2 * sizeof(int16_t));
}

static void apply_idle_overrides(const uint8_t *rom)
{
    uint32_t idle_pc = gba_idle_loop_lookup((const char *)&rom[0xAC]);
    if (idle_pc != 0) {
        idle_loop_target_pc = idle_pc;
        printf("gba: idle loop at 0x%08X (%.4s)\n", idle_pc, (const char *)&rom[0xAC]);
        return;
    }

    static const struct { char code[5]; uint32_t branch_pc; } vcount_polls[] = {
        { "A6SJ", 0x8932178 },
        { "ATIJ", 0x858f088 },
    };
    for (size_t i = 0; i < sizeof(vcount_polls) / sizeof(vcount_polls[0]); i++) {
        if (memcmp(vcount_polls[i].code, &rom[0xAC], 4) == 0) {
            idle_loop_target_pc = vcount_polls[i].branch_pc;
            idle_loop_cond = 1;
            printf("gba: vcount poll at 0x%08X\n", idle_loop_target_pc);
            break;
        }
    }
}

static void set_state_path_from_rom(const char *path)
{
    snprintf(state_path, sizeof(state_path), "%s", path);
    char *dot = strrchr(state_path, '.');
    char *slash = strrchr(state_path, '/');
    if (dot && (!slash || dot > slash))
        *dot = '\0';
    size_t n = strlen(state_path);
    snprintf(state_path + n, sizeof(state_path) - n, ".gbasav");
}

static bool gba_linux_save_state(void)
{
    unsigned nreg = 0;
    const gba_bulk_region_t *rg = gba_bulk_regions(&nreg);

    memset(slim_buf, 0, sizeof(slim_buf));
    gba_save_state_slim(slim_buf);

    uint32_t slim_len = *(uint32_t *)slim_buf;
    if (slim_len == 0 || slim_len > GBA_STATE_SLIM_SIZE) {
        printf("gba: save failed (bad slim_len %u)\n", slim_len);
        return false;
    }

    uint32_t bulk_len = 0;
    for (unsigned i = 0; i < nreg; i++)
        bulk_len += rg[i].len;

    FILE *f = fopen(state_path, "wb");
    if (!f) {
        printf("gba: save failed (fopen %s)\n", state_path);
        return false;
    }

    gba_state_header_t h = {GBA_STATE_MAGIC, GBA_STATE_VER, slim_len, bulk_len};
    bool ok = fwrite(&h, sizeof(h), 1, f) == 1 &&
              fwrite(slim_buf, 1, slim_len, f) == slim_len;
    for (unsigned i = 0; ok && i < nreg; i++)
        ok = fwrite(rg[i].ptr, 1, rg[i].len, f) == rg[i].len;
    fclose(f);

    printf(ok ? "gba: saved %s\n" : "gba: save failed while writing %s\n", state_path);
    return ok;
}

static bool gba_linux_load_state(void)
{
    unsigned nreg = 0;
    const gba_bulk_region_t *rg = gba_bulk_regions(&nreg);

    FILE *f = fopen(state_path, "rb");
    if (!f) {
        printf("gba: load failed (no file %s)\n", state_path);
        return false;
    }

    gba_state_header_t h;
    if (fread(&h, sizeof(h), 1, f) != 1 || h.magic != GBA_STATE_MAGIC ||
        h.version != GBA_STATE_VER || h.slim_len == 0 ||
        h.slim_len > GBA_STATE_SLIM_SIZE) {
        fclose(f);
        printf("gba: load failed (bad header)\n");
        return false;
    }

    uint32_t bulk_len = 0;
    for (unsigned i = 0; i < nreg; i++)
        bulk_len += rg[i].len;
    if (h.bulk_len != bulk_len) {
        fclose(f);
        printf("gba: load failed (bulk size mismatch)\n");
        return false;
    }

    bool ok = fread(slim_buf, 1, h.slim_len, f) == h.slim_len;
    for (unsigned i = 0; ok && i < nreg; i++)
        ok = fread(rg[i].ptr, 1, rg[i].len, f) == rg[i].len;
    fclose(f);

    if (!ok) {
        printf("gba: load failed (short read)\n");
        return false;
    }
    if (!gba_load_state_slim(slim_buf)) {
        printf("gba: load failed (slim apply)\n");
        return false;
    }

    printf("gba: loaded %s\n", state_path);
    return true;
}

int main(int argc, char **argv)
{
    const char *path = NULL;

    if (argc >= 2) {
        path = argv[1];
        rom_data = load_rom_file(path, &rom_size);
        if (!rom_data)
            fatal("could not read ROM file");
        snprintf(rom_path, sizeof(rom_path), "%s", path);
    } else if (gba_embedded_rom_size > 0) {
        rom_size = gba_embedded_rom_size;
        rom_data = (uint8_t *)malloc(rom_size);
        if (!rom_data)
            fatal("out of memory");
        memcpy(rom_data, gba_embedded_rom, rom_size);
        snprintf(rom_path, sizeof(rom_path), "%s",
                 gba_embedded_rom_source[0] ? gba_embedded_rom_source : "embedded.gba");
        printf("gba: using embedded ROM (%u bytes from %s)\n",
               rom_size, rom_path);
    } else {
        fprintf(stderr, "Usage: %s <rom.gba>\n", argv[0]);
        fprintf(stderr, "Or: ./update_gba_rom.sh <rom.gba> && rebuild\n");
        return 1;
    }

    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO | SDL_INIT_TIMER) != 0)
        fatal(SDL_GetError());

    window = SDL_CreateWindow("retro-go GBA (gpSP)",
                              SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
                              GBA_WIDTH * SCALE, GBA_HEIGHT * SCALE, 0);
    if (!window)
        fatal(SDL_GetError());
    renderer = SDL_CreateRenderer(window, -1, SDL_RENDERER_ACCELERATED);
    if (!renderer)
        fatal(SDL_GetError());
    texture = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_RGB565,
                                SDL_TEXTUREACCESS_STREAMING, GBA_WIDTH, GBA_HEIGHT);
    if (!texture)
        fatal(SDL_GetError());

    SDL_AudioSpec want = {0}, have;
    want.freq = GBA_SAMPLE_RATE;
    want.format = AUDIO_S16SYS;
    want.channels = 2;
    want.samples = 1024;
    audio_dev = SDL_OpenAudioDevice(NULL, 0, &want, &have, 0);
    if (!audio_dev) {
        fprintf(stderr, "gba: no audio (%s), continuing\n", SDL_GetError());
        audio_dev = 0;
    }
    SDL_PauseAudioDevice(audio_dev, 0);

    framebuffer = (uint16_t *)calloc(1, GBA_WIDTH * GBA_HEIGHT * 2);
    if (!framebuffer)
        fatal("out of memory (framebuffer)");
    gba_screen_pixels = framebuffer;

    init_main();
    init_memory();
    init_sound();

    {
        const char *bios_path = getenv("GBA_BIOS");
        if (bios_path && bios_path[0]) {
            FILE *bf = fopen(bios_path, "rb");
            if (!bf)
                fatal("GBA_BIOS open failed");
            if (fread(bios_rom, 1, sizeof(bios_rom), bf) != sizeof(bios_rom))
                fatal("GBA_BIOS must be exactly 16 KB");
            fclose(bf);
            printf("gba: official BIOS from %s\n", bios_path);
        } else {
            memcpy(bios_rom, open_gba_bios_rom, sizeof(bios_rom));
            printf("gba: using open/builtin BIOS (set GBA_BIOS=path for official)\n");
        }
    }
    memset(gamepak_backup, 0xFF, sizeof(gamepak_backup));

    gba_set_xip_rom(rom_data, rom_size);
    init_gamepak_buffer();

    if (load_gamepak(NULL, rom_path,
                     FEAT_AUTODETECT, FEAT_DISABLE, SERIAL_MODE_DISABLED) != 0)
        fatal("load_gamepak failed (bad header?)");
    printf("gba: backup_type=%u reset=%u (0=SRAM 1=FLASH 2=EEPROM 3=UNK)\n", (unsigned)backup_type, (unsigned)backup_type_reset);

    apply_idle_overrides(rom_data);
    reset_gba();
    set_state_path_from_rom(rom_path);

    printf("gba: %s (%u bytes) — %.4s\n",
           rom_path, rom_size, (const char *)&rom_data[0xAC]);
    printf("gba: F2 save / F4 load → %s\n", state_path);

    odroid_gamepad_state_t joystick;
    memset(&joystick, 0, sizeof(joystick));

    const double frame_ms = 1000.0 / GBA_FPS;
    uint32_t frame = 0;
    uint32_t t0 = SDL_GetTicks();
    const char *diag = getenv("GBA_DIAG");
    uint32_t diag_frames = 180;
    uint32_t sticky_nonbios = 0;

    if (diag && diag[0]) {
        if (strcmp(diag, "boot") != 0)
            gba_linux_load_state();
        diag_frames = (uint32_t)atoi(getenv("GBA_DIAG_FRAMES") ? getenv("GBA_DIAG_FRAMES") : "120");
        printf("diag: mode=%s frames=%u\n", diag, diag_frames);
    }

    while (true) {
        uint32_t frame_start = SDL_GetTicks();

        odroid_input_read_gamepad(&joystick);
        if (diag) {
            memset(&joystick, 0, sizeof(joystick));
            if (strcmp(diag, "down") == 0 && frame >= 30 && frame < 90)
                joystick.values[ODROID_INPUT_DOWN] = 1;
            else if (strcmp(diag, "a") == 0 && frame >= 30 && frame < 90)
                joystick.values[ODROID_INPUT_A] = 1;
            else if (strcmp(diag, "boot") == 0) {
                if (frame < 1000) {
                    if ((frame % 90) >= 60)
                        joystick.values[ODROID_INPUT_A] = 1;
                    if ((frame % 120) >= 100)
                        joystick.values[ODROID_INPUT_START] = 1;
                } else if (frame < 1080) {
                    joystick.values[ODROID_INPUT_DOWN] = 1;
                } else if (frame < 1200) {
                    joystick.values[ODROID_INPUT_A] = 1;
                }
            }
        }
        gba_input_from_pad(&joystick);

        if (linux_savestate_req) {
            linux_savestate_req = 0;
            gba_linux_save_state();
        }
        if (linux_loadstate_req) {
            linux_loadstate_req = 0;
            gba_linux_load_state();
        }

        skip_next_frame = 0;
        execute_arm(execute_cycles);
        if (reg[15] >= 0x4000) sticky_nonbios++;
        present_frame();
        submit_audio();

        frame++;
        {
            const char *dump = getenv("GBA_DUMP_PPM");
            if (dump && dump[0] && diag && frame == diag_frames) {
                char path[256];
                snprintf(path, sizeof(path), "%s_%u.ppm", dump, frame);
                FILE *f = fopen(path, "wb");
                if (f) {
                    fprintf(f, "P6\n%d %d\n255\n", GBA_WIDTH, GBA_HEIGHT);
                    for (int i = 0; i < GBA_WIDTH * GBA_HEIGHT; i++) {
                        uint16_t p = framebuffer[i];
                        unsigned r = ((p >> 11) & 0x1f) * 255 / 31;
                        unsigned g = ((p >> 5) & 0x3f) * 255 / 63;
                        unsigned b = (p & 0x1f) * 255 / 31;
                        fputc(r, f); fputc(g, f); fputc(b, f);
                    }
                    fclose(f);
                    fprintf(stderr, "wrote %s\n", path);
                }
            }
        }
        if (diag && (frame % 15) == 0) {

            uint32_t sum = 0;
            for (int i = 0; i < GBA_WIDTH * GBA_HEIGHT; i++)
                sum += framebuffer[i];
            uint32_t irqh = *(uint32_t *)&iwram[0x7FFC];
            fprintf(stderr,
                    "f=%u pc=%08x halt=%u cpsr=%08x r0=%x sp=%08x nonbios=%u ifbios=%04x if=%04x ie=%04x ime=%04x p1=%03x irq=%08x fb=%08x\n",
                    frame, reg[15], reg[18], reg[16], reg[0], reg[13], sticky_nonbios,
                    *(uint16_t *)&iwram[0x7FF8],
                    io_registers[0x202 / 2],
                    io_registers[0x200 / 2],
                    io_registers[0x208 / 2],
                    io_registers[0x130 / 2], irqh, sum);
        }
        if ((frame % 60) == 0) {
            uint32_t dt = SDL_GetTicks() - t0;
            if (dt > 0)
                printf("gba: ~%.1f fps\n", 60000.0 / (double)dt);
            t0 = SDL_GetTicks();
        }

        uint32_t elapsed = SDL_GetTicks() - frame_start;
        if (!diag && elapsed < (uint32_t)frame_ms)
            SDL_Delay((uint32_t)frame_ms - elapsed);
        if (diag && frame >= diag_frames)
            return 0;
    }

    return 0;
}
