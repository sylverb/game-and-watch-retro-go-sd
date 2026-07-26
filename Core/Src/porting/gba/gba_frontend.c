/* The pieces of gpSP that gpSP does not own.
 *
 * gpSP is written as a libretro core: a handful of globals and the whole VFS are
 * supplied by whatever front-end is hosting it. On a libretro build that is
 * RetroArch; here it is us. The QEMU harness that proved this core boots and
 * renders had a file exactly like this one (scratchpad/gbabench/full_stubs.c) —
 * this is its device counterpart, and it defines the same symbols with the same
 * meanings so the two builds stay the same program.
 *
 * The names go through gba_redefines like every other object of this core, so a
 * symbol that went missing here would be a link error rather than a quiet
 * aliasing onto some other core's overlay.
 */

#include <stdio.h>
#include <stdint.h>
#include <stddef.h>
#include <string.h>

/* Declared, not included: "main.h" from this directory finds gpSP's, not the
 * firmware's — the same collision the note below describes. */
void wdog_refresh(void);

/* gpSP's own headers are not included here, for the reason main_gba.c gives: they
 * pull in libretro types and register-name macros that collide with CMSIS. And a
 * quoted "common.h" from this directory would find the FIRMWARE's, not gpSP's —
 * gpSP's own sources only get theirs because a quoted include searches the
 * including file's directory first.
 *
 * So the handful of definitions live here spelled out, each pinned by hand to what
 * gpSP declares:
 *   u32                   = unsigned int                   (common.h:95)
 *   MAX_TRANSLATION_GATES = 8                              (cpu.h:159)
 *   boot_mode             = { boot_game = 0, boot_bios }   (main.h:64-67)
 *
 * Be aware of what that costs: if gpSP ever changes one of those types, the C
 * compiler never sees both halves and the linker does not check types, so the
 * mismatch is silent. gpSP is pinned here as a submodule, so such a change can
 * only arrive with an upstream merge — and this file is what that merge has to
 * re-read. It is a hand-maintained contract, not a checked one. */
#define GBA_MAX_TRANSLATION_GATES 8
#define GBA_BOOT_GAME             0u

/* ------------------------------------------------------ front-end globals ---
 * Storage that gpSP reads and writes but does not define.
 */

/* Set from gba_over.h when a cart's game code is recognised (the Korean fan
 * translations included — that is what the K entries in the fork are for). The
 * interpreter compares PC against it and, on a match, throws away the rest of the
 * frame's cycles instead of spinning the game's idle loop. It is the single
 * biggest saving in the core, so a cart that is not in the table is not broken,
 * only slower. 0xFFFFFFFF is "no idle loop known", never a real PC. */
unsigned int idle_loop_target_pc = 0xFFFFFFFF;
/* 0 = ALWAYS (the classic table entries); 1 = WHEN_NE, for raster polls whose
 * callers burst through them — see IDLE_COND_* in gpsp's cpu.h. */
unsigned int idle_loop_cond = 0;

/* Only the dynamic recompiler uses these; the interpreter is what runs here, and
 * cpu_threaded.c is not compiled at all (no Thumb-2 backend exists). They stay
 * because load_gamepak() writes them from the override table. */
unsigned int translation_gate_targets = 0;
unsigned int translation_gate_target_pc[GBA_MAX_TRANSLATION_GATES];

/* Honour the GBA's real per-line sprite budget rather than drawing all 128. The
 * hardware could not fetch them either, and OBJ_PER_LINE_MAX (32) sizes the
 * priority list to match. */
int sprite_limit = 1;

/* The frame driver in main_gba.c sets this from common_emu_frame_loop(): the PPU
 * still evaluates the frame, it just does not draw it. */
unsigned int skip_next_frame = 0;

/* Straight into the game. The BIOS intro is 2 seconds of Nintendo logo that this
 * player has already seen, and booting through it would need the real BIOS. */
unsigned int selected_boot_mode = GBA_BOOT_GAME;

/* Link cable and wireless adapter. Both are compiled in — the harness compiled
 * them, and gba_memory.c calls into them from reachable code — but the unit has
 * no link port and no second unit to talk to, so nobody is ever on the other end.
 * These say so honestly: no clients, nothing sent, nothing received. */
unsigned int netplay_num_clients = 0;
unsigned int netplay_client_id = 0;

void netpacket_send(uint16_t client_id, const void *buf, size_t len)
{
    (void)client_id; (void)buf; (void)len;
}

void netpacket_poll_receive(void)
{
}

/* ------------------------------------------------------------- watchdog ---
 * gpSP calls this every 64 KB of its save-type scan. That scan reads a megabyte
 * of the cart, and on this device the cart is memory-mapped in QSPI flash, so a
 * megabyte of it takes far longer than the ~472 ms window watchdog allows. It
 * reset the machine mid-scan and dropped the player straight back to the game
 * list: no fault, no BSOD, no message — the emulator simply never appeared to
 * start. Overriding gpSP's weak no-op is the whole fix. */
void gba_scan_yield(void)
{
    wdog_refresh();
}

/* ------------------------------------------------------------------- VFS ---
 * gpSP reads the cart through libretro's filestream API. On this device it never
 * does: main_gba.c calls gba_set_xip_rom() first, and load_gamepak_raw() takes the
 * memory-mapped path and returns before it opens anything (gba_memory.c:2667).
 *
 * These are implemented over the firmware's stdio anyway, rather than returning
 * NULL and calling it done. The path is unreachable today, but "unreachable" is a
 * claim about the caller, not about this file — and a stub that lies about having
 * read a file would hand the core a ROM of zeroes and let it fail somewhere else,
 * far from the cause. A real read is about forty lines. It costs nothing to be
 * true.
 */
#define RETRO_VFS_SEEK_POSITION_START    0
#define RETRO_VFS_SEEK_POSITION_CURRENT  1
#define RETRO_VFS_SEEK_POSITION_END      2

struct RFILE {
    FILE *fp;
};

/* One at a time: gpSP opens the cart, reads it, and closes it. FatFs allows ten
 * handles open at once, so a single slot is not a limit anyone can reach — but it
 * IS a claim, so it fails loudly rather than quietly reusing a live handle. */
static struct RFILE g_vfs_file;

struct RFILE *filestream_open(const char *path, unsigned mode, unsigned hints)
{
    (void)mode; (void)hints;

    if (g_vfs_file.fp != NULL) {
        printf("gba: filestream_open(%s) while a file is already open\n", path);
        return NULL;
    }
    g_vfs_file.fp = fopen(path, "rb");
    return g_vfs_file.fp ? &g_vfs_file : NULL;
}

int filestream_close(struct RFILE *stream)
{
    if (stream == NULL || stream->fp == NULL)
        return -1;
    int rc = fclose(stream->fp);
    stream->fp = NULL;
    return rc;
}

int64_t filestream_get_size(struct RFILE *stream)
{
    if (stream == NULL || stream->fp == NULL)
        return 0;

    long here = ftell(stream->fp);
    if (fseek(stream->fp, 0, SEEK_END) != 0)
        return 0;
    long size = ftell(stream->fp);
    fseek(stream->fp, here, SEEK_SET);
    return (size < 0) ? 0 : (int64_t)size;
}

int64_t filestream_seek(struct RFILE *stream, int64_t offset, int seek_position)
{
    if (stream == NULL || stream->fp == NULL)
        return -1;

    int whence = (seek_position == RETRO_VFS_SEEK_POSITION_CURRENT) ? SEEK_CUR
               : (seek_position == RETRO_VFS_SEEK_POSITION_END)     ? SEEK_END
                                                                    : SEEK_SET;
    if (fseek(stream->fp, (long)offset, whence) != 0)
        return -1;
    return (int64_t)ftell(stream->fp);
}

int64_t filestream_tell(struct RFILE *stream)
{
    if (stream == NULL || stream->fp == NULL)
        return -1;
    return (int64_t)ftell(stream->fp);
}

int64_t filestream_read(struct RFILE *stream, void *data, int64_t len)
{
    if (stream == NULL || stream->fp == NULL || len <= 0)
        return 0;
    return (int64_t)fread(data, 1, (size_t)len, stream->fp);
}

int64_t filestream_write(struct RFILE *stream, const void *data, int64_t len)
{
    if (stream == NULL || stream->fp == NULL || len <= 0)
        return 0;
    return (int64_t)fwrite(data, 1, (size_t)len, stream->fp);
}

int filestream_flush(struct RFILE *stream)
{
    if (stream == NULL || stream->fp == NULL)
        return -1;
    return fflush(stream->fp);
}
