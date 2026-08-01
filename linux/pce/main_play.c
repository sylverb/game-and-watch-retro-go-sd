/*
 * Linux/SDL harness for PC Engine CD — interactive play mode.
 * For the heavy diagnostic harness (SCSI trace, save/load self-tests), build
 * with: make -f Makefile.pce debug
 */
#include <odroid_system.h>

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>

#include <SDL.h>

#include "porting.h"
#include "crc32.h"

#include <gfx.h>
#include "gw_lcd.h"
#include <pce.h>
#include <romdb.h>
#include "pce_cd.h"
#include "pce_scsi.h"
#include "pce_adpcm.h"
#include "syscard_load.h"
#ifdef PCE_ENABLE_ARCADE_CARD
#include "arcade_card.h"
#endif
#include "pce_input_sdl.h"
#include "pce_audio.h"

extern int linux_savestate_req;
extern int linux_loadstate_req;

#define APP_ID 20

#define JOY_A       0x01
#define JOY_B       0x02
#define JOY_SELECT  0x04
#define JOY_RUN     0x08
#define JOY_UP      0x10
#define JOY_RIGHT   0x20
#define JOY_DOWN    0x40
#define JOY_LEFT    0x80

/* Mednafen nominal PCE viewport: 320×240 (256/342/512-wide modes are aspect-
 * corrected to 320px wide; default window scale is 3× → 960×720). */
#define WIDTH    320
#define HEIGHT   240
#define BPP      2
#define SCALE    2

typedef uint16_t pixel_t;
static uint16_t mypalette[256];
#define COLOR_RGB(r, g, b) ((((r) << 13) & 0xf800) + (((g) << 8) & 0x07e0) + (((b) << 3) & 0x001f))

#define AUDIO_SAMPLE_RATE   (48000)

static uint8_t emulator_framebuffer_pce[XBUF_WIDTH * XBUF_HEIGHT];
static int current_height = 224, current_width = 256;

#define FB_INTERNAL_OFFSET_X  (((XBUF_WIDTH - current_width) / 2) > 0 ? ((XBUF_WIDTH - current_width) / 2) : 0)
#define FB_INTERNAL_OFFSET    (((XBUF_HEIGHT - current_height) / 2 + 16) * XBUF_WIDTH + FB_INTERNAL_OFFSET_X)

static odroid_video_frame_t update1 = {WIDTH, HEIGHT, WIDTH * 2, 2, 0xFF, -1, NULL, NULL, 0, {}};
static odroid_video_frame_t update2 = {WIDTH, HEIGHT, WIDTH * 2, 2, 0xFF, -1, NULL, NULL, 0, {}};

static bool saveSRAM = false;

SDL_Window *window;
SDL_Renderer *renderer;
SDL_Texture *fb_texture;
uint16_t fb_data[WIDTH * HEIGHT * BPP];

static uint8_t PCE_EXRAM_BUF[0x8000];
static int framePerSecond = 0;
static int capTimer;

static const char *g_cue_path = NULL;
static const char *g_hucard_path = NULL;
static bool g_cd_state_loaded = false;

static bool host_LoadState(const char *savePathName);
static bool host_SaveState(const char *savePathName);

static void pce_state_path(char *out, size_t out_size)
{
	snprintf(out, out_size, "%s.state", g_cue_path);
}

static void pce_sram_path(char *out, size_t out_size)
{
	snprintf(out, out_size, "%s.sram", g_cue_path);
}

static void pce_sram_load(void)
{
	if (!g_cue_path) return;
	pce_bram_init();
	char path[1024];
	pce_sram_path(path, sizeof(path));
	FILE *f = fopen(path, "rb");
	if (f) { fread(PCE.bram, 1, 0x800, f); fclose(f); }
	pce_bram_format_if_needed();
}

static void pce_sram_save(void)
{
	if (!g_cue_path) return;
	char path[1024];
	pce_sram_path(path, sizeof(path));
	FILE *f = fopen(path, "wb");
	if (f) { fwrite(PCE.bram, 1, 0x800, f); fclose(f); }
}

static void pce_linux_save_state(void)
{
	char path[1024];
	pce_state_path(path, sizeof(path));
	if (!host_SaveState(path))
		fprintf(stderr, "PCE: save failed '%s'\n", path);
}

static void pce_linux_load_state(void)
{
	char path[1024];
	pce_state_path(path, sizeof(path));
	if (!host_LoadState(path))
		fprintf(stderr, "PCE: load failed '%s'\n", path);
}

#define SVAR_1(k, v) { 1, k, &v }
#define SVAR_2(k, v) { 2, k, &v }
#define SVAR_4(k, v) { 4, k, &v }
#define SVAR_A(k, v) { sizeof(v), k, &v }
#define SVAR_N(k, v, n) { n, k, &v }
#define SVAR_END { 0, "\0\0\0\0", 0 }

static const char SAVESTATE_HEADER[8] = "PCE_V007";
static const struct {
	size_t len;
	char key[16];
	void *ptr;
} SaveStateVars[] = {
	SVAR_A("RAM", PCE.RAM),      SVAR_A("VRAM", PCE.VRAM),  SVAR_A("SPRAM", PCE.SPRAM),
	SVAR_A("PAL", PCE.Palette),  SVAR_A("MMR", PCE.MMR),
	SVAR_2("CPU.PC", CPU_PCE.PC),    SVAR_1("CPU.A", CPU_PCE.A),    SVAR_1("CPU.X", CPU_PCE.X),
	SVAR_1("CPU.Y", CPU_PCE.Y),      SVAR_1("CPU.P", CPU_PCE.P),    SVAR_1("CPU.S", CPU_PCE.S),
	SVAR_4("Cycles", Cycles),                   SVAR_4("MaxCycles", PCE.MaxCycles),
	SVAR_1("SF2", PCE.SF2),                     SVAR_2("VBlankFL", PCE.VBlankFL),
	SVAR_1("irq_mask", CPU_PCE.irq_mask),           SVAR_1("irq_mask_delay", CPU_PCE.irq_mask_delay),
	SVAR_1("irq_lines", CPU_PCE.irq_lines),
	SVAR_1("psg.ch", PCE.PSG.ch),               SVAR_1("psg.vol", PCE.PSG.volume),
	SVAR_1("psg.lfo_f", PCE.PSG.lfo_freq),      SVAR_1("psg.lfo_c", PCE.PSG.lfo_ctrl),
	SVAR_N("psg.ch0", PCE.PSG.chan[0], 40),     SVAR_N("psg.ch1", PCE.PSG.chan[1], 40),
	SVAR_N("psg.ch2", PCE.PSG.chan[2], 40),     SVAR_N("psg.ch3", PCE.PSG.chan[3], 40),
	SVAR_N("psg.ch4", PCE.PSG.chan[4], 40),     SVAR_N("psg.ch5", PCE.PSG.chan[5], 40),
	SVAR_1("vce_cr", PCE.VCE.CR),               SVAR_1("vce_dot_clock", PCE.VCE.dot_clock),
	SVAR_A("vce_regs", PCE.VCE.regs),           SVAR_2("vce_reg", PCE.VCE.reg),
	SVAR_A("vdc_regs", PCE.VDC.regs),           SVAR_1("vdc_reg", PCE.VDC.reg),
	SVAR_1("vdc_status", PCE.VDC.status),       SVAR_1("vdc_vram", PCE.VDC.vram),
	SVAR_1("vdc_satb", PCE.VDC.satb),			SVAR_4("vdc_pen_irqs", PCE.VDC.pending_irqs),
	SVAR_1("timer_reload", PCE.Timer.reload),   SVAR_1("timer_running", PCE.Timer.running),
	SVAR_1("timer_counter", PCE.Timer.counter), SVAR_4("timer_next", PCE.Timer.cycles_counter),
	SVAR_2("timer_freq", PCE.Timer.cycles_per_line),
	SVAR_END
};

static void set_color(int index, uint8_t r, uint8_t g, uint8_t b)
{
	uint16_t col = 0xffff;
	if (index != 255)
		col = COLOR_RGB(r, g, b);
	mypalette[index] = col;
}

static void init_color_pals(void)
{
	for (int i = 0; i < 255; i++)
		set_color(i, (i & 0x1C) >> 2, (i & 0xE0) >> 5, (i & 0x03));
	set_color(255, 0x3f, 0x3f, 0x3f);
}

void odroid_display_force_refresh(void) {}

static void pce_fb_clear(void)
{
	memset(emulator_framebuffer_pce, PCE.Palette[0], sizeof(emulator_framebuffer_pce));
}

static inline void pce_norm_vdc_size(int *w, int *h)
{
	if (*w < 8) *w = 8;
	if (*w > 512) *w = 512;
	if (*h < 8) *h = 8;
	if (*h > 256) *h = 256;
}

static int s_fb_offset;

static void pce_layout_update(int w, int h)
{
	int offx = (XBUF_WIDTH - w) / 2;
	if (offx < 0) offx = 0;
	s_fb_offset = ((XBUF_HEIGHT - h) / 2 + 16) * XBUF_WIDTH + offx;
}

uint8_t *osd_gfx_framebuffer(void)
{
	return emulator_framebuffer_pce + s_fb_offset;
}

void osd_gfx_set_mode(int width, int height)
{
	pce_norm_vdc_size(&width, &height);
	if (width == current_width && height == current_height)
		return;
	pce_fb_clear();
	gfx_reset(false);
	current_width = width;
	current_height = height;
	pce_layout_update(width, height);
}

void pce_input_read(odroid_gamepad_state_t *out_state)
{
	unsigned char rc = 0;
	if (out_state->values[ODROID_INPUT_LEFT])   rc |= JOY_LEFT;
	if (out_state->values[ODROID_INPUT_RIGHT])  rc |= JOY_RIGHT;
	if (out_state->values[ODROID_INPUT_UP])     rc |= JOY_UP;
	if (out_state->values[ODROID_INPUT_DOWN])   rc |= JOY_DOWN;
	if (out_state->values[ODROID_INPUT_A])      rc |= JOY_A;
	if (out_state->values[ODROID_INPUT_B])      rc |= JOY_B;
	if (out_state->values[ODROID_INPUT_START])  rc |= JOY_RUN;
	if (out_state->values[ODROID_INPUT_SELECT]) rc |= JOY_SELECT;
	PCE.Joypad.regs[0] = rc;
}

int init_window(int width, int height)
{
	if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO) != 0)
		return -1;

	window = SDL_CreateWindow("retro-go-pce (PC Engine CD)",
	    SDL_WINDOWPOS_UNDEFINED, SDL_WINDOWPOS_UNDEFINED,
	    width * SCALE, height * SCALE, 0);
	if (!window)
		return -1;

	renderer = SDL_CreateRenderer(window, -1, SDL_RENDERER_ACCELERATED);
	if (!renderer)
		return -1;

	fb_texture = SDL_CreateTexture(renderer,
	    SDL_PIXELFORMAT_RGB565, SDL_TEXTUREACCESS_STREAMING,
	    width, height);
	if (!fb_texture)
		return -1;

	return 0;
}

#define SAVE_STATE_BUFFER_SIZE (76 * 1024)

static bool host_LoadState(const char *savePathName)
{
	char buffer[512];
	FILE *fp = fopen(savePathName, "rb");
	if (fp == NULL)
		return false;

	if (fread(buffer, 8, 1, fp) != 1 ||
	    memcmp(buffer, SAVESTATE_HEADER, 8) != 0) {
		MESSAGE_ERROR("Loading state failed: Header mismatch\n");
		fclose(fp);
		return false;
	}

	/* Device saves: header(8) + 0 + CRC(4) + SVARs + pad → 76KB, then CD RAM.
	 * Legacy linux saves: header(8) + SVARs + CD RAM (no pad, no CRC). */
	uint8_t probe = 0;
	if (fread(&probe, 1, 1, fp) != 1) {
		fclose(fp);
		return false;
	}
	const bool padded = (probe == 0);
	if (padded) {
		uint32_t saved_crc;
		if (fread(&saved_crc, sizeof(saved_crc), 1, fp) != 1) {
			fclose(fp);
			return false;
		}
		(void)saved_crc;
		/* SVARs start at byte 13 — do NOT skip to 76KB here (that was loading
		 * CD-ROM bytes into CPU/RAM and caused the post-load freeze). */
	} else {
		fseek(fp, 8, SEEK_SET);
	}

	for (int i = 0; SaveStateVars[i].len > 0; i++) {
		if (fread(SaveStateVars[i].ptr, SaveStateVars[i].len, 1, fp) != 1) {
			fclose(fp);
			return false;
		}
	}

	if (g_cue_path) {
		if (padded)
			fseek(fp, SAVE_STATE_BUFFER_SIZE, SEEK_SET);
		for (int v = 0x68; v <= 0x87; v++) {
			if (fread(PCE.MemoryMapW[v], 0x2000, 1, fp) != 1) {
				fclose(fp);
				return false;
			}
		}
		pce_scsi_reset();
		uint32_t cdda[1 + PCE_SCSI_CDDA_STATE_WORDS];
		if (fread(cdda, sizeof(cdda), 1, fp) == 1 && cdda[0] == 0x41444443u)
			pce_scsi_cdda_set(cdda + 1);
#ifdef PCE_ADPCM_STATE_WORDS
		uint32_t adpc[1 + PCE_ADPCM_STATE_WORDS];
		if (fread(adpc, sizeof(adpc), 1, fp) == 1 && adpc[0] == 0x43504441u) {
			fread(pce_adpcm_ram(), 1, 0x10000, fp);
			pce_adpcm_set(adpc + 1);
			pce_adpcm_reconcile_load();
		} else {
			pce_adpcm_reset();
		}
		{ uint32_t scsx = 0; static pce_scsi_state_t sst;
		  if (fread(&scsx, sizeof(scsx), 1, fp) == 1 && scsx == 0x58534353u &&
		      fread(&sst, sizeof(sst), 1, fp) == 1)
		      pce_scsi_state_set(&sst); }
#ifdef PCE_ENABLE_ARCADE_CARD
		{ long arc_pos = ftell(fp); uint32_t arcd = 0;
		  static pce_arcade_card_state_t acst;
		  if (fread(&arcd, sizeof(arcd), 1, fp) == 1 && arcd == PCE_ARCADE_CARD_STATE_MAGIC &&
		      fread(&acst, sizeof(acst), 1, fp) == 1) {
		      pce_arcade_card_state_set(&acst);
		      if (acst.ram_used && pce_arcade_card_ram())
		          fread(pce_arcade_card_ram(), 1, 0x200000, fp);
		  } else if (arc_pos >= 0)
		      fseek(fp, arc_pos, SEEK_SET);
		}
#endif
		pce_scsi_post_restore();
#endif
	}

	for (int i = 0; i < 8; i++)
		pce_bank_set(i, PCE.MMR[i]);

	gfx_reset(true);
	osd_gfx_set_mode(IO_VDC_SCREEN_WIDTH, IO_VDC_SCREEN_HEIGHT);
	pce_fb_clear();
	fclose(fp);

	g_cd_state_loaded = true;
	printf("Loaded state: %s\n", savePathName);
	return true;
}

static bool host_SaveState(const char *savePathName)
{
	FILE *fp = fopen(savePathName, "wb");
	if (fp == NULL)
		return false;

	fwrite(SAVESTATE_HEADER, 8, 1, fp);
	uint8_t zero = 0;
	fwrite(&zero, 1, 1, fp);
	fwrite(&PCE.ROM_CRC, sizeof(PCE.ROM_CRC), 1, fp);
	long pos = 8 + 1 + (long)sizeof(PCE.ROM_CRC);
	for (int i = 0; SaveStateVars[i].len > 0; i++) {
		fwrite(SaveStateVars[i].ptr, SaveStateVars[i].len, 1, fp);
		pos += (long)SaveStateVars[i].len;
	}
	{ static const uint8_t pad[512] = {0};
	  while (pos < SAVE_STATE_BUFFER_SIZE) {
	      long chunk = SAVE_STATE_BUFFER_SIZE - pos;
	      if (chunk > (long)sizeof(pad)) chunk = (long)sizeof(pad);
	      fwrite(pad, 1, (size_t)chunk, fp);
	      pos += chunk;
	  }
	}

	if (g_cue_path) {
		for (int v = 0x68; v <= 0x87; v++)
			fwrite(PCE.MemoryMapR[v], 0x2000, 1, fp);
		uint32_t cdda[1 + PCE_SCSI_CDDA_STATE_WORDS];
		cdda[0] = 0x41444443u;
		pce_scsi_cdda_get(cdda + 1);
		fwrite(cdda, sizeof(cdda), 1, fp);
#ifdef PCE_ADPCM_STATE_WORDS
		uint32_t adpc[1 + PCE_ADPCM_STATE_WORDS];
		adpc[0] = 0x43504441u;
		pce_adpcm_get(adpc + 1);
		fwrite(adpc, sizeof(adpc), 1, fp);
		fwrite(pce_adpcm_ram(), 1, 0x10000, fp);
		{ uint32_t scsx = 0x58534353u; static pce_scsi_state_t sst;
		  pce_scsi_state_get(&sst);
		  fwrite(&scsx, sizeof(scsx), 1, fp);
		  fwrite(&sst, sizeof(sst), 1, fp); }
#ifdef PCE_ENABLE_ARCADE_CARD
		{ uint32_t arcd = PCE_ARCADE_CARD_STATE_MAGIC;
		  static pce_arcade_card_state_t acst;
		  pce_arcade_card_state_get(&acst);
		  fwrite(&arcd, sizeof(arcd), 1, fp);
		  fwrite(&acst, sizeof(acst), 1, fp);
		  if (acst.ram_used && pce_arcade_card_ram())
		      fwrite(pce_arcade_card_ram(), 1, 0x200000, fp);
		}
#endif
#endif
	}

	fclose(fp);

	printf("Saved state: %s\n", savePathName);
	return true;
}

void pcm_submit(void) {}

size_t pce_osd_getromdata(unsigned char **data)
{
	if (g_hucard_path)
		return hucard_get_data(data);
	return syscard_get_data(data);
}

static const struct {
	const uint32_t crc;
	const char *Name;
	const uint32_t Flags;
} pceRomFlags[] = {
	{0x00000000, "Unknown", JAP},
	{0xF0ED3094, "Blazing Lazers", USA | TWO_PART_ROM},
	{0xB4A1B0F6, "Blazing Lazers", USA | TWO_PART_ROM},
	{0x55E9630D, "Legend of Hero Tonma", USA | US_ENCODED},
	{0x083C956A, "Populous", JAP | ONBOARD_RAM},
	{0x0A9ADE99, "Populous", JAP | ONBOARD_RAM},
};

int LoadCard(const char *name)
{
	(void)name;
	int offset;
	size_t rom_length = pce_osd_getromdata(&PCE.ROM);
	if (rom_length == 0) {
		fprintf(stderr, "No System Card loaded.\n");
		return 1;
	}

	offset = rom_length & 0x1fff;
	PCE.ROM_SIZE = (rom_length - offset) / 0x2000;
	PCE.ROM_DATA = PCE.ROM + offset;
	PCE.ROM_CRC = crc32_le(0, PCE.ROM, rom_length);

	uint8_t IDX = 0;
	uint8_t ROM_MASK = 1;
	while (ROM_MASK < PCE.ROM_SIZE) ROM_MASK <<= 1;
	ROM_MASK--;

	printf("System Card: %zu bytes (%d x 8KB banks)\n", rom_length, PCE.ROM_SIZE);

	const int local_rom_count = (int)(sizeof(pceRomFlags) / sizeof(pceRomFlags[0]));
	for (int index = 0; index < local_rom_count; index++) {
		if (PCE.ROM_CRC == pceRomFlags[index].crc) {
			IDX = index;
			break;
		}
	}

	if ((pceRomFlags[IDX].Flags & US_ENCODED) || PCE.ROM_DATA[0x1FFF] < 0xE0) {
		unsigned char inverted_nibble[16] = {
			0, 8, 4, 12, 2, 10, 6, 14,
			1, 9, 5, 13, 3, 11, 7, 15
		};
		for (int x = 0; x < PCE.ROM_SIZE * 0x2000; x++) {
			unsigned char temp = PCE.ROM_DATA[x] & 15;
			PCE.ROM_DATA[x] &= ~0x0F;
			PCE.ROM_DATA[x] |= inverted_nibble[PCE.ROM_DATA[x] >> 4];
			PCE.ROM_DATA[x] &= ~0xF0;
			PCE.ROM_DATA[x] |= inverted_nibble[temp] << 4;
		}
	}

	if (pceRomFlags[IDX].Flags & TWO_PART_ROM)
		PCE.ROM_SIZE = 0x30;

	for (int i = 0; i < 0x80; i++) {
		if (PCE.ROM_SIZE == 0x30) {
			switch (i & 0x70) {
			case 0x00:
			case 0x10:
			case 0x50:
				PCE.MemoryMapR[i] = PCE.ROM_DATA + (i & ROM_MASK) * 0x2000;
				break;
			case 0x20:
			case 0x60:
				PCE.MemoryMapR[i] = PCE.ROM_DATA + ((i - 0x20) & ROM_MASK) * 0x2000;
				break;
			case 0x30:
			case 0x70:
				PCE.MemoryMapR[i] = PCE.ROM_DATA + ((i - 0x10) & ROM_MASK) * 0x2000;
				break;
			case 0x40:
				PCE.MemoryMapR[i] = PCE.ROM_DATA + ((i - 0x20) & ROM_MASK) * 0x2000;
				break;
			}
		} else {
			PCE.MemoryMapR[i] = PCE.ROM_DATA + (i & ROM_MASK) * 0x2000;
		}
		PCE.MemoryMapW[i] = PCE.NULLRAM;
	}

	if (pceRomFlags[IDX].Flags & ONBOARD_RAM) {
		PCE.ExRAM = PCE.ExRAM ?: PCE_EXRAM_BUF;
		PCE.MemoryMapR[0x40] = PCE.MemoryMapW[0x40] = PCE.ExRAM;
		PCE.MemoryMapR[0x41] = PCE.MemoryMapW[0x41] = PCE.ExRAM + 0x2000;
		PCE.MemoryMapR[0x42] = PCE.MemoryMapW[0x42] = PCE.ExRAM + 0x4000;
		PCE.MemoryMapR[0x43] = PCE.MemoryMapW[0x43] = PCE.ExRAM + 0x6000;
	}

	if (PCE.ROM_SIZE >= 192)
		PCE.MemoryMapW[0x00] = PCE.IOAREA;

	return 0;
}

int InitPCE(int samplerate, bool stereo, const char *huecard)
{
	(void)samplerate;
	(void)stereo;
	(void)huecard;
	if (gfx_init())
		return 1;
	if (pce_init())
		return 1;
	if (LoadCard(NULL))
		return 1;

	if (g_cue_path) {
		static uint8_t *cdram = NULL;
		if (!cdram) cdram = (uint8_t *)malloc(0x40000);
		memset(cdram, 0, 0x40000);
		for (int v = 0x68; v <= 0x87; v++)
			PCE.MemoryMapR[v] = PCE.MemoryMapW[v] = cdram + (uint32_t)(v - 0x68) * 0x2000;
		static pce_cd_toc_t s_toc;
		if (pce_cd_parse_cue(g_cue_path, &s_toc)) {
			pce_scsi_set_disc(&s_toc, true);
			printf("CD mounted: %s (%d tracks)\n", g_cue_path, s_toc.num_tracks);
		} else {
			pce_scsi_set_disc(NULL, false);
			fprintf(stderr, "CD mount FAILED: %s\n", g_cue_path);
			return 1;
		}
		/* BRAM: per-game .sram file. */
		pce_sram_load();

#ifdef PCE_ENABLE_ARCADE_CARD
		if (!getenv("PCE_ARCADE_CARD") || strcmp(getenv("PCE_ARCADE_CARD"), "0") != 0) {
			pce_arcade_card_init();
			pce_arcade_card_map_banks();
			printf("Arcade Card enabled (2MB RAM, banks $40-$43, I/O $1A00-$1BFF)\n");
		}
#endif
	}

	gfx_reset(0);
	pce_reset(0);
	return 0;
}

static void init_emu(void)
{
	odroid_system_init(APP_ID, AUDIO_SAMPLE_RATE);
	odroid_system_emu_init(&host_LoadState, &host_SaveState, NULL, NULL, NULL, NULL, NULL);
	update1.buffer = fb_data;
	update2.buffer = fb_data;
	saveSRAM = false;

	if (InitPCE(0, false, NULL))
		exit(1);

	pce_audio_init();

	init_color_pals();
	pce_layout_update(current_width, current_height);
	memset(fb_data, 0, sizeof(fb_data));
}

void pce_osd_gfx_blit(bool drawFrame)
{
	static uint32_t lastFPSTime = 0;
	static uint32_t frames = 0;
	int y;
	uint8_t *fbTmp;

	if (!drawFrame) {
		memset(fb_data, 0, sizeof(fb_data));
		return;
	}

	uint32_t currentTime = HAL_GetTick();
	uint32_t delta = currentTime - lastFPSTime;

	frames++;
	if (delta >= 1000) {
		framePerSecond = (10000 * frames) / delta;
		printf("FPS: %d.%d\n", framePerSecond / 10, framePerSecond % 10);
		frames = 0;
		lastFPSTime = currentTime;
	}

	memset(fb_data, 0, sizeof(fb_data));

	const int disp_w = current_width;
	const int disp_h = current_height;

	int cropX = 0, cropY = 0;
	int renderWidth = disp_w;
	int renderHeight = disp_h;
	int offsetX = 0, offsetY = 0;

	if (renderWidth > WIDTH) {
		cropX = (renderWidth - WIDTH) / 2;
		renderWidth = WIDTH;
	} else {
		offsetX = (WIDTH - renderWidth) / 2;
	}
	if (renderHeight > HEIGHT) {
		cropY = (renderHeight - HEIGHT) / 2;
		renderHeight = HEIGHT;
	} else {
		offsetY = (HEIGHT - renderHeight) / 2;
	}

	uint8_t *emuFrameBuffer = osd_gfx_framebuffer();

	for (y = 0; y < renderHeight; y++) {
		fbTmp = emuFrameBuffer + ((y + cropY) * XBUF_WIDTH);
		pixel_t *dst = fb_data + (y + offsetY) * WIDTH + offsetX;
		for (int x = 0; x < renderWidth; x++)
			dst[x] = mypalette[fbTmp[x + cropX]];
	}

	SDL_UpdateTexture(fb_texture, NULL, fb_data, WIDTH * BPP);
	SDL_RenderCopy(renderer, fb_texture, NULL, NULL);
	SDL_RenderPresent(renderer);
}

static void pce_cd_autostart_input(odroid_gamepad_state_t *joy, int frame)
{
	if (getenv("PCE_NO_AUTOSTART") || !g_cue_path || g_cd_state_loaded)
		return;
	/* System Card boot menu defaults to HuCARD — select CD-ROM SYSTEM first. */
	if (frame >= 45 && frame <= 90)
		joy->values[ODROID_INPUT_DOWN] = 1;
	if (frame >= 90 && frame <= 210)
		joy->values[ODROID_INPUT_START] = 1;
}

void osd_log(int type, const char *format, ...)
{
	(void)type;
	va_list ap;
	va_start(ap, format);
	vprintf(format, ap);
	va_end(ap);
}

static int path_has_ext(const char *path, const char *ext)
{
	const char *dot = strrchr(path, '.');
	if (!dot)
		return 0;
	return strcasecmp(dot, ext) == 0;
}

static const char *find_default_syscard(void)
{
	return syscard_find_default();
}

static void usage(const char *argv0)
{
	fprintf(stderr,
	    "PC Engine — Linux/SDL test harness\n\n"
	    "Usage:\n"
	    "  %s GAME.pce                         HuCard\n"
	    "  %s [--syscard FILE.pce] GAME.cue    PCE CD\n"
	    "  %s SYSCARD.pce GAME.cue\n\n"
	    "Environment:\n"
	    "  PCE_SYSCARD       path to System Card ROM (default: ./syscard3.pce)\n"
	    "  PCE_NO_AUTOSTART  set to disable auto-press RUN at boot screen\n\n"
	    "Controls:\n"
	    "  Arrows  D-pad    X  I (A)    Z  II (B)\n"
	    "  Shift/Return/Space  RUN      Ctrl  SELECT\n"
	    "  At boot: Down selects CD-ROM SYSTEM, then Run starts the disc\n"
	    "  F2 save state    F4 load state    Esc quit\n",
	    argv0, argv0, argv0);
}

static int parse_args(int argc, char *argv[], const char **syscard, const char **cue,
                      const char **hucard)
{
	*syscard = NULL;
	*cue = NULL;
	*hucard = NULL;

	for (int i = 1; i < argc; i++) {
		if (strcmp(argv[i], "--syscard") == 0 && i + 1 < argc) {
			*syscard = argv[++i];
		} else if (strcmp(argv[i], "-h") == 0 || strcmp(argv[i], "--help") == 0) {
			usage(argv[0]);
			return 1;
		} else if (path_has_ext(argv[i], ".cue")) {
			*cue = argv[i];
		} else if (path_has_ext(argv[i], ".pce")) {
			if (*cue)
				*syscard = argv[i];
			else if (!*hucard)
				*hucard = argv[i];
			else
				*syscard = argv[i];
		} else {
			fprintf(stderr, "Unknown argument: %s\n", argv[i]);
			return -1;
		}
	}

	if (*hucard && !*cue)
		return 0;

	if (!*syscard)
		*syscard = find_default_syscard();

	if (!*syscard) {
		fprintf(stderr, "No System Card found. Pass --syscard or set PCE_SYSCARD.\n");
		usage(argv[0]);
		return -1;
	}

	if (!*cue) {
		fprintf(stderr, "No .cue file specified.\n");
		usage(argv[0]);
		return -1;
	}

	return 0;
}

int main(int argc, char *argv[])
{
	const char *syscard_path = NULL;
	const char *hucard_path = NULL;
	int pr = parse_args(argc, argv, &syscard_path, &g_cue_path, &hucard_path);
	if (pr != 0)
		return pr == 1 ? 0 : 1;

	g_hucard_path = hucard_path;

	if (hucard_path) {
		if (hucard_load_file(hucard_path))
			return 1;
	} else if (syscard_load_file(syscard_path)) {
		return 1;
	}

	if (init_window(WIDTH, HEIGHT)) {
		fprintf(stderr, "SDL init failed: %s\n", SDL_GetError());
		return 1;
	}

	init_emu();

	odroid_gamepad_state_t joystick;
	int frame = 0;

	printf("retro-go-pce play mode\n");
	if (hucard_path)
		printf("Running HuCard: %s\n", hucard_path);
	else
		printf("Running: syscard=%s  cue=%s\n", syscard_path, g_cue_path);
	printf("Boot menu: press Down then Run (Shift/Return), or wait for autostart.\n");

	{
		const char *al = getenv("GWAUTOLOAD");
		if (al && al[0] && al[0] != '0')
			linux_loadstate_req = 1;
	}

	const char *mf = getenv("PCE_MAX_FRAMES");
	const int max_frames = (mf && mf[0]) ? atoi(mf) : 0;

	while (true) {
		capTimer = SDL_GetTicks();
		bool drawFrame = true;

		pce_sdl_input_poll(&joystick);

		if (linux_quit_req)
			break;

		if (linux_savestate_req) {
			linux_savestate_req = 0;
			pce_linux_save_state();
		}
		if (linux_loadstate_req) {
			linux_loadstate_req = 0;
			pce_linux_load_state();
		}

		pce_cd_autostart_input(&joystick, frame);
		pce_input_read(&joystick);
		pce_scsi_run();

		for (PCE.Scanline = 0; PCE.Scanline < 263; ++PCE.Scanline)
			gfx_run();

		pce_osd_gfx_blit(drawFrame);
		pce_pcm_submit();
		pce_audio_pace();

		pce_adpcm_frame_end();

		PCE.Timer.cycles_counter -= Cycles;
		PCE.MaxCycles -= Cycles;
		Cycles = 0;
		frame++;
		if (max_frames > 0 && frame >= max_frames) {
			printf("PCE_MAX_FRAMES reached: PC=%04X A=%02X P=%02X scanline=%u\n",
			       CPU_PCE.PC, CPU_PCE.A, CPU_PCE.P, (unsigned)PCE.Scanline);
			break;
		}
	}

	pce_sram_save();
	SDL_Quit();
	pce_audio_shutdown();
	return 0;
}
