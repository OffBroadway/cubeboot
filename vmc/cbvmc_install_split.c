/* Copyright (C) 2026 Nigzel / Bjerreman. SPDX-License-Identifier: GPL-2.0-only */
/*
 * cbvmc_install_split: the INSTALL image of the loader build (PowerPC, freestanding).  Original work.
 * Runs once from the hook inside OSInit (the `bl OSInitAlarm` after `bl __OSInitSystemCall`, before any EXI use) on its own stack, from memory
 * that lies inside the arena: when OSInit returns, ClearArena zeroes all of it and the game's heaps take it over (docs/virtual_memory_card.md).
 * Everything that must outlive this function is in the resident image (cbvmc_resident*.c); this file only calls it.
 *
 * Loader build: nothing is formatted or created.  If the card file is missing or not valid, the drive is not a normal-mode FlippyDrive, or a
 * pending journal cannot be replayed, the game is left completely untouched (no vector, no words) and the reason goes to OSReport.
 * Test build (-DCBVMC_TEST_RAM, Dolphin): the RAM card is formatted when missing, because nothing else provides one.
 *
 * Order: DSI stub and word table checks; open the resident store; the file must exist; replay a pending journal; validate and open the card
 * through the model; copy the blob; bind; install the DSI vector (one word); apply the word table.
 */
#include "cbvmc_plan.h"
#include "cbvmc_blob.h"
#include "cbvmc_words.h"
#include "cbvmc_resident.h"
#include "cbvmc_cardgen.h"

extern const unsigned char cbvmc_blob_image[], cbvmc_blob_image_end[];
extern char __cbvmc_blob_end[];
extern char cbvmc_vec[];
extern char cbvmc_chain[];

void cbvmc_osreport(const char *fmt, ...);     /* cbvmc_tramp.S: OSReport with CR6 cleared */

#define CARD_PATH(s) (cbvmc_resident_card_path(s))

enum { ST_GEN = 10, ST_NOSPACE = 9, ST_PHYSICAL = 8, ST_OK = 0, ST_ERR_VEC = 1, ST_ERR_WORDS = 2, ST_ERR_CARD = 6, ST_ERR_BIND = 7, ST_ERR_JOURNAL = 5 };

#ifdef CBVMC_TEST_RAM
/* result words for the harness (Dolphin): the block lives in the harness, never in this (short-lived) image or the resident one;
   the pointer is a resident variable the harness sets (cbvmc_resident_ram.c). Indexes: 0 magic, 1 status, 2 detail, 3 words, 4 free blocks. */
extern uint32_t *cbvmc_test_res;
static void res_put(unsigned i, uint32_t v) { if (cbvmc_test_res) cbvmc_test_res[i] = v; }
#endif

static void flush_range(uint32_t a, uint32_t n)
{
    uint32_t p;
    for (p = a & ~31u; p < a + n; p += 32u)
        __asm__ volatile("dcbf 0,%0 ; sync ; icbi 0,%0" : : "r"(p) : "memory");
    __asm__ volatile("sync ; isync" : : : "memory");
}

static int fail(unsigned st, int detail)
{
#ifdef CBVMC_TEST_RAM
    res_put(1, st); res_put(2, (uint32_t)detail);
#endif
    cbvmc_osreport("cbvmc: not installed, status %u detail %d\n", st, detail);
    return (int)st;
}

/* the status note: one line per slot, built by hand (no libc formatting in this image) */
static unsigned put_s(char *b, unsigned n, const char *s) { while (*s) b[n++] = *s++; return n; }
static unsigned put_d(char *b, unsigned n, int v)
{
    char t[12]; unsigned k = 0;
    if (v < 0) { b[n++] = '-'; v = -v; }
    do { t[k++] = (char)('0' + v % 10); v /= 10; } while (v);
    while (k) b[n++] = t[--k];
    return n;
}

extern uint32_t cbvmc_diag[8];      /* resident image: what the Cubeboot patch decided before the game started (flash build); [7] = the vmc_diag mask */
extern uint32_t cbvmc_diag_files;   /* resident image: periodic / per-stage diagnostic files during the game */

static int install(void)
{
    char note[384];
    unsigned nn = 0;
    struct cubeboot_vmc_identity id = { 0x4D53484E454C4954ull, 0x00000001u };   /* placeholder: used only when a card is formatted (test build) */
#ifdef CBVMC_TEST_RAM
    struct cubeboot_vmc_geometry geom = { 4, CUBEBOOT_VMC_ENC_WINDOWS_1252 };        /* the Dolphin harness has a 0.5 MiB RAM card */
#else
    struct cubeboot_vmc_geometry geom = { 128, CUBEBOOT_VMC_ENC_WINDOWS_1252 };      /* 16 MiB, ANSI: what vmc_builder creates */
#endif
    const struct cubeboot_vmc_store *tops[2] = { 0, 0 };
    uint32_t blob_len = (uint32_t)(cbvmc_blob_image_end - cbvmc_blob_image), i;
    unsigned allow = cbvmc_resident_slot_allow() & 3u, served = 0, s, first = 0, live = 0;
    unsigned bad = 0, first_err = 0, WN = 0;
    const struct cbvmc_word *W = 0;
    int rc, detail, first_detail = 0, t2;

    nn = put_s(note, nn, "install image ran; hook: ");
    if (cbvmc_diag[0] == 0x44494147u) {
        cbvmc_diag_files = (cbvmc_diag[7] & 0x10000000u) ? 1u : 0u;          /* CBVMC_IPLD_NOTES of vmc_diag */
        nn = put_s(note, nn, "probe A state "); nn = put_d(note, nn, (int)cbvmc_diag[3]); nn = put_s(note, nn, " id "); nn = put_d(note, nn, (int)cbvmc_diag[4]);
        nn = put_s(note, nn, ", B state "); nn = put_d(note, nn, (int)cbvmc_diag[5]); nn = put_s(note, nn, " id "); nn = put_d(note, nn, (int)cbvmc_diag[6]);
        nn = put_s(note, nn, ", mask "); nn = put_d(note, nn, (int)cbvmc_diag[1]); nn = put_s(note, nn, ", loader rc "); nn = put_d(note, nn, (int)cbvmc_diag[2]);
    } else nn = put_s(note, nn, "(loader DOL: no patch data)");
    nn = put_s(note, nn, "\nallow mask at install "); nn = put_d(note, nn, (int)allow); note[nn++] = '\n';
#ifndef CBVMC_TEST_RAM
    if (cbvmc_geom_unpack(cbvmc_resident_card_geom(), &geom) == 0) {         /* config.ini vmc_blocks / vmc_region: only for a card file generated this boot */
        nn = put_s(note, nn, "new cards: "); nn = put_d(note, nn, (int)CUBEBOOT_VMC_BLOCKS_FOR(geom.mbits) - 5); nn = put_s(note, nn, geom.encoding ? " blocks, Shift-JIS\n" : " blocks, Windows-1252\n");
    }
#endif
    nn = put_s(note, nn, cbvmc_t2_enable ? "multi-stage watch: on, see cbvmc_stages.txt\n" : "multi-stage watch: off\n");
    /* a physical card (or any other device) in a slot always wins: that slot is left alone; with both slots taken nothing is installed and the game runs
       exactly as without us */
    if (!allow) return fail(ST_PHYSICAL, 0);
    if (*(volatile uint32_t *)CBVMC_DSI_VECTOR != CBVMC_DSI_ORIG) return fail(ST_ERR_VEC, 0);
    {   /* the word table: the loader's hook block (every loader writes one: the generic scanner's table, 0 rows = keep-alive install of an executable the
           scanner refused, see cbvmc_t2.h).  No per-game table exists. */
        const volatile uint32_t *blk = (const volatile uint32_t *)CBVMC_HOOKBLK;
        if (blk[0] == CBVMC_HOOKBLK_MAGIC && blk[3] <= CBVMC_HOOKBLK_MAXW) { W = (const struct cbvmc_word *)(blk + 4); WN = blk[3]; }
        else return fail(ST_ERR_WORDS, -1);
    }
    rc = cbvmc_words_check((const uint8_t *)0x80000000u, 0x80000000u, 0x01800000u, W, WN, &bad);
    if (rc != CBVMC_WORDS_OK) return fail(ST_ERR_WORDS, (int)bad);

    /* each allowed slot: open its card file (it must exist in the loader build), replay its pending journal; a slot that fails is simply not served */
    for (s = 0; s < 2; ++s) {
        if (!(allow & (1u << s))) continue;
        rc = cbvmc_resident_open(s, &tops[s], &detail);
        if (rc == CBVMC_OPEN_ERR_NOFILE && tops[s]) {
            /* the card file does not exist: generate it (never over an existing file, never under /swiss/): folders, formatted card in <path>.tmp, validate, rename */
            struct cubeboot_vmc_identity gid;
            uint32_t t0, t1;
            int g;
            __asm__ volatile("mftb %0" : "=r"(t0));
            gid.time = ((uint64_t)t0 << 8) | s;
            gid.seed = 0x4D53484E454C4954ull ^ ((uint64_t)t0 * 0x9E3779B1u) ^ ((uint64_t)s << 56);        /* the serial only has to differ between cards; the console's flash-ID check is skipped (section 12.14) */
            cbvmc_osreport("cbvmc: slot %c card file missing, generating %s ...\n", 'A' + (int)s, CARD_PATH(s));
            g = cbvmc_cardgen(tops[s], CARD_PATH(s), s, &gid, &geom, 0, 0, 0);
            __asm__ volatile("mftb %0" : "=r"(t1));
            if (g == CBVMC_GEN_CREATED) nn = put_s(note, nn, s ? "slot B: card file generated this boot\n" : "slot A: card file generated this boot\n");
            if (g == CBVMC_GEN_CREATED) { cbvmc_osreport("cbvmc: slot %c card file created in %u ms\n", 'A' + (int)s, (unsigned)((t1 - t0) / 40500u)); rc = CBVMC_OPEN_OK; }
            else { rc = ST_GEN; detail = g; }
        }
        if (rc == CBVMC_OPEN_OK && cbvmc_resident_recover(s) != 0) { rc = ST_ERR_JOURNAL; detail = 0; }
        if (rc == CBVMC_OPEN_OK && cbvmc_resident_prepare(s) != 0) { rc = ST_NOSPACE; detail = 0; }       /* journal cannot be created in full size: SD full or read-only */
        if (rc != CBVMC_OPEN_OK) {
            if (!first_err) { first_err = (unsigned)rc; first_detail = detail; }
            cbvmc_osreport("cbvmc: slot %c not served (status %u detail %d)\n", 'A' + (int)s, (unsigned)rc, detail);
            nn = put_s(note, nn, s ? "slot B: NOT served, status " : "slot A: NOT served, status "); nn = put_d(note, nn, rc);
            nn = put_s(note, nn, " detail "); nn = put_d(note, nn, detail); note[nn++] = '\n';
            continue;
        }
        served |= 1u << s;
        nn = put_s(note, nn, s ? "slot B: card file ok\n" : "slot A: card file ok\n");
    }
    note[nn] = 0;
    cbvmc_resident_note(note);                       /* what happened, for the owner (status file on the SD, best effort) */
    if (!served) return fail(first_err, first_detail);
    first = (served & 1u) ? 0u : 1u;

    /* the blob: zero its footprint, copy, make it visible */
    for (i = 0; i < (uint32_t)(__cbvmc_blob_end - (char *)CBVMC_BLOB_BASE); i += 4) *(volatile uint32_t *)(CBVMC_BLOB_BASE + i) = 0;
    for (i = 0; i < blob_len; ++i) ((volatile uint8_t *)CBVMC_BLOB_BASE)[i] = cbvmc_blob_image[i];
    flush_range(CBVMC_BLOB_BASE, (uint32_t)(__cbvmc_blob_end - (char *)CBVMC_BLOB_BASE));
    ((volatile uint32_t *)cbvmc_chain)[0] = *(volatile uint32_t *)CBVMC_DSI_VECTOR;
    flush_range((uint32_t)cbvmc_chain, 8);

    if (cbvmc_blob_bind_ext(tops[first], cbvmc_resident_tick, cbvmc_resident_tick_context(), cbvmc_resident_stack_top()) != 0) return fail(ST_ERR_BIND, 2);
    rc = 0;
    if (served & 1u) {
        rc = cubeboot_vmc_port_init(&cbvmc_state.port, &cbvmc_state.store, CUBEBOOT_VMC_SLOT_A, CARD_PATH(0), &id, &geom);
        if (rc == 0) {
            cubeboot_vmc_port_service(&cbvmc_state.port);
            rc = cbvmc_state.port.source == CUBEBOOT_VMC_SOURCE_VIRTUAL ? 0 : cbvmc_state.port.last_error;
        }
        if (rc == 0) live |= 1u;
        else { cbvmc_osreport("cbvmc: slot A card not usable (%d)\n", rc); if (!first_err) first_err = ST_ERR_CARD; first_detail = rc; }
    }
    if (served & 2u) {
        struct cubeboot_vmc_port *pb = cbvmc_resident_port_b();
        int rb = cubeboot_vmc_port_init(pb, tops[1], CUBEBOOT_VMC_SLOT_B, CARD_PATH(1), &id, &geom);
        if (rb == 0) {
            cubeboot_vmc_port_service(pb);
            rb = pb->source == CUBEBOOT_VMC_SOURCE_VIRTUAL ? 0 : pb->last_error;
        }
        if (rb == 0) { live |= 2u; (void)cbvmc_blob_bind_b(pb); }
        else { cbvmc_osreport("cbvmc: slot B card not usable (%d)\n", rb); if (!first_err) first_err = ST_ERR_CARD; first_detail = rb; }
    }
    if (live & 2u) cbvmc_resident_port_b()->card.trace = cbvmc_resident_trace_hook();
    if (live & 1u) cbvmc_state.port.card.trace = cbvmc_resident_trace_hook();        /* diagnostics: every card command and commit goes to the SD (flash/loader build) */
    if (!live) {
        /* the blob is in low memory but nothing points at it yet: the game is untouched. Put the window back in order. */
        cbvmc_resident_sync();
        return fail(ST_ERR_CARD, first_detail);
    }

    *(volatile uint32_t *)CBVMC_DSI_VECTOR = 0x48000002u | (((uint32_t)cbvmc_vec & 0x7FFFFFFFu) & 0x03FFFFFCu);   /* ba cbvmc_vec */
    flush_range(CBVMC_DSI_VECTOR, 4);
    rc = cbvmc_words_apply((uint8_t *)0x80000000u, 0x80000000u, 0x01800000u, W, WN, &bad);
    if (rc != CBVMC_WORDS_OK) return fail(ST_ERR_WORDS, 100 + (int)bad);   /* cannot happen: checked above; the vector is live but no word moved */
    for (i = 0; i < WN; ++i) flush_range(W[i].addr, 4);
    /* T2: from here on every executable that brings up the SDK OS again is caught at its OSExceptionInit and gets the same rule set (cbvmc_t2.h) */
    t2 = cbvmc_resident_t2_arm(WN);
#ifdef CBVMC_TEST_RAM
    res_put(7, t2 == 0 ? 0x54324152u : 0u);      /* 'T2AR' */
    res_put(0, 0x43425632u); res_put(1, ST_OK); res_put(3, WN);
    res_put(4, (live & 1u) ? (uint32_t)cubeboot_vmc_free_blocks(&cbvmc_state.store, CARD_PATH(0)) : 0u);
    res_put(5, live);
    res_put(6, (live & 2u) ? (uint32_t)cubeboot_vmc_free_blocks(tops[1], CARD_PATH(1)) : 0u);
#endif
    cbvmc_osreport("cbvmc: virtual card installed: slot %s%s (%u words, multi-stage watch %s)\n", (live & 1u) ? "A" : "", (live & 2u) ? "B" : "", (unsigned)WN, t2 == 0 ? "armed" : "off");
    return ST_OK;
}

/* entry point called by cbvmc_tramp.S */
void cbvmc_install_c(void)
{
    /* Put the hooked word back to the game's own `bl target` first (the trampoline re-issues the call from the hook block, which it reads itself).
       This image and the hook block live in the arena and are gone once OSInit's ClearArena runs: an executable that runs its OSInit again from the
       same RAM would otherwise branch into zeroed memory, and the T2 re-scan of it would not find its unique EXIInit call (Dolphin T2 replay 2026-10-14:
       KEEP, why 5 = EXIINIT_CALLS). */
    const volatile uint32_t *blk = (const volatile uint32_t *)CBVMC_HOOKBLK;
    if (blk[0] == CBVMC_HOOKBLK_MAGIC) {
        uint32_t h = blk[1], tgt = blk[2];
        if (h >= 0x80003100u && h < CBVMC_B_BASE && (h & 3u) == 0 && (tgt & 3u) == 0) {
            volatile uint32_t *w = (volatile uint32_t *)h;
            uint32_t cur = *w, to = h + (uint32_t)(((int32_t)((cur & 0x03FFFFFCu) << 6)) >> 6);
            if ((cur & 0xFC000003u) == 0x48000001u && to == CBVMC_B_BASE) {
                *w = 0x48000001u | ((tgt - h) & 0x03FFFFFCu);
                flush_range(h, 4);
            }
        }
    }
    (void)install();
}
