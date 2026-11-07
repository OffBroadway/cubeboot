/* Copyright (C) 2026 Nigzel / Bjerreman. SPDX-License-Identifier: GPL-2.0-only */
/*
 * cbvmc_resident_t2: the console glue of the T2 multi-executable trigger (cbvmc_t2.h), part of BOTH resident images (Flippy and the Dolphin RAM build).
 * Original work.  The scanner, the word table applier and this handler stay in memory for the whole session, above ArenaHi with the rest of the
 * resident image, so they are still there when the second, third ... executable starts.
 */
#include "cbvmc_resident.h"
#include "cbvmc_blob.h"
#include "cbvmc_t2.h"

/* blob symbols (the resident is linked against them one by one, build_blob.sh) */
extern void (*cbvmc_t2_fn)(struct cbvmc_ctx *ctx);
extern uint32_t cbvmc_fix_pending, cbvmc_fix_src;
extern void (*cbvmc_diag_fn)(void);
extern char cbvmc_chain[];

/* config.ini [cubeboot] vmc_t2 (default 1), written by the Cubeboot patch before the game starts (cbvmc_flash_set_allow): 0 = no breakpoint, the first
   executable is served as before and nothing else */
uint32_t cbvmc_t2_enable = 1;
struct cbvmc_t2 cbvmc_t2_state;
static uint8_t snap[CBVMC_T2_SNAP_BYTES] __attribute__((aligned(32)));

static void t2_flush(void *c, uint32_t a, uint32_t n)
{
    uint32_t p;
    (void)c;
    a = 0x80000000u | (a & 0x01FFFFFFu);
    for (p = a & ~31u; p < a + n; p += 32u)
        __asm__ volatile("dcbf 0,%0 ; sync ; icbi 0,%0" : : "r"(p) : "memory");
    __asm__ volatile("sync ; isync" : : : "memory");
}

static void t2_dabr(void *c, uint32_t v)
{
    (void)c;
    __asm__ volatile("mtspr 1013,%0 ; isync" : : "r"(v));
}

static int t2_sync(void *c) { (void)c; return cbvmc_resident_sync(); }

/* both channel models back to the state of the first install (cbvmc_blob_bind_ext / _b): nothing selected, no latched interrupt; the card itself (its
   unlock and status) stays as it is, like a real card that stays in the slot across a reboot of the game */
static void t2_card_reset(void *c)
{
    struct cubeboot_vmc_exi *e[2];
    unsigned i;
    (void)c;
    e[0] = &cbvmc_state.rt.exi; e[1] = &cbvmc_state.rt.exi1;
    for (i = 0; i < 2; ++i) cbvmc_t2_exi_reset(e[i]);
    cbvmc_state.rt.exi_line_prev = 0;
}

static uint32_t t2_now(void *c) { uint32_t t; (void)c; __asm__ volatile("mftb %0" : "=r"(t)); return t; }
static void t2_note(void *c, const char *s, unsigned n) { (void)c; cbvmc_resident_stage_note(s, n, 0); }
static void t2_entry(struct cbvmc_ctx *ctx) { (void)cbvmc_t2_trap(&cbvmc_t2_state, ctx); }

int cbvmc_resident_t2_arm(unsigned words)
{
    struct cbvmc_t2_env env;
    char line[96];
    unsigned n = 0, k = 0;
    char d[10];
    const char *s = words ? "stage 0 (first executable, installed at OSInit) OWN words " : "stage 0 (first executable, installed at OSInit) KEEP words ";
    if (!cbvmc_t2_enable) return 1;
    env.mem = (uint8_t *)0x80000000u; env.mem_size = 0x01800000u;
    env.chain = (volatile uint32_t *)cbvmc_chain; env.fix_pending = &cbvmc_fix_pending;
    env.flush = t2_flush; env.set_dabr = t2_dabr; env.sync = t2_sync; env.card_reset = t2_card_reset; env.stage_note = t2_note; env.now = t2_now; env.c = 0;
    cbvmc_t2_init(&cbvmc_t2_state, &env);
    cbvmc_fix_src = (uint32_t)snap & 0x7FFFFFFFu;
    t2_flush(0, (uint32_t)&cbvmc_fix_src, 4);
    cbvmc_diag_fn = cbvmc_resident_diag;
    cbvmc_t2_fn = t2_entry;                         /* the handler first, the breakpoint last */
    t2_flush(0, (uint32_t)&cbvmc_t2_fn, 4);
    cbvmc_t2_arm(&cbvmc_t2_state, snap);
    t2_flush(0, (uint32_t)snap, sizeof(snap));
    t2_flush(0, (uint32_t)&cbvmc_fix_pending, 4);
    while (*s && n < sizeof(line) - 12u) line[n++] = *s++;
    do { d[k++] = (char)('0' + words % 10u); words /= 10u; } while (words);
    while (k) line[n++] = d[--k];
    line[n++] = '\n';
    cbvmc_resident_stage_note(line, n, 1);
    return 0;
}
