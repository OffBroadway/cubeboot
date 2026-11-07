/* Copyright (C) 2026 Nigzel / Bjerreman. SPDX-License-Identifier: GPL-2.0-only */
/*
 * cbvmc_resident_ram: the resident image of the Dolphin TEST build (PowerPC, freestanding).  Original work.
 * Same chain as cbvmc_resident_flippy.c (write-back window with journal and read-back) but the lower store is RAM: the card in a buffer
 * the test harness provides (pointer + capacity set before OSInit, kept outside the resident region so the memory accounting stays honest)
 * and a small journal file.  Dolphin has no FlippyDrive, so this is the closest the layout, the install/resident split and the arena
 * hand-back can be exercised there.
 */
#include <string.h>

#include "cbvmc_resident.h"

/* The card file path: initialised to our own default (/cubeboot/saves, never Swiss's /swiss/saves: owner decision 2026-10-09); the loader overwrites it in its copy of this image before the game starts
   (ini key `card`), so a test card can live elsewhere without touching the owner's file. */
char cbvmc_card_path[CBVMC_CARD_PATH_MAX] = "/cubeboot/saves/MemoryCardA.USA.raw";
char cbvmc_card_path_b[CBVMC_CARD_PATH_MAX] = "/cubeboot/saves/MemoryCardB.USA.raw";      /* slot B (ini key card_b) */
const char *cbvmc_resident_card_path(unsigned slot) { return slot ? cbvmc_card_path_b : cbvmc_card_path; }

/* Which slots the virtual card may serve (bit 0 = slot A, bit 1 = slot B): written by the loader after it probed the slots. A slot with a physical card or any
   other EXI device is left alone (owner rule: a physical card always wins). Default: slot A only. */
uint32_t cbvmc_slot_allow = 1;
uint32_t cbvmc_diag[8];       /* see cbvmc_resident_flippy.c */
uint32_t cbvmc_resident_slot_allow(void) { return cbvmc_slot_allow; }
uint32_t cbvmc_card_geom = 0;         /* config.ini vmc_blocks / vmc_region, packed (cbvmc_geom_pack); 0 = 128 Mbit Windows-1252 */
uint32_t cbvmc_resident_card_geom(void) { return cbvmc_card_geom; }
#include "cbvmc_ramstore.h"
#include "cbvmc_wbmux.h"

uint8_t *cbvmc_test_card;           /* set by the harness: slot A card buffer */
uint32_t cbvmc_test_card_cap;
uint32_t cbvmc_test_card_len;      /* nonzero: the card file already exists with this size (a golden image the harness copied in) */
uint32_t *cbvmc_test_res;
uint8_t *cbvmc_test_wal;           /* journal file contents (CBVMC_WB_JHDR + CBVMC_WB_WINDOW bytes), harness memory, same reason */          /* harness result block (install image writes through it) */
uint8_t *cbvmc_test_card_b;        /* slot B: same four, all optional (NULL buffer = no slot B card) */
uint32_t cbvmc_test_card_b_cap;
uint32_t cbvmc_test_card_b_len;
uint8_t *cbvmc_test_wal_b;

#define IDLE_TICKS 8u

static struct cbvmc_ramstore card_rs[2];
static struct cubeboot_vmc_store card_store[2], lower, top[2];
static struct cbvmc_wb wb;
static struct cbvmc_wbmux mux;
static struct cbvmc_wbmux_slot slots[2];
static struct cubeboot_vmc_port port_b;
static uint8_t window[CBVMC_WB_WINDOW] __attribute__((aligned(32)));
static uint8_t hstack[CBVMC_HSTACK_BYTES] __attribute__((aligned(16)));
static uint8_t ready;

struct cubeboot_vmc_port *cbvmc_resident_port_b(void) { return &port_b; }

/* the journal files: exist after create, grow by appending, header rewritten in place; one per slot */
#define WAL_CAP (CBVMC_WB_JHDR + CBVMC_WB_WINDOW)
static uint32_t wal_n[2];
static uint8_t wal_exists[2];
static char wal_name[2][CUBEBOOT_VMC_PATH_MAX];
static uint8_t *wal_buf(unsigned i) { return i ? cbvmc_test_wal_b : cbvmc_test_wal; }

/* 0/1 = that slot's journal, -1 = not a journal */
static int wal_of(const char *p)
{
    if (wal_name[0][0] && strcmp(p, wal_name[0]) == 0) return 0;
    if (wal_name[1][0] && strcmp(p, wal_name[1]) == 0) return 1;
    return -1;
}
/* which slot's card (or its .tmp) a name belongs to */
static int card_of(const char *p)
{
    size_t n;
    unsigned i;
    for (i = 0; i < 2; ++i) {
        if (!card_rs[i].image) continue;
        n = strlen(card_rs[i].path);
        if (strncmp(p, card_rs[i].path, n) == 0 && (p[n] == 0 || strcmp(p + n, ".tmp") == 0)) return (int)i;
    }
    return -1;
}

static int l_size(void *c, const char *p, uint32_t *b)
{
    int w = wal_of(p), k;
    if (w >= 0) { if (!wal_exists[w]) return CUBEBOOT_VMC_ERR_MISSING; *b = wal_n[w]; return 0; }
    k = card_of(p);
    return k < 0 ? CUBEBOOT_VMC_ERR_MISSING : card_store[k].size(card_store[k].context, p, b);
}
static int l_read(void *c, const char *p, uint32_t o, void *b, uint32_t l)
{
    int w = wal_of(p), k;
    if (w >= 0) { if (!wal_exists[w] || o > wal_n[w] || l > wal_n[w] - o) return -1; memcpy(b, wal_buf((unsigned)w) + o, l); return 0; }
    k = card_of(p);
    return k < 0 ? -1 : card_store[k].read(card_store[k].context, p, o, b, l);
}
static int l_write(void *c, const char *p, uint32_t o, const void *b, uint32_t l)
{
    int w = wal_of(p), k;
    if (w >= 0) {
        if (!wal_exists[w] || o > wal_n[w] || o + l > WAL_CAP) return -1;
        memcpy(wal_buf((unsigned)w) + o, b, l);
        if (o + l > wal_n[w]) wal_n[w] = o + l;
        return 0;
    }
    k = card_of(p);
    return k < 0 ? -1 : card_store[k].write(card_store[k].context, p, o, b, l);
}
static int l_create(void *c, const char *p)
{
    int w = wal_of(p), k;
    if (w >= 0) { if (wal_exists[w]) return CUBEBOOT_VMC_ERR_EXISTS; wal_exists[w] = 1; wal_n[w] = 0; return 0; }
    k = card_of(p);
    return k < 0 ? -1 : card_store[k].create(card_store[k].context, p);
}
static int l_rename(void *c, const char *a, const char *b)
{
    int k = card_of(a);
    return k < 0 ? -1 : card_store[k].rename(card_store[k].context, a, b);
}
static int l_remove(void *c, const char *p)
{
    int w = wal_of(p), k;
    if (w >= 0) { wal_exists[w] = 0; wal_n[w] = 0; return 0; }
    k = card_of(p);
    return k < 0 ? -1 : card_store[k].remove(card_store[k].context, p);
}
static int l_flush(void *c, const char *p) { (void)c; (void)p; return 0; }

int cbvmc_resident_open(unsigned slot, const struct cubeboot_vmc_store **out, int *detail)
{
    const char *path = cbvmc_resident_card_path(slot);
    uint8_t *img = slot ? cbvmc_test_card_b : cbvmc_test_card;
    uint32_t cap = slot ? cbvmc_test_card_b_cap : cbvmc_test_card_cap, len = slot ? cbvmc_test_card_b_len : cbvmc_test_card_len;
    size_t n = strlen(path);
    *detail = 0;
    if (slot > 1 || n + 5 > sizeof(wal_name[0])) return CBVMC_OPEN_ERR_BIND;
    if (!img || !wal_buf(slot) || (slot == 0 && !cbvmc_test_wal)) return CBVMC_OPEN_ERR_BIND;
    memcpy(wal_name[slot], path, n);
    memcpy(wal_name[slot] + n, ".wal", 5);
    if (cbvmc_ramstore_init(&card_rs[slot], &card_store[slot], img, cap, path) != 0) return CBVMC_OPEN_ERR_BIND;
    if (len) { card_rs[slot].exists[0] = 1; card_rs[slot].size[0] = len; }
    if (!ready) {
        memset(&lower, 0, sizeof(lower));
        lower.size = l_size; lower.read = l_read; lower.write = l_write; lower.create = l_create;
        lower.rename = l_rename; lower.remove = l_remove; lower.flush = l_flush;
        if (cbvmc_wbmux_init(&mux, &wb, &lower, window, IDLE_TICKS) != 0) return CBVMC_OPEN_ERR_BIND;
        ready = 1;
    }
    if (cbvmc_wbmux_add(&mux, &slots[slot], &top[slot], path) != 0) return CBVMC_OPEN_ERR_BIND;
    *out = &top[slot];
    return CBVMC_OPEN_OK;                 /* no NOFILE check: the test installer formats a missing card */
}

void cbvmc_resident_note(const char *text) { (void)text; }
void cbvmc_resident_diag(void) { }
/* the per-executable lines stay in memory (the Dolphin harness reads them from the RAM dump) */
char cbvmc_test_stages[512];
uint32_t cbvmc_test_stages_n;
void cbvmc_resident_stage_note(const char *text, unsigned len, int restart)
{
    unsigned i;
    if (restart) cbvmc_test_stages_n = 0;
    for (i = 0; i < len && cbvmc_test_stages_n < sizeof(cbvmc_test_stages) - 1u; ++i) cbvmc_test_stages[cbvmc_test_stages_n++] = text[i];
    cbvmc_test_stages[cbvmc_test_stages_n] = 0;
}
void (*cbvmc_resident_trace_hook(void))(const struct cubeboot_vmc_card *, char, uint32_t, uint32_t, int, const void *) { return 0; }
int cbvmc_resident_prepare(unsigned slot) { return slot > 1 ? -1 : cbvmc_wbmux_prepare(&mux, &slots[slot]); }
int cbvmc_resident_recover(unsigned slot) { return slot > 1 ? -1 : cbvmc_wbmux_recover(&mux, &slots[slot]); }
void cbvmc_resident_tick(void *context) { cbvmc_wb_tick((struct cbvmc_wb *)context); }
void *cbvmc_resident_tick_context(void) { return &wb; }
int cbvmc_resident_sync(void) { return cbvmc_wbmux_sync(&mux); }
uint32_t cbvmc_resident_stack_top(void) { return (uint32_t)hstack + sizeof(hstack); }
