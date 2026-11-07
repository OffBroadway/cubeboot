/* SPDX-License-Identifier: GPL-2.0-only */
/* cbvmc_ipl_menu: the Memory Card page popup, extended.  See cbvmc_ipl_menu.h and docs/virtual_memory_card.md.  Original work.
   Freestanding; no libc beyond the few helpers below.  The card-image part runs on the host test as well (-DCBVMC_TEST_RAM). */
#include <string.h>

#include "cbvmc_ipl_menu.h"
#include "cbvmc_ipl_menu_sigs.h"
#include "cbvmc_wbstore.h"
#include "cbvmc_ipl_plan.h"                 /* CBVMC_IPLD_NOTES */

#define BLOCK CBVMC_MENU_BLOCK
#define CHUNK CBVMC_MENU_CHUNK
#define NCH   (BLOCK / CHUNK)                    /* chunks per block */
#define DIR_ENTRIES 127u
#define DIR_COUNTER 0x1ffau
#define DIR_SUM     0x1ffcu

/* ---- state ----------------------------------------------------------------------------------------------------------------------- */
static struct cbvmc_menu_fs g_fs;
static struct cbvmc_menu_card g_card[2];
static uint8_t g_chunk[CHUNK] __attribute__((aligned(32)));
static uint8_t g_chunk0[CHUNK] __attribute__((aligned(32)));    /* the first chunk of a block map while the rest streams (it carries the checksum) */
static uint8_t g_ent[64];
/* diagnostics: the step and store result the last import_one / undo failed at (0 = none), per file of the last import (up to 4) */
unsigned cbvmc_menu_step, cbvmc_menu_nfile; int cbvmc_menu_rc[4]; unsigned cbvmc_menu_steps[4];
#define FAIL(step, code) do { cbvmc_menu_step = (step); return (code); } while (0)
void cbvmc_menu_file_note(int rc, unsigned step);               /* console: the note gets one entry per imported file */

void cbvmc_menu_bind_fs(const struct cbvmc_menu_fs *fs) { g_fs = *fs; }
/* the import / imported folders live inside the card folder (cbvmc_menu_set_dir); the defaults are the built-in card folder */
static char g_import_dir[CUBEBOOT_VMC_PATH_MAX] = CBVMC_MENU_IMPORT_DIR, g_imported_dir[CUBEBOOT_VMC_PATH_MAX] = CBVMC_MENU_IMPORTED_DIR;
const char *cbvmc_menu_import_dir(void) { return g_import_dir; }
const char *cbvmc_menu_imported_dir(void) { return g_imported_dir; }
void cbvmc_menu_set_dir(const char *dir)
{
    unsigned i = 0, k; const char *p;
    for (p = dir; *p && i + 10 < sizeof(g_import_dir); ++p) g_import_dir[i++] = *p;
    k = i;
    for (p = "/import"; *p; ++p) g_import_dir[i++] = *p;
    g_import_dir[i] = 0;
    for (i = 0; i < k; ++i) g_imported_dir[i] = g_import_dir[i];
    for (p = "/imported"; *p; ++p) g_imported_dir[i++] = *p;
    g_imported_dir[i] = 0;
}
void cbvmc_menu_bind_card(unsigned slot, const struct cbvmc_menu_card *card) { if (slot < 2) g_card[slot] = *card; }

/* ---- small helpers ----------------------------------------------------------------------------------------------------------------- */
static uint16_t be16(const uint8_t *p) { return (uint16_t)((p[0] << 8) | p[1]); }
static uint32_t be32(const uint8_t *p) { return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) | ((uint32_t)p[2] << 8) | p[3]; }
static void put16(uint8_t *p, uint16_t v) { p[0] = (uint8_t)(v >> 8); p[1] = (uint8_t)v; }
static unsigned slen(const char *s) { unsigned n = 0; while (s[n]) ++n; return n; }
static void scat(char *d, unsigned cap, const char *a, const char *b, const char *c)
{
    unsigned n = 0;
    const char *s[3]; unsigned i; s[0] = a; s[1] = b; s[2] = c;
    for (i = 0; i < 3; ++i) { const char *p = s[i]; if (!p) continue; while (*p && n + 1 < cap) d[n++] = *p++; }
    d[n] = 0;
}
/* running checksum of the card's system blocks: sum of big-endian words and of their complements, 0xffff folded to 0 at the end */
struct sum { uint16_t s, i; };
static void sum_add(struct sum *k, const uint8_t *p, unsigned n) { unsigned j; for (j = 0; j + 1 < n; j += 2) { uint16_t w = be16(p + j); k->s = (uint16_t)(k->s + w); k->i = (uint16_t)(k->i + (uint16_t)(w ^ 0xffffu)); } }
static void sum_put(const struct sum *k, uint8_t *out) { uint16_t s = k->s == 0xffffu ? 0 : k->s, i = k->i == 0xffffu ? 0 : k->i; put16(out, s); put16(out + 2, i); }

static const struct cbvmc_menu_card *card_of(unsigned slot) { return slot < 2 && g_card[slot].store ? &g_card[slot] : 0; }
static int rd(const struct cbvmc_menu_card *c, uint32_t off, void *b, uint32_t n) { return c->store->read(c->store->context, c->path, off, b, n) == 0 ? 0 : CBVMC_MENU_ERR_IO; }
static int wr(const struct cbvmc_menu_card *c, uint32_t off, const void *b, uint32_t n) { return c->store->write(c->store->context, c->path, off, b, n) == 0 ? 0 : CBVMC_MENU_ERR_IO; }
static void drop_cache(const struct cbvmc_menu_card *c) { if (c->model) c->model->cache_valid = 0; }
static uint32_t card_blocks(const struct cbvmc_menu_card *c) { uint32_t sz = 0; if (c->store->size(c->store->context, c->path, &sz) != 0) return 0; return sz / BLOCK; }

/* ---- undo: the five system blocks --------------------------------------------------------------------------------------------------- */
static int undo_path(unsigned slot, char *out, unsigned cap)
{
    const struct cbvmc_menu_card *c = card_of(slot);
    if (!c) return CBVMC_MENU_ERR_SLOT;
    if (slen(c->path) + slen(CBVMC_MENU_UNDO_SUFFIX) + 1 > cap) return CBVMC_MENU_ERR_SLOT;
    scat(out, cap, c->path, CBVMC_MENU_UNDO_SUFFIX, 0);
    return 0;
}

int cbvmc_menu_undo_available(unsigned slot)
{
    char p[CUBEBOOT_VMC_PATH_MAX]; uint32_t sz = 0;
    const struct cbvmc_menu_card *c = card_of(slot);
    if (!c || !c->is_virtual || !g_fs.files || undo_path(slot, p, sizeof(p)) != 0) return 0;
    return g_fs.files->size(g_fs.files->context, p, &sz) == 0 && sz == CBVMC_MENU_SYSBLOCKS * BLOCK;
}

int cbvmc_menu_snapshot(unsigned slot)
{
    char p[CUBEBOOT_VMC_PATH_MAX]; uint32_t off; int r;
    const struct cbvmc_menu_card *c = card_of(slot);
    if (!c) return CBVMC_MENU_ERR_SLOT;
    if (!c->is_virtual) return CBVMC_MENU_ERR_PHYSICAL;
    if (!g_fs.files) return CBVMC_MENU_ERR_NOFS;
    if ((r = undo_path(slot, p, sizeof(p))) != 0) return r;
    {   uint32_t sz = 0;                                   /* overwritten in place: a remove + create pair is not reliable on every file service */
        if (g_fs.files->size(g_fs.files->context, p, &sz) != 0 || sz != CBVMC_MENU_SYSBLOCKS * BLOCK) {
            (void)g_fs.files->remove(g_fs.files->context, p);
            if (g_fs.files->create(g_fs.files->context, p) != 0) return CBVMC_MENU_ERR_IO;
        }
    }
    for (off = 0; off < CBVMC_MENU_SYSBLOCKS * BLOCK; off += CHUNK) {
        if ((r = rd(c, off, g_chunk, CHUNK)) != 0) return r;
        if (g_fs.files->write(g_fs.files->context, p, off, g_chunk, CHUNK) != 0) return CBVMC_MENU_ERR_IO;
    }
    if (g_fs.files->flush) (void)g_fs.files->flush(g_fs.files->context, p);
    return 0;
}

int cbvmc_menu_undo(unsigned slot)
{
    char p[CUBEBOOT_VMC_PATH_MAX]; uint32_t off; int r;
    const struct cbvmc_menu_card *c = card_of(slot);
    if (!c) return CBVMC_MENU_ERR_SLOT;
    if (!c->is_virtual) return CBVMC_MENU_ERR_PHYSICAL;
    if (!cbvmc_menu_undo_available(slot)) return CBVMC_MENU_ERR_NOUNDO;
    if ((r = undo_path(slot, p, sizeof(p))) != 0) return r;
    for (off = 0; off < CBVMC_MENU_SYSBLOCKS * BLOCK; off += CHUNK) {
        if (g_fs.files->read(g_fs.files->context, p, off, g_chunk, CHUNK) != 0) return CBVMC_MENU_ERR_IO;
        if ((r = wr(c, off, g_chunk, CHUNK)) != 0) return r;
    }
    if (c->store->flush) (void)c->store->flush(c->store->context, c->path);
    (void)g_fs.files->remove(g_fs.files->context, p);
    drop_cache(c);
    return 0;
}

/* ---- swap: the two card files change place (Swap Slot) --------------------------------------------------------------------------------- */
#define SWAP_SUFFIX ".swap"
static int fexists(const struct cubeboot_vmc_store *f, const char *p) { uint32_t sz; return f->size(f->context, p, &sz) == 0; }
static int fmove(const struct cubeboot_vmc_store *f, const char *a, const char *b) { return f->rename(f->context, a, b) == 0 ? 0 : CBVMC_MENU_ERR_IO; }
/* a -> t, b -> a, t -> b.  A missing file is fine (the other one just moves); a failure half-way is undone where it can be. */
static int rotate(const struct cubeboot_vmc_store *f, const char *a, const char *b, const char *t)
{
    int ea = fexists(f, a), eb = fexists(f, b);
    if (fexists(f, t)) (void)f->remove(f->context, t);
    if (!ea && !eb) return 0;
    if (ea && fmove(f, a, t) != 0) return CBVMC_MENU_ERR_IO;
    if (eb && fmove(f, b, a) != 0) { if (ea) (void)fmove(f, t, a); return CBVMC_MENU_ERR_IO; }
    if (ea && fmove(f, t, b) != 0) return CBVMC_MENU_ERR_IO;
    return 0;
}
int cbvmc_resident_slot_physical(unsigned slot);   /* cbvmc_resident_ipl.c: a physical card is in the slot now */
int cbvmc_menu_swap_available(void)
{
    const struct cbvmc_menu_card *a = card_of(0), *b = card_of(1);
    return a && b && a->is_virtual && b->is_virtual && !cbvmc_resident_slot_physical(0) && !cbvmc_resident_slot_physical(1) && g_fs.files != 0 && g_fs.files->rename != 0;
}
int cbvmc_menu_swap(void)
{
    const struct cbvmc_menu_card *a = card_of(0), *b = card_of(1);
    char ua[CUBEBOOT_VMC_PATH_MAX], ub[CUBEBOOT_VMC_PATH_MAX], t[CUBEBOOT_VMC_PATH_MAX], tu[CUBEBOOT_VMC_PATH_MAX];
    int r;
    if (!cbvmc_menu_swap_available()) return CBVMC_MENU_ERR_PHYSICAL;
    if (slen(a->path) + slen(CBVMC_MENU_UNDO_SUFFIX) + slen(SWAP_SUFFIX) + 1 > sizeof(t)) return CBVMC_MENU_ERR_SLOT;
    scat(t, sizeof(t), a->path, SWAP_SUFFIX, 0);
    if (undo_path(0, ua, sizeof(ua)) != 0 || undo_path(1, ub, sizeof(ub)) != 0) return CBVMC_MENU_ERR_SLOT;
    scat(tu, sizeof(tu), ua, SWAP_SUFFIX, 0);
    r = rotate(g_fs.files, a->path, b->path, t);
    if (r == 0) (void)rotate(g_fs.files, ua, ub, tu);                     /* the Undo snapshots belong to the cards: they move along */
    drop_cache(a); drop_cache(b);
    return r;
}

/* ---- import of a .gci ----------------------------------------------------------------------------------------------------------------- */
/* a directory or block-map block streamed: valid (checksum) and its update counter; the directory's checksum covers [0, 0x1ffc), the map's [4, 0x2000) */
static int block_valid(const struct cbvmc_menu_card *c, unsigned blk, int is_bat, uint16_t *counter)
{
    struct sum k = { 0, 0 }; uint8_t stored[4]; uint8_t calc[4]; uint32_t off; int r;
    for (off = 0; off < BLOCK; off += CHUNK) {
        if ((r = rd(c, blk * BLOCK + off, g_chunk, CHUNK)) != 0) return r;
        if (is_bat) {
            if (off == 0) { memcpy(stored, g_chunk, 4); *counter = be16(g_chunk + 4); sum_add(&k, g_chunk + 4, CHUNK - 4); }
            else sum_add(&k, g_chunk, CHUNK);
        } else {
            if (off + CHUNK == BLOCK) { memcpy(stored, g_chunk + CHUNK - 4, 4); *counter = be16(g_chunk + CHUNK - 6); sum_add(&k, g_chunk, CHUNK - 4); }
            else sum_add(&k, g_chunk, CHUNK);
        }
    }
    sum_put(&k, calc);
    return memcmp(calc, stored, 4) == 0 ? 1 : 0;
}
/* the active copy (newer counter among the valid ones) of a pair; returns the block number or ERR_CORRUPT */
static int active_of(const struct cbvmc_menu_card *c, unsigned a, unsigned b, int is_bat)
{
    uint16_t ca = 0, cb = 0; int va, vb;
    if ((va = block_valid(c, a, is_bat, &ca)) < 0) return va;
    if ((vb = block_valid(c, b, is_bat, &cb)) < 0) return vb;
    if (!va && !vb) return CBVMC_MENU_ERR_CORRUPT;
    if (va && vb) return (int16_t)(cb - ca) > 0 ? (int)b : (int)a;
    return va ? (int)a : (int)b;
}

/* the directory entry at index e of block blk */

static uint16_t rd16(const struct cbvmc_menu_card *c, uint32_t off) { uint8_t b[2]; if (rd(c, off, b, 2) != 0) return 0xffffu; return be16(b); }

/* the source of one save: a .gci (the entry, then its blocks in order) or an entry of a card image (its blocks follow the image's own block map) */
struct src { const struct cbvmc_menu_card *img; const char *path; unsigned raw; uint32_t bat, nblocks, cur_i, cur_b; };
static int src_read(struct src *s, uint32_t i, uint32_t j, void *buf)
{
    if (!s->raw) return g_fs.files->read(g_fs.files->context, s->path, 64u + i * BLOCK + j, buf, CHUNK) == 0 ? 0 : CBVMC_MENU_ERR_IO;
    while (s->cur_i < i) {                                                              /* blocks are read in order: follow the chain from the last one */
        s->cur_b = rd16(s->img, s->bat * BLOCK + 10u + 2u * (s->cur_b - CBVMC_MENU_SYSBLOCKS)); ++s->cur_i;
        if (s->cur_b < CBVMC_MENU_SYSBLOCKS || s->cur_b >= s->nblocks) return CBVMC_MENU_ERR_CORRUPT;
    }
    if (s->cur_b < CBVMC_MENU_SYSBLOCKS || s->cur_b >= s->nblocks) return CBVMC_MENU_ERR_CORRUPT;
    return rd(s->img, s->cur_b * BLOCK + j, buf, CHUNK);
}
static int import_entry(unsigned slot, struct src *s, uint32_t k);

int cbvmc_menu_import_one(unsigned slot, const char *path)
{
    const struct cbvmc_menu_card *c = card_of(slot);
    const struct cubeboot_vmc_store *f = g_fs.files;
    uint32_t sz = 0, k;
    struct src s;
    if (!c) FAIL(1, CBVMC_MENU_ERR_SLOT);
    if (!c->is_virtual) FAIL(2, CBVMC_MENU_ERR_PHYSICAL);
    if (!f) FAIL(3, CBVMC_MENU_ERR_NOFS);
    if (f->size(f->context, path, &sz) != 0 || sz < 64u + BLOCK || (sz - 64u) % BLOCK != 0) FAIL(4, CBVMC_MENU_ERR_BADGCI);
    k = (sz - 64u) / BLOCK;
    if (f->read(f->context, path, 0, g_ent, 64) != 0) FAIL(5, CBVMC_MENU_ERR_IO);
    if (g_ent[0] == 0xff || g_ent[0] == 0) FAIL(6, CBVMC_MENU_ERR_BADGCI);
    memset(&s, 0, sizeof(s)); s.path = path;
    return import_entry(slot, &s, k);
}

/* every save of a card image (.raw / .gcp: a plain dump, 64 KiB to 16 MiB) into the card, each like a .gci; *done / *skipped as for the folder */
int cbvmc_menu_import_image(unsigned slot, const char *path, unsigned *done, unsigned *skipped)
{
    const struct cbvmc_menu_card *c = card_of(slot);
    struct cbvmc_menu_card sc; struct src s;
    uint32_t sz = 0, nb, e; int dir_act, bat_act, r;
    if (done) *done = 0;
    if (skipped) *skipped = 0;
    if (!c) FAIL(1, CBVMC_MENU_ERR_SLOT);
    if (!c->is_virtual) FAIL(2, CBVMC_MENU_ERR_PHYSICAL);
    if (!g_fs.files) FAIL(3, CBVMC_MENU_ERR_NOFS);
    if (g_fs.files->size(g_fs.files->context, path, &sz) != 0 || sz % BLOCK != 0 || sz < 8u * BLOCK || sz > 2048u * BLOCK) FAIL(4, CBVMC_MENU_ERR_BADGCI);
    nb = sz / BLOCK;
    memset(&sc, 0, sizeof(sc)); sc.store = g_fs.files; sc.path = path; sc.is_virtual = 1;
    if ((dir_act = active_of(&sc, 1, 2, 0)) < 0) FAIL(30, CBVMC_MENU_ERR_CORRUPT);   /* the image's own directory and block map, the active copies */
    if ((bat_act = active_of(&sc, 3, 4, 1)) < 0) FAIL(31, CBVMC_MENU_ERR_CORRUPT);
    for (e = 0; e < DIR_ENTRIES; ++e) {
        uint32_t k, first;
        if (rd(&sc, (uint32_t)dir_act * BLOCK + e * 64u, g_ent, 64) != 0) FAIL(32, CBVMC_MENU_ERR_IO);
        if (g_ent[0] == 0xff || g_ent[0] == 0) continue;
        first = be16(g_ent + 0x36); k = be16(g_ent + 0x38);
        if (first < CBVMC_MENU_SYSBLOCKS || first >= nb || k == 0 || k > nb - CBVMC_MENU_SYSBLOCKS) { if (skipped) ++*skipped; continue; }
        memset(&s, 0, sizeof(s)); s.img = &sc; s.path = path; s.raw = 1; s.bat = (uint32_t)bat_act; s.nblocks = nb; s.cur_b = first;
        r = import_entry(slot, &s, k);
        if (cbvmc_menu_nfile < 4u) { cbvmc_menu_rc[cbvmc_menu_nfile] = r; cbvmc_menu_steps[cbvmc_menu_nfile] = cbvmc_menu_step; ++cbvmc_menu_nfile; }
        cbvmc_menu_file_note(r, cbvmc_menu_step);
        if (r == 0) { if (done) ++*done; } else if (skipped) ++*skipped;
    }
    cbvmc_menu_step = 0; return 0;
}

/* g_ent holds the save's directory entry (its first block and chain are rewritten for this card); the k data blocks come from the source */
static int import_entry(unsigned slot, struct src *s, uint32_t k)
{
    const struct cbvmc_menu_card *c = card_of(slot);
    uint32_t nblocks, off, i, j;
    int r, dir_act, bat_act, free_slot = -1;
    unsigned dir_new, bat_new, e, first = 0, last = 0, taken, map_n, cidx;
    uint16_t counter, freeb, chunk_first_free[NCH];
    struct sum sm;

    nblocks = card_blocks(c);
    if (nblocks < CBVMC_MENU_SYSBLOCKS + 1u) FAIL(7, CBVMC_MENU_ERR_CORRUPT);
    if (k > nblocks - CBVMC_MENU_SYSBLOCKS) FAIL(8, CBVMC_MENU_ERR_FULL);
    map_n = nblocks - CBVMC_MENU_SYSBLOCKS;

    /* directory: the active copy, a free entry, no duplicate (game code + maker + file name) */
    if ((dir_act = active_of(c, 1, 2, 0)) < 0) FAIL(9, dir_act);
    dir_new = dir_act == 1 ? 2u : 1u;
    for (e = 0; e < DIR_ENTRIES; ++e) {
        const uint8_t *d;
        if ((e * 64u) % CHUNK == 0 && (r = rd(c, (uint32_t)dir_act * BLOCK + e * 64u, g_chunk, CHUNK)) != 0) FAIL(10, r);   /* one chunk = 32 entries */
        d = g_chunk + (e * 64u) % CHUNK;
        if (d[0] == 0xff) { if (free_slot < 0) free_slot = (int)e; continue; }
        if (memcmp(d, g_ent, 6) == 0 && memcmp(d + 8, g_ent + 8, 32) == 0) FAIL(11, CBVMC_MENU_ERR_DUP);
    }
    if (free_slot < 0) FAIL(12, CBVMC_MENU_ERR_NOENTRY);

    /* block map, pass A over the active copy: enough free blocks, the first free entry of every chunk (the chain look-ahead of pass B) */
    if ((bat_act = active_of(c, 3, 4, 1)) < 0) FAIL(13, bat_act);
    bat_new = bat_act == 3 ? 4u : 3u;
    freeb = rd16(c, (uint32_t)bat_act * BLOCK + 6u);
    if (freeb < k) FAIL(14, CBVMC_MENU_ERR_FULL);
    taken = 0;
    for (cidx = 0; cidx < NCH; ++cidx) {
        chunk_first_free[cidx] = 0xffffu;
        if ((r = rd(c, (uint32_t)bat_act * BLOCK + cidx * CHUNK, g_chunk, CHUNK)) != 0) FAIL(15, r);
        for (i = (cidx == 0 ? 10u : 0u); i + 1 < CHUNK; i += 2) {
            unsigned idx = (cidx * CHUNK + i - 10u) / 2u;
            if (idx >= map_n) break;
            if (be16(g_chunk + i) != 0) continue;
            if (chunk_first_free[cidx] == 0xffffu) chunk_first_free[cidx] = (uint16_t)idx;
            if (taken < k) { if (taken == 0) first = CBVMC_MENU_SYSBLOCKS + idx; last = CBVMC_MENU_SYSBLOCKS + idx; ++taken; }
        }
    }
    if (taken < k) FAIL(16, CBVMC_MENU_ERR_FULL);
    /* pass B: the copy with the chain, counters and checksum; chunks 1..3 go straight to the inactive copy, chunk 0 (it carries the checksum) last */
    sm.s = 0; sm.i = 0; taken = 0;
    for (cidx = 0; cidx < NCH; ++cidx) {
        uint8_t *buf = cidx == 0 ? g_chunk0 : g_chunk;
        if ((r = rd(c, (uint32_t)bat_act * BLOCK + cidx * CHUNK, buf, CHUNK)) != 0) FAIL(17, r);
        for (i = (cidx == 0 ? 10u : 0u); i + 1 < CHUNK && taken < k; i += 2) {
            unsigned idx = (cidx * CHUNK + i - 10u) / 2u, nxt = 0xffffu, jj, c2;
            if (idx >= map_n || be16(buf + i) != 0) continue;
            ++taken;
            if (taken < k) {
                for (jj = i + 2; jj + 1 < CHUNK; jj += 2) { unsigned x = (cidx * CHUNK + jj - 10u) / 2u; if (x < map_n && be16(buf + jj) == 0) { nxt = CBVMC_MENU_SYSBLOCKS + x; break; } }
                if (nxt == 0xffffu) for (c2 = cidx + 1; c2 < NCH; ++c2) if (chunk_first_free[c2] != 0xffffu) { nxt = CBVMC_MENU_SYSBLOCKS + chunk_first_free[c2]; break; }
                if (nxt == 0xffffu) FAIL(18, CBVMC_MENU_ERR_CORRUPT);
            }
            put16(buf + i, (uint16_t)nxt);
        }
        if (cidx == 0) { counter = be16(buf + 4); put16(buf + 4, (uint16_t)(counter + 1u)); put16(buf + 6, (uint16_t)(freeb - k)); put16(buf + 8, (uint16_t)last); sum_add(&sm, buf + 4, CHUNK - 4); }
        else { sum_add(&sm, buf, CHUNK); if ((r = wr(c, bat_new * BLOCK + cidx * CHUNK, buf, CHUNK)) != 0) FAIL(19, r); }
    }
    sum_put(&sm, g_chunk0);
    if ((r = wr(c, bat_new * BLOCK, g_chunk0, CHUNK)) != 0) FAIL(20, r);

    /* the data blocks, following the chain just written (a power cut here leaves allocated blocks without an entry: a leak, never a corruption) */
    {
        unsigned b = first;
        for (i = 0; i < k; ++i) {
            if (b < CBVMC_MENU_SYSBLOCKS || b >= nblocks) FAIL(21, CBVMC_MENU_ERR_CORRUPT);
            for (j = 0; j < BLOCK; j += CHUNK) {
                if ((r = src_read(s, i, j, g_chunk)) != 0) FAIL(22, r);
                if ((r = wr(c, b * BLOCK + j, g_chunk, CHUNK)) != 0) FAIL(23, r);
            }
            b = rd16(c, bat_new * BLOCK + 10u + 2u * (b - CBVMC_MENU_SYSBLOCKS));
        }
    }
    /* the directory copy with the entry, to the inactive copy, counter + 1, checksum on the fly */
    put16(g_ent + 0x36, (uint16_t)first);
    put16(g_ent + 0x38, (uint16_t)k);
    put16(g_ent + 0x3a, 0xffffu);
    sm.s = 0; sm.i = 0;
    for (off = 0; off < BLOCK; off += CHUNK) {
        if ((r = rd(c, (uint32_t)dir_act * BLOCK + off, g_chunk, CHUNK)) != 0) FAIL(24, r);
        if ((uint32_t)free_slot * 64u >= off && (uint32_t)free_slot * 64u < off + CHUNK) memcpy(g_chunk + ((uint32_t)free_slot * 64u - off), g_ent, 64);
        if (off + CHUNK == BLOCK) {
            counter = be16(g_chunk + (DIR_COUNTER - off)); put16(g_chunk + (DIR_COUNTER - off), (uint16_t)(counter + 1u));
            sum_add(&sm, g_chunk, CHUNK - 4); sum_put(&sm, g_chunk + CHUNK - 4);
        } else sum_add(&sm, g_chunk, CHUNK);
        if ((r = wr(c, dir_new * BLOCK + off, g_chunk, CHUNK)) != 0) FAIL(25, r);
    }
    if (c->store->flush) (void)c->store->flush(c->store->context, c->path);
    drop_cache(c);
    cbvmc_menu_step = 0; return 0;
}

struct import_ctx { unsigned slot, done, skipped; char path[CUBEBOOT_VMC_PATH_MAX]; char to[CUBEBOOT_VMC_PATH_MAX]; };
static int ext3(const char *n, const char *x)
{
    unsigned l = slen(n);
    return l > 4 && (n[l - 4] == '.') && (n[l - 3] | 0x20) == x[0] && (n[l - 2] | 0x20) == x[1] && (n[l - 1] | 0x20) == x[2];
}
static int is_gci(const char *n) { return ext3(n, "gci"); }
static int is_image(const char *n) { return ext3(n, "raw") || ext3(n, "gcp"); }
static int import_cb(void *cbctx, const char *name)
{
    struct import_ctx *x = cbctx;
    if ((!is_gci(name) && !is_image(name)) || slen(g_imported_dir) + 1u + slen(name) + 1u > sizeof(x->to)) return 0;
    scat(x->path, sizeof(x->path), g_import_dir, "/", name);
    if (is_image(name)) {                                                               /* a card image: every save of it, the image moved when it could be read */
        unsigned d = 0, k = 0; int r2 = cbvmc_menu_import_image(x->slot, x->path, &d, &k);
        x->done += d; x->skipped += k;
        if (r2 == 0) { scat(x->to, sizeof(x->to), g_imported_dir, "/", name); if (fexists(g_fs.files, x->to)) (void)g_fs.files->remove(g_fs.files->context, x->to); (void)g_fs.files->rename(g_fs.files->context, x->path, x->to); }
        else { x->skipped++; cbvmc_menu_file_note(r2, cbvmc_menu_step); }
        return 0;
    }
    {   int r1 = cbvmc_menu_import_one(x->slot, x->path);
        if (cbvmc_menu_nfile < 4u) { cbvmc_menu_rc[cbvmc_menu_nfile] = r1; cbvmc_menu_steps[cbvmc_menu_nfile] = cbvmc_menu_step; ++cbvmc_menu_nfile; }
        cbvmc_menu_file_note(r1, cbvmc_menu_step);
    if (r1 == 0) {
        x->done++;
        scat(x->to, sizeof(x->to), g_imported_dir, "/", name);
        if (fexists(g_fs.files, x->to)) (void)g_fs.files->remove(g_fs.files->context, x->to);      /* an older copy of the same name in imported/ gives way */
        if (g_fs.files->rename(g_fs.files->context, x->path, x->to) != 0) { /* left in place: it will be a duplicate next time and skipped */ }
    } else x->skipped++;
    }
    return 0;
}
int cbvmc_menu_import(unsigned slot, unsigned *done, unsigned *skipped)
{
    static struct import_ctx x;
    const struct cbvmc_menu_card *c = card_of(slot);
    int r;
    if (done) *done = 0;
    if (skipped) *skipped = 0;
    if (!c) return CBVMC_MENU_ERR_SLOT;
    if (!c->is_virtual) return CBVMC_MENU_ERR_PHYSICAL;
    if (!g_fs.files || !g_fs.list) return CBVMC_MENU_ERR_NOFS;
    memset(&x, 0, sizeof(x));
    x.slot = slot;
    if (g_fs.mkdir) (void)g_fs.mkdir(g_fs.ctx, g_imported_dir);
    r = g_fs.list(g_fs.ctx, g_import_dir, import_cb, &x);
    if (done) *done = x.done;
    if (skipped) *skipped = x.skipped;
    return r < 0 ? CBVMC_MENU_ERR_IO : 0;
}

/* ====================================================================================================================================== */
/* ---- console: the patches and the handlers behind them --------------------------------------------------------------------------- */
#if (defined(__powerpc__) || defined(__PPC__)) && !defined(CBVMC_TEST_RAM)

/* small-data offsets of the page variables, USA values; every other build adds one delta (72 PAL, 128 PAL 1.2), read from the patched code itself */
#define SDA_STATE      (-31640)
#define SDA_NEXT       (-31638)
#define SDA_LATCH      (-31636)
#define SDA_CURSOR     (-31634)
#define SDA_YESNO      (-31624)
#define SDA_STH0_PTR   (-31616)
#define SDA_GLH0_PTR   (-31620)                 /* the page layout (GLH0): +8 offset of its element table, 36 bytes per element; a text element's wrap width is at +8 */
#define SDA_OP_SLOT    (-31652)
#define SDA_OP_KIND    (-31648)
#define SDA_MSG        (-31644)
#define SDA_ACTIVE     (-31672)
#define SDA_YESNO      (-31624)                 /* the Yes/No column cursor: 0 = first row, 1 = second */
#define SDA_REQ_SLOT   (-31552)
#define SDA_REQ_KIND   (-31548)
#define SDA_REQ_RES    (-31544)
#define SDA_RESULT     (-31544)                 /* the worker's result code (what MC_StateMessage polls: -21 while the operation runs, 0 = done well) */
#define OUR_FIRST_REC  64u                      /* record index of our first label (the stock blocks have 47; the two `li ...,2` become `li ...,64`) */
#define OUR_RECS       8u
#define MAX_RECS       (OUR_FIRST_REC + OUR_RECS)
#define ITEM_NONE      99u                      /* an item value the IPL's own switch ignores */

/* the stubs (cbvmc_ipl_menu_stubs.S) and what they read */
extern char cbvmc_menu_stub_guard[], cbvmc_menu_stub_choice[], cbvmc_menu_stub_confirm[], cbvmc_menu_stub_cmp_count[], cbvmc_menu_stub_count_m1[], cbvmc_menu_stub_enc_r0[], cbvmc_menu_stub_enc_r3[];
extern char cbvmc_menu_stub_yes[], cbvmc_menu_stub_msg[], cbvmc_menu_stub_dim[];
extern char cbvmc_menu_stub_cmp_mcount[], cbvmc_menu_stub_rec0_r7[], cbvmc_menu_stub_rec0_add_r3[], cbvmc_menu_stub_rec0_r4[], cbvmc_menu_stub_h_r0[], cbvmc_menu_stub_h_r29[], cbvmc_menu_stub_h_r3[], cbvmc_menu_stub_maxw[], cbvmc_menu_stub_w_r0[];
extern char cbvmc_menu_stub_place[];
/* the popup box stays between these (1/16 pixel units of the page's 640x480 layout; a line of the list is 448, the record's base 352): the top below
   the "Open" header, the bottom (the record's height from the top) above the info panel, which starts at about 5330 */
uint16_t cbvmc_menu_box_top = 1024, cbvmc_menu_box_lim = 4624;
uint8_t cbvmc_menu_box_on;                      /* 1 while the list is shown (the clamp applies); 0 on the Yes/No column, where the IPL's title row sits at the icon as stock */
uint32_t cbvmc_menu_count = 3;                  /* popup items shown */
uint32_t cbvmc_menu_rec0 = 2;                   /* first label record: the stock 2 until our string block is installed (the page measures the popup before it opens), then 64 */
uint32_t cbvmc_menu_mrec0 = 2, cbvmc_menu_mcount = 3;   /* what the measure loop sizes the box by: the list, or on the Yes/No column the one item on show (the box is as wide as what it shows) */
uint16_t cbvmc_menu_maxw;                       /* widest label of the open popup, as MC_PopupMeasureLabels measured it (the item text box is widened to it) */
uint16_t cbvmc_menu_w0;                         /* the label text element's stock wrap width, learnt at the first popup */
uint8_t  cbvmc_menu_wide;                       /* 1: this popup carries our long labels: the label element's wrap width is tripled, the text box is the widest label */
uint8_t cbvmc_menu_item_plain;                  /* set per item by site 4 (the draw loop): 1 = one of ours, drawn plain (never the Move / Copy availability fade of items 0 / 1) */
static uint8_t g_avail[8];                      /* item -> action available (an unavailable item of ours draws dim and refuses A) */
static uint8_t g_col[8];                        /* the IPL frame's two item colours (highlight, plain; RGBA) as the draw loop started */
uint32_t cbvmc_menu_wrap_val = 0xffffffffu;     /* the last value the up-wrap stub handed the IPL (diagnostics, printed by the choice note) */
static uint32_t g_msg_t0;                       /* time base when a successful Format's result message first showed; 0 = not timing */
static uint8_t g_msg_noted;                     /* the one-shot note of the message values was written */
/* the wait message of a region switch that has to generate the pair: the IPL's own message state shows record 31 ("The data has been erased.", op kind 4, code 0),
   which points at our text while it shows; the pair is generated once the text has faded in (its fader element at page + 2668 + 16 * 31, counter >= step + max) */
#define WAIT_REC       31u
#define WAIT_TEXT      "Creating Memory Card"
#define WAIT_FRAMES_MAX 120u
static uint32_t g_page;                         /* the page's data base (r29 at the guard site), 0 = unknown */
static unsigned g_wait_at;                      /* offset in g_block of the wait text */
static uint8_t g_wait_rec[8], g_wait_saved;     /* record 31 as the IPL had it, while ours is in its place */
static int g_pending_layer = -1;                /* the pair to bring up once the wait message shows, -1 = none */
static unsigned g_wait_frames;
#define MSG_AUTO_TICKS (3u * 40500000u)         /* 3 s of the 40.5 MHz time base: the message then closes as if A were pressed */
static uint32_t tb(void) { uint32_t v; __asm__ volatile("mftb %0" : "=r"(v)); return v; }
uint8_t cbvmc_menu_enc[2];                      /* per channel: 0 none, 1 -> encoding 0 (ANSI), 2 -> encoding 1 (Shift-JIS) at the next CARDFormatAsync */
static uint8_t menu_stack[20480] __attribute__((aligned(16)));     /* a layer switch re-validates the cards (16 KiB of stack) */
uint32_t cbvmc_menu_sp = (uint32_t)(uintptr_t)menu_stack + sizeof(menu_stack) - 64u;

static struct cbvmc_menu_found g_found;
const struct cbvmc_menu_found *cbvmc_menu_found(void) { return &g_found; }

enum mode { MODE_NONE = 0, MODE_FILE, MODE_EMPTY };
enum act { ACT_STOCK = 0, ACT_UNDO, ACT_IMPORT, ACT_FORMAT, ACT_SWAP, ACT_LAYER };
static unsigned g_mode, g_slot, g_file, g_nitems;
static uint8_t g_act[OUR_RECS];                 /* item -> action */
static uint32_t g_block[1024] __attribute__((aligned(32)));    /* our copy of the page's string block, extended by our records */
uint8_t cbvmc_menu_layer2;                      /* 1: the Yes/No column is the Region choice: both rows go on (stub_yes), the row picks the pair */
static unsigned g_labels2_at;                   /* offset of the two row texts of the Region choice (7 bytes each) */
static uint8_t g_yesno_rec[16];                 /* the stock records 5 and 6 (Yes / No) of our block, put back after the choice */
static uint8_t g_layer2_row[2];                 /* the pair each row of the choice stands for */
static const char *const label[] = { "Undo", "Import", "Format", "Swap", "Region: US/PAL" };   /* indexed by act - ACT_UNDO; the Region item lists as "Region" (6 characters) and titles its choice as "Region: <name>" (the name patched in place) */
#define LAYER_TITLE_LEN 14u
#define LAYER_ITEM_LEN  6u
#define LAYER_LABEL_IDX 4u
unsigned cbvmc_resident_layer(void); int cbvmc_resident_set_layer(unsigned layer); int cbvmc_resident_layer_missing(unsigned layer); unsigned cbvmc_resident_regions(void); unsigned cbvmc_resident_next_layer(unsigned layer);   /* cbvmc_resident_ipl.c */

static uint32_t r13(void) { uint32_t v; __asm__ volatile("mr %0,13" : "=r"(v)); return v; }
static int16_t *sda16(int off) { return (int16_t *)(r13() + (uint32_t)(off + g_found.sda_delta)); }
static int32_t *sda32(int off) { return (int32_t *)(r13() + (uint32_t)(off + g_found.sda_delta)); }
static uint8_t *sda8(int off) { return (uint8_t *)(r13() + (uint32_t)(off + g_found.sda_delta)); }
static void flush_word(uint32_t a) { __asm__ volatile("dcbf 0,%0 ; sync ; icbi 0,%0 ; sync ; isync" : : "r"(a & ~31u) : "memory"); }
static uint32_t rd32(uint32_t a) { return *(volatile uint32_t *)a; }
static uint32_t bl_target(uint32_t site) { uint32_t w = rd32(site), li = w & 0x03fffffcu; if (li & 0x02000000u) li -= 0x04000000u; return (w & 2u) ? li : site + li; }
static uint32_t bl_word(uint32_t site, uint32_t target) { return 0x48000001u | ((target - site) & 0x03fffffcu); }
typedef void (*fn_v_u)(uint32_t);
typedef uint32_t (*fn_u_v)(void);
typedef int (*fn_send)(uint32_t, uint32_t, uint32_t);

static void sound(uint32_t id) { if (g_found.sound) ((fn_v_u)g_found.sound)(id); }
static uint8_t *slot_rec(unsigned slot) { return (uint8_t *)((fn_u_v)g_found.slot_table)() + 20u * slot; }

/* the popup's string block: the stock one copied (its records are self-relative, so every copied record moves by the table growth), our labels appended */
static unsigned g_labels_at;                    /* offset in g_block of our first label string (0 = block not built) */
static uint32_t rec_off(const uint8_t *r) { return ((uint32_t)r[4] << 24) | ((uint32_t)r[5] << 16) | ((uint32_t)r[6] << 8) | r[7]; }
static void rec_set(uint8_t *d, unsigned kind, unsigned len, uint32_t off) { d[0] = 0; d[1] = (uint8_t)kind; put16(d + 2, (uint16_t)len); d[4] = (uint8_t)(off >> 24); d[5] = (uint8_t)(off >> 16); d[6] = (uint8_t)(off >> 8); d[7] = (uint8_t)off; }
static int build_block(const uint8_t *stock)
{
    uint8_t *blk = (uint8_t *)g_block;
    unsigned n = be16(stock + 4), i, end = 0, grow, pos;
    if (n == 0 || n > OUR_FIRST_REC) return -1;
    for (i = 0; i < n; ++i) { const uint8_t *r = stock + 8 + 8 * i; unsigned e = 8 + 8 * i + rec_off(r) + be16(r + 2) + 1; if (e > end) end = e; }
    if (end < 8 + 8 * n || end > sizeof(g_block) - 256u) return -1;
    grow = (MAX_RECS - n) * 8u;
    memcpy(blk, stock, 8);
    put16(blk + 4, (uint16_t)MAX_RECS);
    for (i = 0; i < n; ++i) { const uint8_t *r = stock + 8 + 8 * i; rec_set(blk + 8 + 8 * i, r[1], be16(r + 2), rec_off(r) + grow); blk[8 + 8 * i] = r[0]; }
    for (i = n; i < MAX_RECS; ++i) rec_set(blk + 8 + 8 * i, 9, 0, 0);
    memcpy(blk + 8 + 8 * MAX_RECS, stock + 8 + 8 * n, end - (8 + 8 * n));
    pos = 8 + 8 * MAX_RECS + (end - (8 + 8 * n));
    g_labels_at = pos;
    for (i = 0; i < sizeof(label) / sizeof(label[0]); ++i) { unsigned l = slen(label[i]); memcpy(blk + pos, label[i], l + 1); pos += l + 1; }
    g_labels2_at = pos;
    for (i = 0; i < 14; ++i) blk[pos + i] = 0;                 /* the two row texts of the Region choice, written when it opens */
    pos += 14; g_wait_at = pos; memcpy(blk + pos, WAIT_TEXT, sizeof(WAIT_TEXT));
    memcpy(g_yesno_rec, blk + 8 + 8 * 5, 16);
    return 0;
}
static void install_strings(void)
{
    uint8_t *blk = (uint8_t *)g_block;
    const uint8_t *stock = (const uint8_t *)(uintptr_t)*sda32(SDA_STH0_PTR);
    unsigned k, i;
    if (!stock) return;
    if (stock != blk) { if (build_block(stock) != 0) { g_labels_at = 0; return; } *sda32(SDA_STH0_PTR) = (int32_t)(uintptr_t)blk; }
    if (!g_labels_at) return;
    /* records 64.. are this popup's labels: a stock item points at the stock string (language kept), ours at the appended text */
    {   /* the Region label names the active layer: the three characters after "Region: " in our copy of the string */
        static const char *const stock[3] = { "US/PAL", "US/PAL", "JPN   " }, *const swiss[3] = { "USA   ", "EUR   ", "JAP   " };
        const char *t = (cbvmc_resident_regions() ? swiss : stock)[cbvmc_resident_layer() % 3u]; unsigned q;
        uint8_t *src = blk + g_labels_at; for (i = 0; i < LAYER_LABEL_IDX; ++i) src += slen((const char *)src) + 1;
        for (q = 0; q < 6; ++q) src[8 + q] = (uint8_t)t[q];
        src[6] = (uint8_t)(cbvmc_menu_layer2 ? ':' : 0);          /* the IPL draws to the terminator: "Region" in the list, "Region: <name>" as the choice's title */
    }
    for (k = 0; k < g_nitems && k < OUR_RECS; ++k) {
        uint8_t *d = blk + 8 + 8 * (OUR_FIRST_REC + k);
        const uint8_t *src;
        if (g_act[k] == ACT_STOCK) { const uint8_t *r = blk + 8 + 8 * (2 + k); src = r + rec_off(r); }
        else { src = blk + g_labels_at; for (i = 0; i < (unsigned)(g_act[k] - ACT_UNDO); ++i) src += slen((const char *)src) + 1; }
        rec_set(d, 9, g_act[k] == ACT_LAYER ? (cbvmc_menu_layer2 ? LAYER_TITLE_LEN : LAYER_ITEM_LEN) : slen((const char *)src), (uint32_t)(src - d));
    }
    cbvmc_menu_rec0 = OUR_FIRST_REC; cbvmc_menu_mrec0 = OUR_FIRST_REC;
}

static int slot_virtual(unsigned slot) { return slot < 2 && g_card[slot].store && g_card[slot].is_virtual && !cbvmc_resident_slot_physical(slot); }
static void wait_rec_restore(void) { if (g_wait_saved) { memcpy((uint8_t *)g_block + 8 + 8 * WAIT_REC, g_wait_rec, 8); g_wait_saved = 0; } }
static void wait_rec_show(void)
{
    uint8_t *d = (uint8_t *)g_block + 8 + 8 * WAIT_REC;
    if (!g_wait_saved) { memcpy(g_wait_rec, d, 8); g_wait_saved = 1; }
    rec_set(d, g_wait_rec[1], sizeof(WAIT_TEXT) - 1u, (uint32_t)((uint8_t *)g_block + g_wait_at - d));
}
static int wait_text_shown(void)                /* the fader of text element 31 has reached its top (or the frame budget is spent) */
{
    if (++g_wait_frames >= WAIT_FRAMES_MAX) return 1;
    if (!g_page || g_wait_frames < 3u) return 0;
    { const int16_t *e = (const int16_t *)(uintptr_t)(g_page + 2668u + 16u * WAIT_REC); return e[0] >= (int16_t)(e[1] + e[2]); }
}

/* the Region choice: records 5 and 6 (the Yes/No column) name the pairs while it is open, the item's record reads "Region: <name>" as the title.
   Stock mode: the rows are US/PAL and JPN, the current one preselected (returned).  Swiss mode: the rows are the two other pairs. */
static const char *const layer_name_stock[3] = { "US/PAL", "US/PAL", "JPN" }, *const layer_name_swiss[3] = { "USA", "EUR", "JAP" };
static unsigned layer_set(int on)
{
    uint8_t *blk = (uint8_t *)g_block; unsigned k, pre = 0;
    if (!g_labels_at || (const uint8_t *)(uintptr_t)*sda32(SDA_STH0_PTR) != blk) { cbvmc_menu_layer2 = 0; return 0; }
    if (on) {
        unsigned cur = cbvmc_resident_layer() % 3u, swiss = cbvmc_resident_regions() != 0;
        const char *const *nm = swiss ? layer_name_swiss : layer_name_stock;
        if (swiss) { g_layer2_row[0] = (uint8_t)((cur + 1u) % 3u); g_layer2_row[1] = (uint8_t)((cur + 2u) % 3u); }
        else { g_layer2_row[0] = CBVMC_LAYER_USA; g_layer2_row[1] = CBVMC_LAYER_JAP; pre = cur == CBVMC_LAYER_JAP ? 1u : 0u; }
        for (k = 0; k < 2; ++k) { uint8_t *d = blk + 8 + 8 * (5 + k), *t = blk + g_labels2_at + 7 * k; unsigned l = slen(nm[g_layer2_row[k]]); memcpy(t, nm[g_layer2_row[k]], l + 1); rec_set(d, 9, l, (uint32_t)(t - d)); }
    } else memcpy(blk + 8 + 8 * 5, g_yesno_rec, 16);
    cbvmc_menu_layer2 = (uint8_t)on;
    for (k = 0; k < g_nitems && k < OUR_RECS; ++k) if (g_act[k] == ACT_LAYER) { uint8_t *d = blk + 8 + 8 * (OUR_FIRST_REC + k); put16(d + 2, (uint16_t)(on ? LAYER_TITLE_LEN : LAYER_ITEM_LEN)); }
    {   uint8_t *src = blk + g_labels_at; for (k = 0; k < LAYER_LABEL_IDX; ++k) src += slen((const char *)src) + 1; src[6] = (uint8_t)(on ? ':' : 0); }
    return pre;
}
/* the last popup action and its result, one line in /cubeboot/cbvmc_menu_last.txt (support and debugging; numbers only) */
#define MENU_LAST_PATH "/cubeboot/cbvmc_menu_last.txt"
static char g_note[512]; static unsigned g_note_n; static char g_note_last[512];
const struct cbvmc_wb_stats *cbvmc_resident_wb_stats(void);      /* cbvmc_resident_ipl.c */
extern uint32_t cbvmc_ripl_diag;                                  /* cbvmc_resident_ipl.c: the vmc_diag mask (the note goes to the SD only with CBVMC_IPLD_NOTES) */
const char *cbvmc_menu_last_note(void) { return g_note_last; }
static void note_s(const char *t) { while (*t && g_note_n + 1 < sizeof(g_note)) g_note[g_note_n++] = *t++; }
static void note_d(int v) { char b[12]; unsigned i = 0, u = v < 0 ? (unsigned)(-v) : (unsigned)v; if (v < 0) note_s("-"); do { b[i++] = (char)('0' + u % 10u); u /= 10u; } while (u); while (i) { char t[2]; t[0] = b[--i]; t[1] = 0; note_s(t); } }
static void note_flush(void)
{
    const struct cubeboot_vmc_store *f = g_fs.files;
    {   const struct cbvmc_wb_stats *w = cbvmc_resident_wb_stats();
        note_s(" |wc "); note_d((int)w->commits); note_s(" wf "); note_d((int)w->commit_failures); note_s(" vm "); note_d((int)w->verify_mismatches); note_s(" lw "); note_d((int)w->lower_writes); note_s(" lr "); note_d((int)w->lower_reads); note_s("| "); }
    unsigned keep;
    g_note[g_note_n] = 0; memcpy(g_note_last, g_note, g_note_n + 1); keep = g_note_n;
    if (!f || !(cbvmc_ripl_diag & CBVMC_IPLD_NOTES)) return;
    note_s("\n");
    while (g_note_n < sizeof(g_note) - 1u) g_note[g_note_n++] = ' ';    /* a fixed-size record, overwritten in place */
    g_note[g_note_n - 1] = '\n';
    {   uint32_t sz = 0;
        if (f->size(f->context, MENU_LAST_PATH, &sz) != 0 || sz != g_note_n) { (void)f->remove(f->context, MENU_LAST_PATH); (void)f->create(f->context, MENU_LAST_PATH); }
        (void)f->write(f->context, MENU_LAST_PATH, 0, g_note, g_note_n);
    }
    if (f->flush) (void)f->flush(f->context, MENU_LAST_PATH);
    g_note_n = keep;                                                     /* the text stays: a later flush appends to it */
}
static void note_begin(void) { g_note_n = 0; }
void cbvmc_menu_file_note(int rc, unsigned step) { note_s(" f"); note_d((int)cbvmc_menu_nfile); note_s(":"); note_d(rc); note_s("/"); note_d((int)step); note_flush(); }

/* the popup labels are one text element of the page layout (the kind byte of the stock records); its width is the wrap limit in the measurer and the
   renderer alike, so our popup gets three times the stock width (the box itself is sized from the measured labels) */
static void set_label_width(void)
{
    uint8_t *layout = (uint8_t *)(uintptr_t)*sda32(SDA_GLH0_PTR);
    const uint8_t *sth = (const uint8_t *)(uintptr_t)*sda32(SDA_STH0_PTR);
    uint16_t *w; unsigned kind;
    if (!layout || !sth || layout[0] != 'G' || layout[1] != 'L' || layout[2] != 'H' || layout[3] != '0') return;
    kind = sth[8 + 8 * 2 + 1];
    w = (uint16_t *)(layout + be32(layout + 8) + kind * 36u + 8u);
    if (!cbvmc_menu_w0) cbvmc_menu_w0 = *w;
    *w = (uint16_t)(cbvmc_menu_wide ? 3u * cbvmc_menu_w0 : cbvmc_menu_w0);
}
static void build_items(void)
{
    unsigned n = 0;
    /* owner order 2026-11-07: Move, Copy, Erase (on a save), Undo, Region, Swap, Import, Format; every item of ours is always listed, dim when unavailable */
    if (g_mode == MODE_FILE) { g_avail[n] = 1; g_act[n++] = ACT_STOCK; g_avail[n] = 1; g_act[n++] = ACT_STOCK; g_avail[n] = 1; g_act[n++] = ACT_STOCK; }
    g_avail[n] = (uint8_t)cbvmc_menu_undo_available(g_slot); g_act[n++] = ACT_UNDO;
    g_avail[n] = (uint8_t)(slot_virtual(0) || slot_virtual(1)); g_act[n++] = ACT_LAYER;    /* the card pair (US/PAL | JPN, or USA | EUR | JAP): a choice on the Yes/No column */
    g_avail[n] = (uint8_t)cbvmc_menu_swap_available();        g_act[n++] = ACT_SWAP;
    g_avail[n] = (uint8_t)slot_virtual(g_slot);               g_act[n++] = ACT_IMPORT;    /* .gci files and card images of the import folder */
    g_avail[n] = 1;                                           g_act[n++] = ACT_FORMAT;    /* the IPL's own Yes/No; the encoding follows the card's region */
    g_nitems = n;
    cbvmc_menu_count = n; cbvmc_menu_mcount = n;
    cbvmc_menu_wide = (uint8_t)(g_mode == MODE_EMPTY || n > 3);                        /* our labels are longer than Move / Copy / Erase */
}

/* back to browsing, and the slot listed again through the IPL's own worker: unmount, then mount + list (what it does for a re-inserted card) */
static void close_and_refresh(unsigned slot)
{
    uint8_t *rec = slot_rec(slot);
    ((fn_v_u)g_found.close_popup)(1);
    *sda8(SDA_REQ_SLOT) = (uint8_t)slot; *sda32(SDA_REQ_KIND) = 0; *sda32(SDA_REQ_RES) = -21;
    {
        uint32_t q = ((fn_u_v)g_found.queue)();
        ((fn_send)g_found.send_message)(q, (slot << 16) | 9u, 0);
        ((fn_send)g_found.send_message)(q, (slot << 16) | 0u, 0);
    }
    *(int16_t *)(rec + 2) = 3; rec[1] = 0;
}

static void do_format(unsigned slot, unsigned enc_override)
{
    if (slot_virtual(slot)) (void)cbvmc_menu_snapshot(slot);
    cbvmc_menu_enc[slot] = (uint8_t)enc_override;
    sound(8);
    ((fn_v_u)g_found.req_format)(slot);
    {   /* after the format the worker lists the card again (the stock format dialog does the same) */
        uint32_t q = ((fn_u_v)g_found.queue)();
        ((fn_send)g_found.send_message)(q, (slot << 16) | 9u, 0);
        ((fn_send)g_found.send_message)(q, (slot << 16) | 0u, 0);
    }
    *sda16(SDA_NEXT) = 5; *sda32(SDA_OP_SLOT) = (int32_t)slot; *sda32(SDA_OP_KIND) = 1; *sda32(SDA_MSG) = -21;
    g_msg_t0 = 0; g_msg_noted = 0;
}

int cbvmc_resident_sync(void);                  /* cbvmc_resident_ipl.c: commit the dirty window block */
void cbvmc_resident_window_discard(void);       /* cbvmc_resident_ipl.c: forget the window (the card file was written directly) */
int cbvmc_resident_cards_swapped(void);         /* cbvmc_resident_ipl.c: each card model takes the file now behind its path */
void cbvmc_resident_replug(void);               /* cbvmc_resident_ipl.c: announce both virtual cards again like a hot-plug */
static void dispatch_body(uint32_t *regs, unsigned site);
/* regs: r0..r31 as saved by the trampoline; site: which stub */
void cbvmc_menu_dispatch(uint32_t *regs, unsigned site)
{
    dispatch_body(regs, site);                  /* each Flippy transaction locks interrupts by itself (cubeboot_flippy_di.c hw_lock); the whole action must not */
}
static void dispatch_body(uint32_t *regs, unsigned site)
{
    switch (site) {
    case 0: {                                                   /* guard: r7 slot, r3 = slot*1524 + fileNo*12, r0 = present */
        unsigned slot = regs[7] & 1u;
        g_slot = slot; g_file = (regs[3] - slot * 1524u) / 12u;
        g_page = regs[29]; wait_rec_restore(); g_pending_layer = -1;
        g_mode = regs[0] ? MODE_FILE : MODE_EMPTY;
        cbvmc_menu_layer2 = 0; cbvmc_menu_box_on = 1;
        g_msg_t0 = 0; g_msg_noted = 0;
        build_items();
        set_label_width();
        install_strings();
        if (!regs[0] && g_nitems) regs[0] = 1;                    /* the popup opens on the empty slot too */
        {   unsigned k = 0;                                           /* the cursor starts on the first available item, not where it last was */
            while (k + 1 < g_nitems && !g_avail[k]) ++k;
            *sda16(SDA_CURSOR) = (int16_t)k; *sda16(SDA_LATCH) = (int16_t)k; }
        break; }
    case 1: {                                                   /* choice (A on an item): r9 item, r8 slot, r30 fileNo */
        unsigned item = regs[9];
        if (item >= g_nitems) break;
        cbvmc_menu_box_on = 0; cbvmc_menu_mcount = 1; if (cbvmc_menu_rec0 == OUR_FIRST_REC) cbvmc_menu_mrec0 = OUR_FIRST_REC + item;   /* the Yes/No column: stock placement, the box as wide as the item on show (the draw site puts the list back) */
        if (g_act[item] == ACT_STOCK) break;
        if (!g_avail[item]) { sound(13); regs[9] = ITEM_NONE; break; }                  /* dim item: the IPL's refusal sound, the popup stays */
        sound(8);
        *sda16(SDA_NEXT) = 3;                                     /* the IPL's own Yes/No column; No is preselected by the IPL */
        if (g_act[item] == ACT_LAYER) regs[0] = (uint32_t)layer_set(1);   /* the column names the pairs; r0 is the row the IPL stores as the cursor */
        regs[9] = ITEM_NONE;
        break; }
    case 2: {                                                   /* confirm (Yes): r0 item, r30 slot, r28 fileNo */
        unsigned item = regs[0], slot = regs[30] & 1u;
        if (item >= g_nitems) break;
        if (g_act[item] == ACT_STOCK) {                                                   /* Undo per slot: Move changes both cards, Copy the other one, Erase this one */
            if (g_mode == MODE_FILE) {
                if (item != 1 && slot_virtual(slot)) (void)cbvmc_menu_snapshot(slot);
                if (item != 2 && slot_virtual(slot ^ 1u)) (void)cbvmc_menu_snapshot(slot ^ 1u);
            }
            break; }
        note_begin(); note_s("confirm item "); note_d((int)item); note_s(" act "); note_d((int)g_act[item]); note_s(" slot "); note_d((int)slot);
        if (g_act[item] == ACT_UNDO || g_act[item] == ACT_IMPORT || g_act[item] == ACT_SWAP || g_act[item] == ACT_LAYER) { int q0 = cbvmc_resident_sync(); cbvmc_resident_window_discard(); note_s(" presync "); note_d(q0); }
        note_flush();
        if (g_act[item] != ACT_LAYER) layer_set(0);
        switch (g_act[item]) {
        case ACT_UNDO:   { int r = cbvmc_menu_undo(slot); cbvmc_resident_window_discard(); note_s(" undo "); note_d(r); sound(8); close_and_refresh(slot); break; }
        case ACT_IMPORT: { unsigned d = 0, k = 0; int r0, r, q = 0; uint32_t t0 = tb(); cbvmc_menu_nfile = 0; r0 = cbvmc_menu_snapshot(slot); note_s(" snapshot "); note_d(r0); note_flush(); r = cbvmc_menu_import(slot, &d, &k); cbvmc_resident_window_discard(); note_s(" import "); note_d(r); note_s(" done "); note_d((int)d); note_s(" skipped "); note_d((int)k); { unsigned q2; note_s(" files"); for (q2 = 0; q2 < cbvmc_menu_nfile; ++q2) { note_s(" rc "); note_d(cbvmc_menu_rc[q2]); note_s(" step "); note_d((int)cbvmc_menu_steps[q2]); } } note_s(" sync "); note_d(q); note_s(" ms "); note_d((int)((tb() - t0) / 40500u)); sound(8); close_and_refresh(slot); break; }
        case ACT_FORMAT: { unsigned enc = cbvmc_resident_layer() == CBVMC_LAYER_JAP ? 2u : 1u; note_s(" format enc "); note_d((int)enc); do_format(slot, enc); break; }   /* the text encoding of the card's region: Shift-JIS for the JAP pair, Windows-1252 otherwise */
        case ACT_LAYER: {                                           /* the chosen card pair: files opened or generated, models re-mounted, both cards re-plugged */
            unsigned nl = g_layer2_row[*sda16(SDA_YESNO) & 1]; int r = 0;
            layer_set(0);
            if (nl == cbvmc_resident_layer()) { sound(8); ((fn_v_u)g_found.close_popup)(1); break; }   /* the current one chosen: nothing to do */
            if (!cbvmc_resident_layer_missing(nl)) {                                      /* both files there: the switch is quick, no wait message */
                r = cbvmc_resident_set_layer(nl); note_s(" layer "); note_d((int)nl); note_s(" rc "); note_d(r);
                sound(8); ((fn_v_u)g_found.close_popup)(1); cbvmc_resident_replug(); break; }
            g_pending_layer = (int)nl; g_wait_frames = 0; wait_rec_show();   /* "Creating Memory Card" through the IPL's message state; the switch runs from site 3 once it shows */
            sound(8); *sda16(SDA_NEXT) = 5; *sda32(SDA_OP_SLOT) = (int32_t)slot; *sda32(SDA_OP_KIND) = 4; *sda32(SDA_MSG) = 0; *sda32(SDA_RESULT) = 0;
            g_msg_t0 = 0; g_msg_noted = 0; break; }
        case ACT_SWAP: {                                            /* the files change place; both card models take their new file, the IPL re-mounts both cards (re-plug) */
            int r = cbvmc_menu_swap(), r2; cbvmc_resident_window_discard(); r2 = cbvmc_resident_cards_swapped(); note_s(" swap "); note_d(r); note_s(" models "); note_d(r2);
            sound(8); ((fn_v_u)g_found.close_popup)(1); cbvmc_resident_replug(); break; }
        default: break;
        }
        note_flush();
        regs[0] = ITEM_NONE;
        break; }
    case 3: {                                                   /* the result message (state 5), once per frame after the worker's result is in: r0 = the pad's pressed bits */
        uint32_t now; int op = *sda32(SDA_OP_KIND), res = *sda32(SDA_RESULT);
        if (g_pending_layer >= 0) {                                                         /* the wait message of a region switch: no A / B until the pair is up */
            regs[0] &= ~0x300u;
            if (wait_text_shown()) {
                unsigned nl = (unsigned)g_pending_layer; int r; g_pending_layer = -1;
                note_begin(); r = cbvmc_resident_set_layer(nl); note_s(" layer "); note_d((int)nl); note_s(" rc "); note_d(r); note_s(" frames "); note_d((int)g_wait_frames); note_flush();
                *sda32(SDA_OP_KIND) = 0; *sda32(SDA_MSG) = 0; *sda16(SDA_YESNO) = 1;      /* as the IPL closes a message on A */
                ((fn_v_u)g_found.close_popup)(1); cbvmc_resident_replug();
            }
            break; }
        if (!g_msg_noted) { g_msg_noted = 1; note_s(" msg op "); note_d(op); note_s(" res "); note_d(res); note_s(" code "); note_d(*sda32(SDA_MSG)); note_s(" pad "); note_d((int)regs[0]); note_flush(); }
        if (op != 8 || *sda32(SDA_MSG) != 0) { g_msg_t0 = 0; break; }                       /* "The Memory Card has been formatted" shows as op 8 / code 0 (seen on the console; the IPL rewrites the op kind after the format); anything else waits for A / B */
        now = tb();
        if (!g_msg_t0) { g_msg_t0 = now ? now : 1u; break; }
        if (now - g_msg_t0 >= MSG_AUTO_TICKS) { regs[0] |= 0x100u; g_msg_t0 = 0; g_msg_noted = 0; }     /* as if A were pressed: the IPL closes the popup and lists the slot */
        break; }
    case 4: {                                                   /* the draw loop, once per item (MC_PopupDrawItems+0x22c): r31 item; the IPL frame holds the item colours at +92 (highlight) and +96 (plain), RGBA */
        unsigned item = regs[31] & 0xffffu, k; uint8_t *fr = (uint8_t *)(uintptr_t)(regs[1] + 0x100u);
        if (!cbvmc_menu_box_on && *sda16(SDA_STATE) == 2 && *sda16(SDA_NEXT) == 2) { cbvmc_menu_box_on = 1; cbvmc_menu_mcount = cbvmc_menu_count; cbvmc_menu_mrec0 = cbvmc_menu_rec0; if (cbvmc_menu_layer2) layer_set(0); }   /* B on the column (or a refused item): the page is in the list state and stays (the choice frame itself still draws the list, with next = 3): clamped, full width, "Region" */
        if (item == 0) memcpy(g_col, fr + 92, 8); else memcpy(fr + 92, g_col, 8);     /* the frame's colours are rebuilt by the IPL each call: item 0 sees the originals */
        cbvmc_menu_item_plain = (uint8_t)(item < g_nitems && g_act[item] != ACT_STOCK);
        if (cbvmc_menu_item_plain && !g_avail[item]) for (k = 0; k < 8; ++k) if ((k & 3u) != 3u) fr[92 + k] = (uint8_t)(g_col[k] / 2u);   /* dim: half the RGB, alpha kept */
        break; }
    default: break;
    }
}

/* ---- install ------------------------------------------------------------------------------------------------------------------------- */
static int find_once(uint32_t lo, uint32_t hi, const struct cbvmc_menu_sig *s, uint32_t *out)
{
    uint32_t a, hit = 0; unsigned n = 0, i;
    for (a = lo; a + 4 * s->n <= hi; a += 4) {
        if ((rd32(a) & s->mask[0]) != s->val[0]) continue;
        for (i = 1; i < s->n; ++i) if ((rd32(a + 4 * i) & s->mask[i]) != s->val[i]) break;
        if (i == s->n) { hit = a; if (++n > 1) return 0; }
    }
    if (n == 1) *out = hit;
    return n == 1;
}
struct patch { uint32_t site, expect, word; };

int cbvmc_menu_install(uint32_t lo, uint32_t hi, const char **why)
{
    uint32_t f[CBVMC_SIG_COUNT];
    struct patch p[32]; unsigned np = 0, i;
    int16_t imm;
    *why = "";
    for (i = 0; i < CBVMC_SIG_COUNT; ++i) if (!find_once(lo, hi, &cbvmc_menu_sigs[i], &f[i])) { *why = cbvmc_menu_sigs[i].name; return -1; }
    g_found.browse = f[CBVMC_SIG_MC_STATEBROWSE]; g_found.measure = f[CBVMC_SIG_MC_POPUPMEASURELABELS]; g_found.draw = f[CBVMC_SIG_MC_POPUPDRAWITEMS];
    g_found.popup = f[CBVMC_SIG_MC_STATEPOPUP]; g_found.confirm = f[CBVMC_SIG_MC_STATECONFIRM]; g_found.format_async = f[CBVMC_SIG_CARDFORMATASYNC];
    g_found.req_format = f[CBVMC_SIG_MC_REQFORMAT]; g_found.close_popup = f[CBVMC_SIG_MC_CLOSEPOPUP];
    g_found.message = g_found.confirm + 0x610;                                  /* MC_StateMessage follows MC_StateConfirm and MC_StateFormatDialog at the same distance in every build */
    if (rd32(g_found.message) != 0x7c0802a6u || rd32(g_found.message + 0x2c) != 0x2c03ffebu || rd32(g_found.message + 0xb0) != 0xa003000cu) { *why = "message"; return -2; }
    g_found.pdraw = g_found.measure + 0x70c;                                    /* MC_PopupDraw, the only caller of MC_PopupMeasureLabels, at the same distance in every build */
    if (rd32(g_found.pdraw + 0xd4) != bl_word(g_found.pdraw + 0xd4, g_found.measure) || rd32(g_found.pdraw) != 0x7c0802a6u) { *why = "pdraw"; return -2; }
    /* the words at the sites must be the stock ones */
#define SITE(addr, ex, w) do { if (rd32(addr) != (ex)) { *why = "site"; return -2; } p[np].site = (addr); p[np].expect = (ex); p[np].word = (w); ++np; } while (0)
    SITE(g_found.browse + 0x5b4, 0x28000000u, bl_word(g_found.browse + 0x5b4, (uint32_t)(uintptr_t)cbvmc_menu_stub_guard));
    SITE(g_found.measure + 0x4, 0x38e00002u, bl_word(g_found.measure + 0x4, (uint32_t)(uintptr_t)cbvmc_menu_stub_rec0_r7));
    SITE(g_found.measure + 0x40, 0x38630002u, bl_word(g_found.measure + 0x40, (uint32_t)(uintptr_t)cbvmc_menu_stub_rec0_add_r3));   /* the string record: the stock loop takes the kind from record r7+i but the text from record 2+i */
    SITE(g_found.measure + 0x88, 0x28000003u, bl_word(g_found.measure + 0x88, (uint32_t)(uintptr_t)cbvmc_menu_stub_cmp_mcount));   /* the measure loop: the list, or the one item on show */
    SITE(g_found.draw + 0x1e8, 0x38800002u, bl_word(g_found.draw + 0x1e8, (uint32_t)(uintptr_t)cbvmc_menu_stub_rec0_r4));
    SITE(g_found.draw + 0x3f0, 0x28000003u, bl_word(g_found.draw + 0x3f0, (uint32_t)(uintptr_t)cbvmc_menu_stub_cmp_count));
    /* the popup box: its height is the base plus (items - 1) lines (stock: line height << 5 for 3 items; the half height << 4 of the Yes/No box stays stock, two rows), the item text box is as wide as the widest label */
    SITE(g_found.measure + 0xa4, 0x54002834u, bl_word(g_found.measure + 0xa4, (uint32_t)(uintptr_t)cbvmc_menu_stub_h_r0));
    SITE(g_found.measure + 0xd0, 0x541d2c34u, bl_word(g_found.measure + 0xd0, (uint32_t)(uintptr_t)cbvmc_menu_stub_h_r29));
    SITE(g_found.measure + 0x100, 0x54632834u, bl_word(g_found.measure + 0x100, (uint32_t)(uintptr_t)cbvmc_menu_stub_h_r3));
    SITE(g_found.measure + 0x150, 0xb3fa0000u, bl_word(g_found.measure + 0x150, (uint32_t)(uintptr_t)cbvmc_menu_stub_maxw));
    SITE(g_found.draw + 0xdc, 0xa0180018u, bl_word(g_found.draw + 0xdc, (uint32_t)(uintptr_t)cbvmc_menu_stub_w_r0));
    SITE(g_found.draw + 0xf4, 0xa0180018u, bl_word(g_found.draw + 0xf4, (uint32_t)(uintptr_t)cbvmc_menu_stub_w_r0));
    SITE(g_found.pdraw + 0x174, 0x8bfb0003u, bl_word(g_found.pdraw + 0x174, (uint32_t)(uintptr_t)cbvmc_menu_stub_place));   /* the box top: kept above the info panel and below the header */
    SITE(g_found.popup + 0x50, 0x38000002u, bl_word(g_found.popup + 0x50, (uint32_t)(uintptr_t)cbvmc_menu_stub_count_m1));
    SITE(g_found.popup + 0x7c, 0x2c000003u, bl_word(g_found.popup + 0x7c, (uint32_t)(uintptr_t)cbvmc_menu_stub_cmp_count));
    SITE(g_found.popup + 0x108, 0x2c090001u, bl_word(g_found.popup + 0x108, (uint32_t)(uintptr_t)cbvmc_menu_stub_choice));
    SITE(g_found.confirm + 0x8c, 0x2c000000u, bl_word(g_found.confirm + 0x8c, (uint32_t)(uintptr_t)cbvmc_menu_stub_yes));       /* the Yes test: both rows go on while the Region choice is open */
    SITE(g_found.confirm + 0xe8, 0x2c000001u, bl_word(g_found.confirm + 0xe8, (uint32_t)(uintptr_t)cbvmc_menu_stub_confirm));
    SITE(g_found.message + 0xb8, 0x540005efu, bl_word(g_found.message + 0xb8, (uint32_t)(uintptr_t)cbvmc_menu_stub_msg));         /* the A test of the result message: a successful Format's closes by itself */
    SITE(g_found.draw + 0x22c, 0x281f0001u, bl_word(g_found.draw + 0x22c, (uint32_t)(uintptr_t)cbvmc_menu_stub_dim));            /* site 4: our items draw plain, dim when unavailable */
    if (rd32(g_found.format_async + 0x50) == 0xb07b0024u) {                     /* PAL family: one store of r3 */
        g_found.enc_variant = 2;
        SITE(g_found.format_async + 0x50, 0xb07b0024u, bl_word(g_found.format_async + 0x50, (uint32_t)(uintptr_t)cbvmc_menu_stub_enc_r3));
    } else {                                                                    /* USA/JAP: two stores of r0 */
        g_found.enc_variant = 1;
        SITE(g_found.format_async + 0x70, 0xb01b0024u, bl_word(g_found.format_async + 0x70, (uint32_t)(uintptr_t)cbvmc_menu_stub_enc_r0));
        SITE(g_found.format_async + 0x8c, 0xb01b0024u, bl_word(g_found.format_async + 0x8c, (uint32_t)(uintptr_t)cbvmc_menu_stub_enc_r0));
    }
#undef SITE
    /* the helpers the handlers call, taken from the page's own calls; the small-data delta from the `sth r0,next(r13)` after the guard */
    if ((rd32(g_found.browse + 0x5c0) & 0xffff0000u) != 0xb00d0000u) { *why = "sda"; return -3; }
    imm = (int16_t)(rd32(g_found.browse + 0x5c0) & 0xffffu);
    g_found.sda_delta = (int32_t)imm - SDA_NEXT;
    if ((rd32(g_found.browse + 0x5c8) >> 26) != 18u || (rd32(g_found.close_popup + 0x18) >> 26) != 18u || (rd32(g_found.req_format + 0x30) >> 26) != 18u || (rd32(g_found.req_format + 0x3c) >> 26) != 18u) { *why = "calls"; return -4; }
    g_found.sound = bl_target(g_found.browse + 0x5c8);
    g_found.slot_table = bl_target(g_found.close_popup + 0x18);
    g_found.queue = bl_target(g_found.req_format + 0x30);
    g_found.send_message = bl_target(g_found.req_format + 0x3c);
    cbvmc_menu_sp = (uint32_t)(uintptr_t)menu_stack + sizeof(menu_stack) - 64u;
    cbvmc_menu_count = 3; cbvmc_menu_rec0 = 2; cbvmc_menu_mcount = 3; cbvmc_menu_mrec0 = 2; cbvmc_menu_layer2 = 0; cbvmc_menu_item_plain = 0; g_msg_t0 = 0; cbvmc_menu_enc[0] = cbvmc_menu_enc[1] = 0; g_mode = MODE_NONE; cbvmc_menu_wide = 0; cbvmc_menu_w0 = 0;
    for (i = 0; i < np; ++i) { *(volatile uint32_t *)p[i].site = p[i].word; flush_word(p[i].site); }
    return (int)np;
}

#else
void cbvmc_menu_file_note(int rc, unsigned step) { (void)rc; (void)step; }
int cbvmc_menu_install(uint32_t lo, uint32_t hi, const char **why) { (void)lo; (void)hi; *why = "host"; return -100; }
static struct cbvmc_menu_found g_found;
const struct cbvmc_menu_found *cbvmc_menu_found(void) { return &g_found; }
#endif
