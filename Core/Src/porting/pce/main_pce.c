#include <odroid_system.h>
#include <string.h>

// shared.h includes sms.h which defined CYCLES_PER_LINE.
// pce.h defines it to the desired value.
// It's a hack, but it'll do.
#undef CYCLES_PER_LINE

#include <pce.h>
#include <romdb_pce.h>
#include <assert.h>
#include <gfx.h>
#include "main.h"
#include "bilinear.h"
#include "gw_lcd.h"
#include "gw_linker.h"
#include "gw_buttons.h"
#include "rom_manager.h"
#include "common.h"
#include "sound_pce.h"
#include "pce_cd.h"
#include "pce_scsi.h"
#include "pce_adpcm.h"
#include "appid.h"
#ifndef GNW_DISABLE_COMPRESSION
#include "lzma.h"
#endif
#include "gw_malloc.h"

//#define PCE_SHOW_DEBUG
#define FPS_NTSC 60
/* PC Engine CD: the boot ROM is the (user-supplied, copyrighted) System Card.
 * Super System Card 3.0 covers base + Super CD-ROM2 titles. The dump is a raw
 * HuCard image, so .bin and .pce are interchangeable (a 512-byte header, if
 * present, is auto-stripped by LoadCartPCE via `rom_length & 0x1fff`). Try both
 * names so the user need not rename their dump. */
static const char *const PCE_SYSCARD_BIOS_PATHS[] = {
    "/bios/pce/syscard3.pce",
    "/bios/pce/syscard3.bin",
};

/* Mounted PCE-CD disc table-of-contents (kept for the SCSI target's lifetime). */
static pce_cd_toc_t s_pcecd_toc;

#define FB_INTERNAL_OFFSET_X  (((XBUF_WIDTH - current_width) / 2) > 0 ? ((XBUF_WIDTH - current_width) / 2) : 0)
#define FB_INTERNAL_OFFSET    (((XBUF_HEIGHT - current_height) / 2 + 16) * XBUF_WIDTH + FB_INTERNAL_OFFSET_X)
#define AUDIO_BUFFER_LENGTH_PCE  (PCE_SAMPLE_RATE / FPS_NTSC)
#define JOY_A       0x01
#define JOY_B       0x02
#define JOY_SELECT  0x04
#define JOY_RUN     0x08
#define JOY_UP      0x10
#define JOY_RIGHT   0x20
#define JOY_DOWN    0x40
#define JOY_LEFT    0x80

#define COLOR_RGB(r, g, b) ((((r) << 13) & 0xf800) + (((g) << 8) & 0x07e0) + (((b) << 3) & 0x001f))

static uint16_t mypalette[256];
static int current_height = 224, current_width = 256;
static short audioBuffer_pce[ AUDIO_BUFFER_LENGTH_PCE * 2];
static uint8_t pce_framebuffer[XBUF_WIDTH * XBUF_HEIGHT];
/* Borrowed for CD-RAM banks (idle on CD; Populous HuCard uses only the first 0x8000).
 * NOTE: 32KB only. Growing this to fit big Super-CD games (Dynastic Hero ~232KB needs
 * 32 banks) links OK but at runtime the extra overlay BSS starves the PCE heap and ALL
 * CD games crash -> Dynastic Hero is a genuine hard RAM limit; keep the working 200KB. */
static uint8_t PCE_EXRAM_BUF[0x8000];

// TODO: Move to lcd.c/h
extern LTDC_HandleTypeDef hltdc;
static char pce_log[100];

/**
 * Describes what is saved in a save state. Changing the order will break
 * previous saves so add a place holder if necessary. Eventually we could use
 * the keys to make order irrelevant...
 */
#define SVAR_1(k, v) { 1, k, &v }
#define SVAR_2(k, v) { 2, k, &v }
#define SVAR_4(k, v) { 4, k, &v }
#define SVAR_A(k, v) { sizeof(v), k, &v }
#define SVAR_N(k, v, n) { n, k, &v }
#define SVAR_END { 0, "\0\0\0\0", 0 }

const char SAVESTATE_HEADER[8] = "PCE_V007";
static const struct
{
	size_t len;
	char key[16];
	void *ptr;
} SaveStateVars[] =
{
	// Arrays
	SVAR_A("RAM", PCE.RAM),      SVAR_A("VRAM", PCE.VRAM),  SVAR_A("SPRAM", PCE.SPRAM),
	SVAR_A("PAL", PCE.Palette),  SVAR_A("MMR", PCE.MMR),

	// CPU registers
	SVAR_2("CPU.PC", CPU_PCE.PC),    SVAR_1("CPU.A", CPU_PCE.A),    SVAR_1("CPU.X", CPU_PCE.X),
	SVAR_1("CPU.Y", CPU_PCE.Y),      SVAR_1("CPU.P", CPU_PCE.P),    SVAR_1("CPU.S", CPU_PCE.S),

	// Misc
	SVAR_4("Cycles", Cycles),                   SVAR_4("MaxCycles", PCE.MaxCycles),
	SVAR_1("SF2", PCE.SF2),                     SVAR_2("VBlankFL", PCE.VBlankFL),

	// IRQ
	SVAR_1("irq_mask", CPU_PCE.irq_mask),           SVAR_1("irq_mask_delay", CPU_PCE.irq_mask_delay),
	SVAR_1("irq_lines", CPU_PCE.irq_lines),

	// PSG
	SVAR_1("psg.ch", PCE.PSG.ch),               SVAR_1("psg.vol", PCE.PSG.volume),
	SVAR_1("psg.lfo_f", PCE.PSG.lfo_freq),      SVAR_1("psg.lfo_c", PCE.PSG.lfo_ctrl),
	SVAR_N("psg.ch0", PCE.PSG.chan[0], 40),     SVAR_N("psg.ch1", PCE.PSG.chan[1], 40),
	SVAR_N("psg.ch2", PCE.PSG.chan[2], 40),     SVAR_N("psg.ch3", PCE.PSG.chan[3], 40),
	SVAR_N("psg.ch4", PCE.PSG.chan[4], 40),     SVAR_N("psg.ch5", PCE.PSG.chan[5], 40),

	// VCE
    SVAR_1("vce_cr", PCE.VCE.CR),               SVAR_1("vce_dot_clock", PCE.VCE.dot_clock),    
	SVAR_A("vce_regs", PCE.VCE.regs),           SVAR_2("vce_reg", PCE.VCE.reg),

	// VDC
	SVAR_A("vdc_regs", PCE.VDC.regs),           SVAR_1("vdc_reg", PCE.VDC.reg),
	SVAR_1("vdc_status", PCE.VDC.status),       SVAR_1("vdc_vram", PCE.VDC.vram),
	SVAR_1("vdc_satb", PCE.VDC.satb),			SVAR_4("vdc_pen_irqs", PCE.VDC.pending_irqs),

	// Timer
	SVAR_1("timer_reload", PCE.Timer.reload),   SVAR_1("timer_running", PCE.Timer.running),
	SVAR_1("timer_counter", PCE.Timer.counter), SVAR_4("timer_next", PCE.Timer.cycles_counter),
	SVAR_2("timer_freq", PCE.Timer.cycles_per_line),

	SVAR_END
};

/* Skip-frame render skip: when the pacer decides this frame won't be shown,
 * hand the core a NULL framebuffer — render_lines (gfx.c) early-outs and the
 * whole BG+sprite tile render (~0.7-1.3ms) is skipped, not just the blit.
 * VDC/IRQ/timing still run (gfx_run executes normally), so emulation state is
 * identical; only the pixel work is dropped. blit() deliberately does NOT use
 * this hook (direct pointer) so menu repaints can never see NULL. */
static bool s_skip_render;

/* Centre the active VDC picture inside the fixed-size core buffer. Must track
 * IO_VDC_* directly — osd_gfx_set_mode() used to run at scanline 256 (after
 * the draw), so for one frame the tile/sprite path wrote at the old FB offset
 * while gfx_screen_width() already reflected the new resolution. Ys' world-map
 * screen uses a smaller mode than field gameplay and hit this hardest. */
static inline void pce_norm_vdc_size(int *w, int *h)
{
    /* PCE VDC can legitimately run small viewports (Ys world map ≈ 128×126).
     * The old 160px floor forced 256×224 and mis-centred the blit. */
    if (*w < 8) *w = 8;
    if (*w > 512) *w = 512;
    if (*h < 8) *h = 8;
    if (*h > 256) *h = 256;
}

/* Cached layout — recomputed only in osd_gfx_set_mode() when the VDC viewport
 * size actually changes. Reading IO_VDC + division on every render_lines()
 * (via osd_gfx_framebuffer) was a steady-state CPU regression on device. */
static int s_fb_offset;

static void pce_layout_update(int w, int h)
{
    int offx = (XBUF_WIDTH - w) / 2;
    if (offx < 0) offx = 0;
    s_fb_offset = ((XBUF_HEIGHT - h) / 2 + 16) * XBUF_WIDTH + offx;
}

uint8_t *osd_gfx_framebuffer(void){
    return s_skip_render ? NULL : pce_framebuffer + s_fb_offset;
}

void set_color(int index, uint8_t r, uint8_t g, uint8_t b) {
    uint16_t col = 0xffff;
    if (index != 255)  {
        col = COLOR_RGB(r,g,b);
    }
    mypalette[index] = col;
}

void init_color_pals() {
    for (int i = 0; i < 255; i++) {
        // GGGRR RBB
          set_color(i, (i & 0x1C)>>2, (i & 0xE0) >> 5, (i & 0x03) );
    }
    set_color(255, 0x3f, 0x3f, 0x3f);
}

static void pce_fb_clear(void)
{
    memset(pce_framebuffer, PCE.Palette[0], sizeof(pce_framebuffer));
}

void osd_gfx_set_mode(int width, int height) {
    pce_norm_vdc_size(&width, &height);
    if (width == current_width && height == current_height)
        return;
    pce_fb_clear();
    gfx_reset(false);
    current_width = width;
    current_height = height;
    pce_layout_update(width, height);
}

void osd_log(int type, const char *format, ...) {
    va_list ap;
    va_start(ap, format);
    vprintf(format, ap);
    va_end(ap);
}

static void blit();

#define SAVE_STATE_BUFFER_SIZE (76*1024)
/* NO LONGER save staging — the state now streams straight to the file (below).
 * This array is purely CD RAM bank backing (banks 0x81-0x87 on device, see the
 * mapping in pce_rom_full_patch). The old design used it for BOTH: building the
 * SVAR snapshot in here DESTROYED the live content of the banks it backs, so a
 * later load left those 56KB as garbage — games that keep code/data up there
 * (Cotton) came back with the screen logic dead while CD-DA music (main-loop
 * driven) kept playing. */
uint8_t save_buffer[SAVE_STATE_BUFFER_SIZE];
#define PCE_SRAM_SIZE 0x800   /* 2KB usable BRAM (bank $F7 low mirror) */

/* Load per-game BRAM from the standard .sram save file. */
static void pce_sram_load(void)
{
    if (!ACTIVE_FILE || strcmp(ACTIVE_FILE->ext, "cue") != 0) return;
    pce_bram_init();
    char *path = odroid_system_get_path(ODROID_PATH_SAVE_SRAM, ACTIVE_FILE->path);
    FILE *f = fopen(path, "rb");
    if (f) {
        fread(PCE.bram, 1, PCE_SRAM_SIZE, f);
        fclose(f);
    }
    free(path);
    pce_bram_format_if_needed();
}

/* odroid sram_save callback: exit, sleep, app switch. */
static void pce_sram_save_cb(void)
{
    if (!ACTIVE_FILE || strcmp(ACTIVE_FILE->ext, "cue") != 0) return;
    char *path = odroid_system_get_path(ODROID_PATH_SAVE_SRAM, ACTIVE_FILE->path);
    FILE *f = fopen(path, "wb");
    if (f) {
        fwrite(PCE.bram, 1, PCE_SRAM_SIZE, f);
        fclose(f);
    }
    free(path);
    /* Must fclose CD .bin handles before sdcard_deinit()'s f_unmount.
     * Leaving them open across unmount/remount leaves FatFs flaky —
     * intermittent font fopen failures (� glyphs in pause menus) and
     * /CONFIG writes that silently fail so StartupFile isn't cleared
     * on quit → hot-boot relaunches the game. */
    pce_cd_close();
}

static bool SaveState(const char *savePathName) {
    size_t pos = 0;
    FILE *file = fopen(savePathName, "wb");
    if (file == NULL) {
        return false;
    }
    /* Same on-disk layout as the old staged writer: header + 0 + CRC + SVARs,
     * padded to SAVE_STATE_BUFFER_SIZE, then the CD blocks. */
    uint8_t zero = 0;
    pos += fwrite(SAVESTATE_HEADER, 1, sizeof(SAVESTATE_HEADER), file);
    pos += fwrite(&zero, 1, 1, file);
    pos += fwrite(&PCE.ROM_CRC, 1, sizeof(uint32_t), file);
    for (int i = 0; SaveStateVars[i].len > 0; i++) {
        pos += fwrite(SaveStateVars[i].ptr, 1, SaveStateVars[i].len, file);
    }
    assert(pos < SAVE_STATE_BUFFER_SIZE);
    size_t written = (pos > 0);
    {   /* zero-pad up to the fixed core-block size (keeps old-reader compat) */
        static const uint8_t pad[512] = {0};
        while (pos < SAVE_STATE_BUFFER_SIZE) {
            size_t chunk = SAVE_STATE_BUFFER_SIZE - pos;
            if (chunk > sizeof(pad)) chunk = sizeof(pad);
            pos += fwrite(pad, 1, chunk, file);
        }
    }
    /* PCE-CD: the 256KB CD RAM (banks 0x68-0x87) holds the game's loaded code +
     * data and is far bigger than save_buffer, so stream it to the file right
     * after the core state. (HuCard .pce saves are unchanged.) */
    if (written && strcmp(ACTIVE_FILE->ext, "cue") == 0) {
        for (int v = PCE_CD_RAM_FIRST_BANK; v <= PCE_CD_RAM_LAST_BANK; v++)
            fwrite(PCE.MemoryMapR[v], PCE_CD_RAM_BANK_SIZE, 1, file);
        /* CD-DA stream snapshot (magic + 5 words) so a resume re-arms the BGM. */
        uint32_t cdda[1 + PCE_SCSI_CDDA_STATE_WORDS] = { 0x41444443u /* 'CDDA' */ };
        pce_scsi_cdda_get(cdda + 1);
        fwrite(cdda, sizeof(cdda), 1, file);
        /* ADPCM engine + 64KB sample RAM: the game-side audio sequencer lives in
         * game RAM (restored above) and references segments in ADPCM RAM by
         * address — without this block a load desyncs them and the sequencer
         * hangs waiting for a segment that never ends (~2s after load). */
        uint32_t adpc[1 + PCE_ADPCM_STATE_WORDS] = { 0x43504441u /* 'ADPC' */ };
        pce_adpcm_get(adpc + 1);
        fwrite(adpc, sizeof(adpc), 1, file);
        fwrite(pce_adpcm_ram(), 1, 0x10000, file);
        /* Full SCSI engine (in-flight transfer!): games like Cotton save DURING
         * a CD read — without this block a load leaves them polling $1802/$1803
         * forever for a reply the old reset threw away. */
        uint32_t scsx = 0x58534353u; /* 'SCSX' */
        static pce_scsi_state_t sst;
        pce_scsi_state_get(&sst);
        fwrite(&scsx, sizeof(scsx), 1, file);
        fwrite(&sst, sizeof(sst), 1, file);
    }
    fclose(file);
    if (!written) {
        return false;
    }
    sprintf(pce_log,"%08lX",PCE.ROM_CRC);
    return true;
}

/* CD autostart = RUN injection for the "PUSH RUN BUTTON" boot screen. It must
 * NOT fire into a restored game: to a running game RUN is its own PAUSE, so a
 * power-on resume "froze" exactly 1s in (frame 60 = injection start) — the game
 * sat in its pause loop (SUBQ+DA issued, music held) looking dead. Set on any
 * successful state load; a failed/CRC-mismatched load keeps the boot injection. */
static bool s_cd_state_loaded;

static bool LoadState(const char *savePathName) {
    /* Streams the state straight from the file into the live structures —
     * NEVER through save_buffer, which is CD RAM bank backing (0x81-0x87 on
     * device): the old staged reader overwrote those banks' live content with
     * file bytes it was still parsing. Layout is unchanged, old saves load. */
    FILE *file = fopen(savePathName, "rb");
    if (file == NULL) {
        return false;
    }

    uint8_t head[sizeof(SAVESTATE_HEADER) + 1];
    uint32_t saved_crc = 0;
    if (fread(head, 1, sizeof(head), file) != sizeof(head) ||
        fread(&saved_crc, 1, sizeof(saved_crc), file) != sizeof(saved_crc)) {
        fclose(file);
        return false;
    }
    sprintf(pce_log,"%08lX",saved_crc);
    if (saved_crc != PCE.ROM_CRC) {
        fclose(file);
        return true;
    }
    for (int i = 0; SaveStateVars[i].len > 0; i++) {
        printf("Loading %s (%d)\n", SaveStateVars[i].key, (int)SaveStateVars[i].len);
        fread(SaveStateVars[i].ptr, 1, SaveStateVars[i].len, file);
    }
    /* PCE-CD: restore the 256KB CD RAM streamed after the fixed-size core
     * block, then reset the SCSI to idle (the disc stays mounted from launch;
     * saves are taken with no transfer in flight). */
    if (strcmp(ACTIVE_FILE->ext, "cue") == 0) {
        fseek(file, SAVE_STATE_BUFFER_SIZE, SEEK_SET);
        for (int v = PCE_CD_RAM_FIRST_BANK; v <= PCE_CD_RAM_LAST_BANK; v++)
            fread(PCE.MemoryMapW[v], PCE_CD_RAM_BANK_SIZE, 1, file);
        pce_scsi_reset();
        /* Restore the CD-DA stream the reset just killed (block absent in old
         * saves -> fread short-reads -> keep the pre-fix silent-resume behaviour). */
        uint32_t cdda[1 + PCE_SCSI_CDDA_STATE_WORDS];
        if (fread(cdda, sizeof(cdda), 1, file) == 1 && cdda[0] == 0x41444443u)
            pce_scsi_cdda_set(cdda + 1);
        /* ADPCM engine + RAM (see SaveState). Old saves lack the block: reset the
         * engine so a stale in-session state can't claim "still playing" against
         * RAM it no longer matches (the load-then-hang failure). */
        uint32_t adpc[1 + PCE_ADPCM_STATE_WORDS];
        if (fread(adpc, sizeof(adpc), 1, file) == 1 && adpc[0] == 0x43504441u) {
            fread(pce_adpcm_ram(), 1, 0x10000, file);
            pce_adpcm_set(adpc + 1);
            pce_adpcm_reconcile_load();
        } else {
            pce_adpcm_reset();
        }
        /* SCSI engine block: restores an in-flight transfer over the reset above
         * (old saves without it keep the reset-to-idle behaviour). */
        uint32_t scsx = 0;
        static pce_scsi_state_t sst;
        if (fread(&scsx, sizeof(scsx), 1, file) == 1 && scsx == 0x58534353u &&
            fread(&sst, sizeof(sst), 1, file) == 1) {
            pce_scsi_state_set(&sst);
        }
        pce_scsi_post_restore();
    }
    fclose(file);

    for(int i = 0; i < 8; i++) {
        pce_bank_set(i, PCE.MMR[i]);
    }
    gfx_reset(true);
    osd_gfx_set_mode(IO_VDC_SCREEN_WIDTH, IO_VDC_SCREEN_HEIGHT);
    pce_fb_clear();   /* drop pixels from the pre-load screen / old FB offset */
    common_emu_state.skip_frames = 0;
    s_cd_state_loaded = true;   /* suppress the boot RUN injection (see decl) */
    return true;
}

static void *Screenshot()
{
    lcd_wait_for_vblank();

    lcd_clear_active_buffer();
    blit();
    return lcd_get_active_buffer();
}

static void pce_rom_full_patch()
{
    for (int i = 0; i < PCE.rp_count; i++)
    {
        uint8_t p_count = PCE.patchs[i][0];
        uint8_t p_start = 1;
        for (int j = 0; j < p_count; j++)
        {
            uint32_t addr = (PCE.patchs[i][p_start] & 0xf) * 0x10000 + PCE.patchs[i][p_start + 1] * 0x100 + PCE.patchs[i][p_start + 2];
            uint8_t count = (PCE.patchs[i][p_start] >> 4) + 1;
            for (int x = 0; x < count; x++)
            {
                // all address are seek from rom_data
                //uint8_t val = PCE.ROM_DATA[addr + x];
                PCE.ROM_DATA[addr + x] = PCE.patchs[i][p_start + 3 + x];
                //printf("Patched at RAM: %p from val: %2x To: %2x \n", (unsigned char *)(addr + x), val, PCE.patchs[i][p_start + 3 + x]);
            }
            p_start = p_start + count + 3; 
        }
    }
}


static void pce_rom_patch()
{
    unsigned char *dest = (unsigned char *)&_PCE_ROM_UNPACK_BUFFER;
    uint32_t available_size = (uint32_t)&_PCE_ROM_UNPACK_BUFFER_SIZE;

    uint8_t *DynMEM[16]; //max 16*16=256k;  single bank is 8k but here must two bank batch move
    uint8_t DynCount = 0;
    uint8_t CurIdx = 0;
    uint16_t MaxCount =  available_size / 0x4000;
    MaxCount = (MaxCount > 16) ? 16 : MaxCount;

    for (int i = 0; i < PCE.rp_count; i++)
    {
        uint8_t p_count = PCE.patchs[i][0];
        uint8_t p_start = 1;
        for (int j = 0; j < p_count; j++)
        {
            uint32_t addr = (PCE.patchs[i][p_start] & 0xf) * 0x10000 + PCE.patchs[i][p_start + 1] * 0x100 + PCE.patchs[i][p_start + 2];
            uint32_t s_addr = addr / 0x4000 * 0x4000;
            uint32_t x_addr = addr & 0x3fff;
            //found here moved?
            CurIdx = 0xFF;
            for (int k=0; k<DynCount && k<MaxCount; k++)
            {
                if (DynMEM[k] == (unsigned char *)s_addr) 
                {
                    CurIdx = k;
                    break;
                } 
            }

            if (CurIdx == 0xFF) 
            {
                //New Item;
                DynCount += 1;
                if (DynCount > MaxCount)
                    return;
                DynMEM[DynCount - 1] = (unsigned char *)s_addr;
                //Set Index and copy data;
                CurIdx = DynCount - 1;
                uint8_t *d_addr = dest + CurIdx * 0x4000;

                for (int k=0; k<0x4000; k++)
                    dest[CurIdx * 0x4000 + k] = PCE.ROM_DATA[s_addr + k];

                for (int k = 0; k < 0x80; k++) 
                {
                    if (PCE.MemoryMapR[k] == (PCE.ROM_DATA + s_addr))
                    {
                        PCE.MemoryMapR[k] = d_addr;
                    }
                    else if (PCE.MemoryMapR[k] == (PCE.ROM_DATA + s_addr + 0x2000))
                    {
                        PCE.MemoryMapR[k] = d_addr + 0x2000;
                    }
                }
            }

            uint8_t count = (PCE.patchs[i][p_start] >> 4) + 1;
            for (int x = 0; x < count; x++)
            {
                // all address are seek from rom_data
                //uint8_t val = dest[CurIdx * 0x4000 + x_addr + x];
                dest[CurIdx * 0x4000 + x_addr + x] = PCE.patchs[i][p_start + 3 + x];
                //printf("Patched %p at RAM: %p from val: %2x To: %2x \n", (unsigned char *)(addr + x), &(dest[CurIdx * 0x4000 + x_addr + x]), val, PCE.patchs[i][p_start + 3 + x]);
            }
            p_start = p_start + count + 3; 
        }
    }
}

size_t
pce_osd_getromdata(unsigned char **data)
{
    /* src pointer to the ROM data in the external flash (raw or LZ4) */
#ifndef GNW_DISABLE_COMPRESSION
#if SD_CARD == 1
#error "Roms compression is not supported on SD Card"
#else
    unsigned char *dest = (unsigned char *)&_PCE_ROM_UNPACK_BUFFER;
    uint32_t available_size = (uint32_t)&_PCE_ROM_UNPACK_BUFFER_SIZE;
    uint32_t src_size = 0;
    const unsigned char *src = odroid_overlay_cache_file_in_flash(ACTIVE_FILE->path, &src_size, false);
    if (src == NULL || src_size == 0) {
        *data = NULL;
        return 0;
    }
    if(strcmp(ACTIVE_FILE->ext, "lzma") == 0){
        size_t n_decomp_bytes;
        n_decomp_bytes = lzma_inflate(dest, available_size, src, src_size);
        *data = dest;
        return n_decomp_bytes;
    }
    else
#endif
#endif
    ram_start = (uint32_t)&_OVERLAY_PCE_BSS_END;
    if (strcmp(ACTIVE_FILE->ext, "cue") == 0) {
        /* PCE-CD: the "ROM" is the System Card BIOS (mapped at bank 0); the disc
         * image itself is streamed from SD separately. XIP it from flash like a
         * HuCard. Phase 1 boots the CD-ROM2 menu to validate the core path. */
        uint32_t bios_size = 0;
        *data = NULL;
        for (size_t i = 0; i < sizeof(PCE_SYSCARD_BIOS_PATHS) / sizeof(PCE_SYSCARD_BIOS_PATHS[0]); i++) {
            *data = (unsigned char *)odroid_overlay_cache_file_in_flash(PCE_SYSCARD_BIOS_PATHS[i], &bios_size, false);
            if (*data != NULL && bios_size > 0)
                break;
        }
        /* Mount the disc so the System Card's SCSI reads ($1800) hit the CUE/BIN. */
        pce_cd_close();   /* drop any stale cached .bin handle from a prior launch */
        if (pce_cd_parse_cue(ACTIVE_FILE->path, &s_pcecd_toc))
            pce_scsi_set_disc(&s_pcecd_toc, true);
        else
            pce_scsi_set_disc(NULL, false);
        return (*data != NULL && bios_size > 0) ? bios_size : 0;
    }
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
}

void LoadCartPCE() {
    int offset;
    size_t rom_length = pce_osd_getromdata(&PCE.ROM);
    if (rom_length == 0 || PCE.ROM == NULL) {
        return;
    }
    offset = rom_length & 0x1fff;
    PCE.ROM_SIZE = (rom_length - offset) / 0x2000;
    PCE.ROM_DATA = PCE.ROM + offset;
    if (PCE.ROM_SIZE < 192) {
        PCE.ROM_CRC = crc32_le(0, PCE.ROM, rom_length);
    } else {
        PCE.ROM_CRC = crc32_le(0, PCE.ROM, 4096);
    }
    //   PCE.ROM_CRC = crc32_le(0, PCE.ROM, rom_length);

       uint IDX = 0;
       uint ROM_MASK = 1;

       while (ROM_MASK < PCE.ROM_SIZE) ROM_MASK <<= 1;
       ROM_MASK--;

#ifdef PCE_SHOW_DEBUG
       printf("Rom Size: %d, B1:%X, B2:%X, B3:%X, B4:%X" , rom_length, PCE.ROM[0], PCE.ROM[1],PCE.ROM[2],PCE.ROM[3]);
#endif

       for (int index = 0; index < KNOWN_ROM_COUNT; index++) {
           if (PCE.ROM_CRC == pceRomFlags[index].crc) {
               IDX = index;
               break;
           }
       }

       printf("Game Name: %s\n", pceRomFlags[IDX].Name);
       printf("Game Region: %s\n", (pceRomFlags[IDX].Flags & JAP) ? "Japan" : "USA");

       // US Encrypted
    if ((pceRomFlags[IDX].Flags & US_ENCODED) || PCE.ROM_DATA[0x1FFF] < 0xE0) {
		printf("US Encrypted rom code");
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

	// For example with Devil Crush 512Ko
    if (pceRomFlags[IDX].Flags & TWO_PART_ROM) 
        PCE.ROM_SIZE = 0x30;

    // Game ROM
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

    // Allocate the card's onboard ram
    if (pceRomFlags[IDX].Flags & ONBOARD_RAM) {
        PCE.ExRAM = PCE.ExRAM ?: PCE_EXRAM_BUF;
        PCE.MemoryMapR[0x40] = PCE.MemoryMapW[0x40] = PCE.ExRAM;
        PCE.MemoryMapR[0x41] = PCE.MemoryMapW[0x41] = PCE.ExRAM + 0x2000;
        PCE.MemoryMapR[0x42] = PCE.MemoryMapW[0x42] = PCE.ExRAM + 0x4000;
        PCE.MemoryMapR[0x43] = PCE.MemoryMapW[0x43] = PCE.ExRAM + 0x6000;
    }

    // Mapper for roms >= 1.5MB (SF2, homebrews)
    if (PCE.ROM_SIZE >= 192)
        PCE.MemoryMapW[0x00] = PCE.IOAREA;

    //pce_rom_patch
    unsigned char *dest = (unsigned char *)&_PCE_ROM_UNPACK_BUFFER;
    printf("Rom: %p %p \n", PCE.ROM, dest);

    if (PCE.ROM != dest)
        pce_rom_patch();
    else
        pce_rom_full_patch();

    /* PCE-CD: back the CD-ROM2 program-RAM banks with real RAM. The System Card
     * is XIP'd from flash (pce_osd_getromdata), so the entire ROM-unpack buffer
     * is free to host the CD RAM. Banks 0x68-0x87 are contiguous: 0x80-0x87 is
     * the 64KB base RAM, 0x68-0x7F adds the 192KB Super CD RAM (256KB total).
     * Without this every CD bank aliases the shared 8KB NULLRAM and a loaded
     * game overwrites itself, then traps. Done last so it overrides the
     * System-Card ROM mirror that the bank loop left on 0x68-0x7F. */
    if (strcmp(ACTIVE_FILE->ext, "cue") == 0) {
        uint8_t  *buf  = (uint8_t *)&_PCE_ROM_UNPACK_BUFFER;
        uint32_t  room = (uint32_t)&_PCE_ROM_UNPACK_BUFFER_SIZE;
        int total_banks = PCE_CD_RAM_LAST_BANK - PCE_CD_RAM_FIRST_BANK + 1;  /* 32 = 256KB */
        int buf_banks   = (int)(room / PCE_CD_RAM_BANK_SIZE);
        if (buf_banks > total_banks) buf_banks = total_banks;
        /* The unpack buffer is ~10KB shy of the full 256KB, so borrow the few
         * leftover banks from PCE_EXRAM_BUF — the Populous on-board RAM, which
         * is idle for CD (HuCard and CD never load together). Banks are mapped
         * independently via MemoryMap, so the two pools needn't be contiguous. */
        int exram_banks = (int)(sizeof(PCE_EXRAM_BUF) / PCE_CD_RAM_BANK_SIZE);
        int from_exram  = total_banks - buf_banks;
        if (from_exram < 0) from_exram = 0;
        if (from_exram > exram_banks) from_exram = exram_banks;
        int mapped = buf_banks + from_exram;
        if (mapped > total_banks) mapped = total_banks;
        for (int i = 0; i < mapped; i++) {
            int v = PCE_CD_RAM_FIRST_BANK + i;
            uint8_t *p = (i < from_exram)
                       ? PCE_EXRAM_BUF + (uint32_t)i * PCE_CD_RAM_BANK_SIZE
                       : buf + (uint32_t)(i - from_exram) * PCE_CD_RAM_BANK_SIZE;
            PCE.MemoryMapR[v] = p;
            PCE.MemoryMapW[v] = p;
        }
        if (from_exram) memset(PCE_EXRAM_BUF, 0, (uint32_t)from_exram * PCE_CD_RAM_BANK_SIZE);
        if (buf_banks > 0) {
            uint32_t clear_sz = (uint32_t)buf_banks * PCE_CD_RAM_BANK_SIZE;
            if (clear_sz > room)
                clear_sz = room;
            memset(buf, 0, clear_sz);
        }
        /* STILL short of the full 32 banks (unpack ~174KB=21 + EXRAM 32KB=4 = 25 on
         * device, so 7 banks/56KB missing)? Borrow the save-state staging buffer
         * (save_buffer, SAVE_STATE_BUFFER_SIZE ~78KB). It is touched ONLY during
         * Save/Load — idle during play — and it lives OUTSIDE the unpack buffer, so
         * borrowing it is a NET GAIN (unlike enlarging a static buffer, which would
         * just shrink the unpack pool by the same amount). This is what lets a full
         * 256KB Super CD-ROM² game (Dynastic Hero) map all 32 banks and BOOT on the
         * device — proven necessary: host maps 32/32 and boots it, device mapped 25/32
         * and hung in the System-Card IPL. Caveat: a state Save of a game that actually
         * uses banks 25-31 will clobber them (save_buffer doubles as save staging);
         * booting is the priority and small CD games never touch those banks. */
        int save_banks_cap = (int)(sizeof(save_buffer) / PCE_CD_RAM_BANK_SIZE);
        int from_save = total_banks - mapped;
        if (from_save < 0) from_save = 0;
        if (from_save > save_banks_cap) from_save = save_banks_cap;
        for (int k = 0; k < from_save; k++) {
            int v = PCE_CD_RAM_FIRST_BANK + mapped + k;
            uint8_t *p = save_buffer + (uint32_t)k * PCE_CD_RAM_BANK_SIZE;
            PCE.MemoryMapR[v] = p;
            PCE.MemoryMapW[v] = p;
        }
        if (from_save) memset(save_buffer, 0, (uint32_t)from_save * PCE_CD_RAM_BANK_SIZE);
        mapped += from_save;
#ifdef LINUX_EMU   /* host only: on-device this fopen + the open .bin = 1-file-limit corruption */
        FILE *cf = fopen("pcecd_diag.txt", "a");
        if (cf) {
            fprintf(cf, "CDRAM map: room=%lu buf_banks=%d exram=%d save=%d mapped=%d/%d %s\n",
                    (unsigned long)room, buf_banks, from_exram, from_save, mapped, total_banks,
                    (mapped >= total_banks) ? "FULL" : "PARTIAL");
            fclose(cf);
        }
#endif
        /* BRAM: per-game .sram file (managed by the launcher like any SRAM). */
        pce_sram_load();
    }
}

void ResetPCE(bool hard) {
    gfx_reset(hard);
    pce_reset(hard);

}


void pce_input_read(odroid_gamepad_state_t* out_state) {
    unsigned char rc = 0;
    if (out_state->values[ODROID_INPUT_LEFT])   rc |= JOY_LEFT;
    if (out_state->values[ODROID_INPUT_RIGHT])  rc |= JOY_RIGHT;
    if (out_state->values[ODROID_INPUT_UP])     rc |= JOY_UP;
    if (out_state->values[ODROID_INPUT_DOWN])   rc |= JOY_DOWN;
    if (out_state->values[ODROID_INPUT_A])      rc |= JOY_A;
    if (out_state->values[ODROID_INPUT_B])      rc |= JOY_B;
    if ((out_state->values[ODROID_INPUT_START]) || (out_state->values[ODROID_INPUT_X]))  rc |= JOY_RUN;
    if ((out_state->values[ODROID_INPUT_SELECT]) || (out_state->values[ODROID_INPUT_Y])) rc |= JOY_SELECT;
    PCE.Joypad.regs[0] = rc;
}

static void blit() {
    odroid_display_scaling_t scaling = odroid_display_get_scaling_mode();
    const int disp_w = current_width;
    const int disp_h = current_height;

    /* Direct pointer, NOT osd_gfx_framebuffer(): that hook returns NULL on
     * skip frames and blit can be called as the menu repaint callback. */
    uint8_t *emuFrameBuffer = pce_framebuffer + s_fb_offset;
    pixel_t *framebuffer_active = lcd_get_active_buffer();
    int y=0, offsetY, offsetX = 0, cropX = 0;
    int xScale = 0;
    uint8_t *fbTmp;

    if (scaling == ODROID_DISPLAY_SCALING_FULL) {
        /* Stretch BOTH axes to fill the screen, like the other cores' FULL mode.
         * Without the Y stretch, 224-line games (most CD titles) leave 8px black
         * bars top and bottom. Nearest-neighbour: 224->240 repeats 1 row in 15. */
        xScale = (disp_w << 8) / GW_LCD_WIDTH;
        int yScale = (disp_h << 8) / GW_LCD_HEIGHT;
        for (y = 0; y < GW_LCD_HEIGHT; y++) {
            fbTmp = emuFrameBuffer + ((y * yScale) >> 8) * XBUF_WIDTH;
            pixel_t *dst = framebuffer_active + y * GW_LCD_WIDTH;
            for (int x = 0; x < GW_LCD_WIDTH; x++) {
                dst[x] = mypalette[fbTmp[(x * xScale) >> 8]];
            }
        }
        return;
    }

    static int s_blit_w, s_blit_h;
    bool vdc_res_changed = (disp_w != s_blit_w || disp_h != s_blit_h);
    s_blit_w = disp_w;
    s_blit_h = disp_h;

    if (scaling != ODROID_DISPLAY_SCALING_OFF ) {
        xScale = (disp_w << 8) / GW_LCD_WIDTH ;
    } else if ( disp_w < GW_LCD_WIDTH) {
        offsetX = (GW_LCD_WIDTH - disp_w)/2;
    } else if ( disp_w > GW_LCD_WIDTH) {
        cropX = (disp_w - GW_LCD_WIDTH)/2;
    }

    int renderHeight = (disp_h<=GW_LCD_HEIGHT)?disp_h:GW_LCD_HEIGHT;
    int renderWidth = (disp_w<=GW_LCD_WIDTH)?disp_w:GW_LCD_WIDTH;
    int offY = (GW_LCD_HEIGHT - renderHeight) / 2;
    if (offY < 0) offY = 0;

    if (vdc_res_changed) {
        /* Once per VDC mode change: drop stale pixels after offset/crop shift.
         * Doing this every frame cost ~150KB memset @ 60fps and spiked CPU. */
        memset(framebuffer_active, 0,
               (size_t)GW_LCD_WIDTH * GW_LCD_HEIGHT * sizeof(pixel_t));
    } else {
        memset(framebuffer_active, 0,
               (size_t)offY * GW_LCD_WIDTH * sizeof(pixel_t));
    }

    for(y=0;y<renderHeight;y++) {
        fbTmp = emuFrameBuffer+(y*XBUF_WIDTH);
        offsetY = (y + offY)*GW_LCD_WIDTH;
        if (xScale) {
            for(int x=0;x<GW_LCD_WIDTH;x++) {
                framebuffer_active[offsetY+x]= mypalette[fbTmp[ (x * xScale) >> 8 ]];
            }
        } else {
            for(int x=0;x<renderWidth;x++) {
                   framebuffer_active[offsetY+x+offsetX]=mypalette[fbTmp[x+cropX]];
            }
        }
    }
    if (!vdc_res_changed) {
        for(y = offY + renderHeight; y < GW_LCD_HEIGHT; y++) {
            offsetY = y*GW_LCD_WIDTH;
            for(int x=0;x<GW_LCD_WIDTH;x++)
                framebuffer_active[offsetY+x]=0;
        }
    }
}

void pce_osd_gfx_blit() {
#ifdef PCE_SHOW_DEBUG
    static uint32_t frames = 0;
    static uint32_t lastFPSTime = 0;
    static int framePerSecond=0;
    uint32_t currentTime = HAL_GetTick();
    uint32_t delta = currentTime - lastFPSTime;
    frames++;
    if (delta >= 1000) {
        framePerSecond = (10000 * frames) / delta;
        //printf("FPS: %d.%d, frames %ld, delta %ld ms\n", framePerSecond / 10, framePerSecond % 10, frames, delta);
        frames = 0;
        lastFPSTime = currentTime;
    }
#endif

    blit();

    common_ingame_overlay();

#ifdef PCE_SHOW_DEBUG
    char debugMsg[200];
    sprintf(debugMsg,"FPS:%d.%d,W:%d,H:%d,L:%s", framePerSecond / 10,framePerSecond % 10,current_width,current_height,pce_log);
    odroid_overlay_draw_text(0,0, GW_LCD_WIDTH, debugMsg, curr_colors->sel_c, curr_colors->main_c);
#endif
    lcd_swap();
}

/* common_emu_sound_sync(false) + CD-DA prefetch. The pacer wait (until the
 * SAI DMA frees the next audio slot) is dead CPU time; spend it pulling the
 * NEXT sectors of CD-DA from SD so slow frames find the FIFO full and their
 * pce_scsi_cdda_fill() pays no SD read. One small read per iteration, and
 * dma_counter is re-checked between reads, so the hand-off to the pacer stays
 * prompt. Uses common_emu_sound_dma_marker so pause/resume reset stays in sync
 * with common_emu_sound_sync. */
static void pce_sound_sync_with_prefetch(void)
{
    if (common_emu_state.skip_frames)
        return;                     /* running behind: no wait, no extra SD work */
    if (common_emu_sound_dma_marker == 0)
        common_emu_sound_dma_marker = dma_counter;
    for (uint8_t p = 0; p < common_emu_state.pause_frames + 1; p++) {
        while (dma_counter == common_emu_sound_dma_marker) {
            if (!pce_scsi_cdda_prefetch())
                cpumon_sleep();     /* FIFO full / no BGM: plain WFI as before */
        }
        common_emu_sound_dma_marker = dma_counter;
    }
}

void pce_pcm_submit() {
    pce_snd_update(audioBuffer_pce, AUDIO_BUFFER_LENGTH_PCE);

    if (common_emu_sound_loop_is_muted()) {
        return;
    }

    int32_t factor = common_emu_sound_get_volume() / 2; // Divide by 2 to prevent overflow in stereo mixing
    int16_t* sound_buffer = audio_get_active_buffer();
    uint16_t sound_buffer_length = audio_get_buffer_length();

    /* CD-DA (Red Book audio / BGM) + ADPCM (voice): pull this frame's samples and mix
     * with the PSG. CD-DA is now ON for device too. The old thrash that forced it off
     * was fopen/fclose-per-sector on a SINGLE shared .bin handle (a FatFs dir walk 60x/s
     * while the SCSI engine read the data track) — that is FIXED in pce_cd.c: the CD-DA
     * stream uses its OWN persistent handle (s_bin_f[1], slot 1) so it never thrashes the
     * data handle (slot 0). Reads are now just fseek+fread on an open file (~75 sectors/s).
     * The CD-DA decode is verified on the host harness (Dynastic Hero opening = 17s of real
     * stereo BGM, cdda.pcm 97% non-zero). ADPCM samples are already resident in ADPCM RAM
     * (adpcm_dma_drain), so pce_adpcm_fill is pure in-RAM decode. Both channels on. */
    static const int s_pcecd_cd_audio = 1;
    static int16_t cdda_buf[AUDIO_BUFFER_LENGTH_PCE * 2];
    static int16_t adpcm_buf[AUDIO_BUFFER_LENGTH_PCE * 2];
    int cdda_n  = s_pcecd_cd_audio ? pce_scsi_cdda_fill(cdda_buf, AUDIO_BUFFER_LENGTH_PCE) : 0;
    int adpcm_n = pce_adpcm_fill(adpcm_buf, AUDIO_BUFFER_LENGTH_PCE);   /* in-RAM, cheap: always on */

    uint32_t adpcm_vol = pce_scsi_adpcm_volume();  /* Q16 fader volume for ADPCM */

    for (int i = 0; i < sound_buffer_length; i++) {
        /* mix left & right */
        int32_t sample = (audioBuffer_pce[i*2] + audioBuffer_pce[i*2+1]);
        if (cdda_n && i < cdda_n) {
            /* stereo sum (= ((L+R)>>1)<<1); CD-DA already fader-scaled */
            sample += (int32_t)cdda_buf[i * 2] + (int32_t)cdda_buf[i * 2 + 1];
        }
        if (adpcm_n && i < adpcm_n) {
            int32_t a = adpcm_buf[i*2];
            if (adpcm_vol < 65536)
                a = (int32_t)(((int64_t)a * adpcm_vol) >> 16);
            sample += a;                                         /* ADPCM is mono (dup L/R) */
        }
        sample = (sample * factor) >> 8;
        if (sample > 32767) sample = 32767; else if (sample < -32768) sample = -32768;
        sound_buffer[i] = sample;
    }
}

static bool is_pce_cd(void)
{
    return ACTIVE_FILE && ACTIVE_FILE->ext && strcmp(ACTIVE_FILE->ext, "cue") == 0;
}

static void apply_cpu_clock(void)
{
    /* PCE-CD only: auto-OC level 2 (340MHz, the max the firmware/menu offers —
     * same level VB uses) for the extra CD load: SCSI engine + CD-DA
     * fseek/fread + ADPCM on top of the core. HuCard runs full speed at
     * stock so it stays at the user's clock. Same VB pattern: NOT persisted
     * (exit resets the clock), no-op on OSPI1 SD hardware (guarded inside). The
     * actual clock is proven in /pcecd_diag.txt at disc mount ("clock=... MHz"). */
    if (is_pce_cd())
        SystemClock_Config(2);
}

static void sleep_wake_up()
{
    /* Safety net: sram_save already pce_cd_close()'d before unmount, but if
     * any path skipped that, drop dangling FILE* without fclose. */
    if (is_pce_cd())
        pce_cd_invalidate_handles();

    /* gw_sleep restores the *settings* OC and reinits audio at that clock.
     * PCE-CD gameplay forces auto-OC lvl2 — re-boost and reinit SAI/DMA so
     * sample pacing matches again (same MSX/Genesis/NES/Amstrad pattern;
     * SystemClock_Config reprograms the audio PLL). */
    if (is_pce_cd()) {
        SystemClock_Config(2);
        odroid_audio_init(odroid_audio_sample_rate_get());
        audio_start_playing_full_length(audio_get_buffer_full_length());
    }
}

int app_main_pce(uint8_t load_state, uint8_t start_paused, int8_t save_slot) {
    if (start_paused) {
        common_emu_state.pause_after_frames = 2;
        odroid_audio_mute(true);
    } else {
        common_emu_state.pause_after_frames = 0;
    }

    // Apply OC if needed
    apply_cpu_clock();

    odroid_system_init(APPID_PCE, PCE_SAMPLE_RATE);
    odroid_system_emu_init(&LoadState, &SaveState, &Screenshot, NULL, &sleep_wake_up, &pce_sram_save_cb, NULL);
    pce_log[0]=0;

    // Init Graphics
    init_color_pals();
    pce_layout_update(current_width, current_height);
    const int refresh_rate = FPS_NTSC;
    sprintf(pce_log,"%d",refresh_rate);

    gfx_init();
    printf("Graphics initialized\n");

    // Init Sound
    audio_start_playing(AUDIO_BUFFER_LENGTH_PCE);
    pce_snd_init();
    printf("Sound initialized\n");

    // Init PCE Core
    pce_init();

#if CHEAT_CODES == 1
    int cheat_count = 0;
    const char **active_cheat_codes = NULL;
    for(int i=0; i<MAX_CHEAT_CODES && i<ACTIVE_FILE->cheat_count; i++) {
        if (odroid_settings_ActiveGameGenieCodes_is_enabled(ACTIVE_FILE->path, i)) {
            cheat_count++;
        }
    }

    active_cheat_codes = rg_alloc(cheat_count * sizeof(char**), MEM_ANY);
    for(int i=0, j=0; i<MAX_CHEAT_CODES && i<ACTIVE_FILE->cheat_count; i++) {
        if (odroid_settings_ActiveGameGenieCodes_is_enabled(ACTIVE_FILE->path, i)) {
            active_cheat_codes[j] = ACTIVE_FILE->cheat_codes[i];
            j++;
        }
    }
    PCE.patchs = (char **)active_cheat_codes;
    PCE.rp_count = cheat_count;
#endif

    LoadCartPCE();
    ResetPCE(false);
    printf("PCE Core initialized\n");

    // If user select "RESUME" in main menu
    if (load_state) {
        odroid_system_emu_load_state(save_slot);
    } else {
        lcd_clear_buffers();
    }
    // Main emulator loop
    printf("Main emulator loop start\n");
    odroid_gamepad_state_t joystick = {0};
    odroid_dialog_choice_t options[] = {
            ODROID_DIALOG_CHOICE_LAST
    };
    while (true) {
        wdog_refresh();

        bool drawFrame = common_emu_frame_loop();

        odroid_input_read_gamepad(&joystick);
        common_emu_input_loop(&joystick, options, &blit);
        common_emu_input_loop_handle_turbo(&joystick);

        /* PCE-CD: auto-press START (RUN) at the "CD-ROM SYSTEM" screen so the
         * disc boots without the user pressing it. Injected after the emu input
         * loop (so it can't trip the emulator menu) and only for a window early
         * after launch, then released. NEVER after a state load: a restored game
         * is past the boot screen, and RUN pauses it (the "resume freezes after
         * 1 second" bug — host-verified at the device log's exact lba 13811). */
        if (!s_cd_state_loaded && strcmp(ACTIVE_FILE->ext, "cue") == 0) {
            static int s_autostart = 0;
            if (s_autostart <= 150) {
                s_autostart++;
                if (s_autostart >= 60) joystick.values[ODROID_INPUT_START] = 1;
            }
        }

        pce_input_read(&joystick);

        /* Chunked SCSI->ADPCM DMA pump (<=8KB/frame) — see pce_scsi_run. */
        pce_scsi_run();

        s_skip_render = !drawFrame;   /* drop tile/sprite work on skip frames */
        for (PCE.Scanline = 0; PCE.Scanline < 263; ++PCE.Scanline) {
            gfx_run();
        }
        s_skip_render = false;        /* never leak into menu/screenshot paths */

        if (drawFrame) {
            pce_osd_gfx_blit();
        }

        pce_pcm_submit();

        pce_sound_sync_with_prefetch();   /* sound_sync + CD-DA prefetch in the wait */

        pce_adpcm_frame_end();

        // Prevent overflow
        PCE.Timer.cycles_counter -= Cycles;
        PCE.MaxCycles -= Cycles;
        Cycles = 0;
    }
#if CHEAT_CODES == 1
    rg_free(active_cheat_codes); // No need to clean up the objects in the array as they're allocated in read only space
#endif
}
