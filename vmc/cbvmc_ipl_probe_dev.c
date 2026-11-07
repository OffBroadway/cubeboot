/* Copyright (C) 2026 Nigzel / Bjerreman. SPDX-License-Identifier: GPL-2.0-only */
/* G3 IPL probe, Cubeboot side (only with -DCBVMC_IPL_PROBE).  Original work.  See cbvmc_ipl_probe.h and docs/virtual_memory_card.md. */
#ifdef CBVMC_IPL_PROBE

#include <stddef.h>
#include <string.h>

#include "flippy_sync.h"
#include "attr.h"
#include "time.h"
#include "ipl.h"
#include "usbgecko.h"

#include "cbvmc_ipl_probe.h"
#include "cbvmc_flash_hook.h"

extern const void _patches_end;
extern const void _lowmem_end;

/* the known function addresses of patches/linker/link_*.ld: absolute symbols, resolved at link time, nothing is called */
#define SYMLIST(X, P) X(P, EXIImm) X(P, EXIImmEx) X(P, EXISync) X(P, EXISelect) X(P, EXIDeselect) X(P, EXILock) X(P, EXIUnlock) X(P, CARDProbe) X(P, __CARDGetStatusEx) X(P, OSDisableInterrupts)
#define ALLSETS(X) SYMLIST(X, ntsc10) SYMLIST(X, ntsc11) SYMLIST(X, ntsc12_001) SYMLIST(X, ntsc12_101) SYMLIST(X, pal10) SYMLIST(X, pal11) SYMLIST(X, pal12)
#define X_EXTERN(P, N) extern char P##_##N[];
#define X_ENTRY(P, N) { #P, #N, (uint32_t)(uintptr_t)&P##_##N },
ALLSETS(X_EXTERN)
static const struct cbvmc_ipl_sym g_syms[] = { ALLSETS(X_ENTRY) };

#define MFSPR(n) ({ uint32_t v_; __asm__ volatile("mfspr %0,%1" : "=r"(v_) : "i"(n)); v_; })

/* ---- the file: block 0 (32 bytes) is the progress marker, rewritten after every stage; the text follows from byte 32 in whole 32-byte blocks ---- */
#define START_TEXT "cbvmc ipl probe 1 (read-only, docs/virtual_memory_card.md)\nstarted\n"
struct sink { uint32_t fd; uint32_t off; int bad; };
static char g_chunk[1024] __attribute__((aligned(32)));
static char g_mark[32] __attribute__((aligned(32)));

static void put_hex(char *d, uint32_t v) { int i; for (i = 0; i < 8; ++i) d[i] = "0123456789ABCDEF"[(v >> (28 - 4 * i)) & 15]; }

static int wr(struct sink *s, const char *src, unsigned len, uint32_t at)
{
    DCFlushRange((void *)src, len);
    if (dvd_custom_write((char *)src, at, len, s->fd) != 0) { s->bad = 1; return 1; }
    return 0;
}

static int marker(struct sink *s, unsigned stage)
{
    unsigned i;
    for (i = 0; i < 32; ++i) g_mark[i] = '\n';
    memcpy(g_mark, "IPLPROBE1 S=", 12);
    g_mark[12] = (char)('0' + stage); memcpy(g_mark + 13, "/6 END=", 7); put_hex(g_mark + 20, s->off);
    return wr(s, g_mark, 32, 0);
}

static int emit(void *ctx, unsigned stage, const char *text, unsigned len)
{
    struct sink *s = (struct sink *)ctx;
    unsigned done = 0;
    if (s->bad) return 1;
    while (done < len) {
        unsigned n = len - done, pad, i;
        if (n > sizeof(g_chunk)) n = sizeof(g_chunk);
        memcpy(g_chunk, text + done, n);
        pad = (32u - (n & 31u)) & 31u;                    /* the Flippy API writes whole 32-byte blocks: pad with newlines */
        for (i = 0; i < pad; ++i) g_chunk[n + i] = '\n';
        if (wr(s, g_chunk, n + pad, s->off)) return 1;
        s->off += n + pad; done += n;
    }
    return marker(s, stage);                              /* after the text of this stage is down */
}

void cbvmc_ipl_probe_device(void)
{
    static struct cbvmc_ipl_env e;
    static struct sink s;
    const char *path = "/cubeboot/ipl_probe.txt";
    file_status_t *st;
    unsigned i, ch;
    volatile uint32_t *p;

    if (!cbvmc_flash_enabled()) return;                  /* vmc = 0 in config.ini: stock behaviour, no file */
    custom_OSReport("cbvmc ipl probe: start\n");
    dvd_custom_unlink((char *)path);
    if (dvd_custom_open(path, FILE_ENTRY_TYPE_FILE, IPC_FILE_FLAG_WRITE | IPC_FILE_FLAG_DISABLECACHE | IPC_FILE_FLAG_DISABLEFASTSEEK | IPC_FILE_FLAG_DISABLESPEEDEMU) != 0) return;
    st = dvd_custom_status();
    if (st == NULL || st->result != 0) return;
    s.fd = st->fd; s.off = 32; s.bad = 0;
    /* stage 1: the start marker goes down before anything else is read, so that a crash later still leaves a file that says how far it got */
    if (emit(&s, 1, START_TEXT, sizeof(START_TEXT) - 1)) { if (!s.bad) dvd_custom_close(s.fd); return; }

    e.mem = (const uint8_t *)0x80000000;
    e.bs2_lo = 0x81300000; e.bs2_hi = 0x81400000;
    e.ipl_rev = (uint32_t)get_ipl_revision();
    { register uint32_t r2v __asm__("r2"); register uint32_t r13v __asm__("r13"); e.r2 = r2v; e.r13 = r13v; }
    __asm__ volatile("mfmsr %0" : "=r"(e.msr));
    e.pvr = MFSPR(287); e.hid0 = MFSPR(1008); e.sdr1 = MFSPR(25);
    e.bat[0] = MFSPR(528); e.bat[1] = MFSPR(529); e.bat[2] = MFSPR(530); e.bat[3] = MFSPR(531);
    e.bat[4] = MFSPR(532); e.bat[5] = MFSPR(533); e.bat[6] = MFSPR(534); e.bat[7] = MFSPR(535);
    e.bat[8] = MFSPR(536); e.bat[9] = MFSPR(537); e.bat[10] = MFSPR(538); e.bat[11] = MFSPR(539);
    e.bat[12] = MFSPR(540); e.bat[13] = MFSPR(541); e.bat[14] = MFSPR(542); e.bat[15] = MFSPR(543);
    p = (volatile uint32_t *)0xCC006000;
    for (i = 0; i < 10; ++i) e.di[i] = p[i];
    for (ch = 0; ch < 3; ++ch) {
        p = (volatile uint32_t *)(0xCC006800 + 0x14 * ch);
        for (i = 0; i < 5; ++i) e.exi[ch][i] = p[i];
    }
    e.pi_cause = *(volatile uint32_t *)0xCC003000; e.pi_mask = *(volatile uint32_t *)0xCC003004;
    e.patches_end = (uint32_t)(uintptr_t)&_patches_end; e.lowmem_end = (uint32_t)(uintptr_t)&_lowmem_end;
    e.vmc_enabled = (uint32_t)cbvmc_flash_enabled();
    e.syms = g_syms; e.nsyms = sizeof(g_syms) / sizeof(g_syms[0]);
    e.now = gettime; e.ticks_per_ms = (uint32_t)TB_TIMER_CLOCK;
    e.emit = emit; e.ctx = &s;
    cbvmc_ipl_probe_run(&e);
    if (!s.bad) dvd_custom_close(s.fd);
    custom_OSReport("cbvmc ipl probe: end (%d)\n", s.bad);
}

#endif
