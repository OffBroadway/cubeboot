/* Copyright (C) 2026 Nigzel / Bjerreman. SPDX-License-Identifier: GPL-3.0-or-later */
/* Write-coalescing card store, see cbvmc_wbstore.h.  Original work.  Freestanding (memcpy/memset/strcmp only). */
#include <string.h>

#include "cbvmc_wbstore.h"

#define NSECT (CBVMC_WB_WINDOW / CBVMC_WB_SECTOR)       /* 16 */
#define NPAGE (CBVMC_WB_WINDOW / CBVMC_WB_PAGE)         /* 64 */
#define ALL_SECT 0xFFFFu
#define ALL_PAGE 0xFFFFFFFFFFFFFFFFull

enum { WHY_COMPLETE, WHY_SWITCH, WHY_SYNC, WHY_IDLE };

static int is_card(const struct cbvmc_wb *wb, const char *p) { return strcmp(p, wb->path) == 0; }

static void window_reset(struct cbvmc_wb *wb, uint32_t base)
{
    wb->base = base;
    wb->valid = wb->dirty = 0;
    wb->programmed = 0;
    wb->active = 1;
}

static int load_sector(struct cbvmc_wb *wb, unsigned s);
void cbvmc_wb_discard(struct cbvmc_wb *wb) { wb->valid = wb->dirty = 0; wb->programmed = 0; wb->active = 0; }

/* A lower read is tried up to three times: a failure of the drive path (busy bus, one lost command) must not become card data. */
static int lower_read(struct cbvmc_wb *wb, const char *p, uint32_t off, void *buf, uint32_t len)
{
    int r = 0, k;
    for (k = 0; k < 3; ++k) {
        r = wb->lower.read(wb->lower.context, p, off, buf, len);
        if (r == 0) return 0;
        wb->stats.read_retries++;
    }
    return r;
}

static uint32_t crc32_of(const uint8_t *p, uint32_t n)
{
    uint32_t c = 0xFFFFFFFFu, i, k;
    for (i = 0; i < n; ++i) {
        c ^= p[i];
        for (k = 0; k < 8; ++k) c = (c >> 1) ^ (0xEDB88320u & (0u - (c & 1u)));
    }
    return ~c;
}
static void put32(uint8_t *p, uint32_t v) { p[0] = (uint8_t)(v >> 24); p[1] = (uint8_t)(v >> 16); p[2] = (uint8_t)(v >> 8); p[3] = (uint8_t)v; }
static uint32_t get32(const uint8_t *p) { return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) | ((uint32_t)p[2] << 8) | p[3]; }

#define JMAGIC 0x4342574Au      /* "CBWJ" */

/* header = {magic, card offset, crc32(data), ~crc32} ; all zero = empty */
static void jhdr(uint8_t *h, uint32_t base, uint32_t crc, int valid)
{
    memset(h, 0, CBVMC_WB_JHDR);
    if (!valid) return;
    put32(h, JMAGIC); put32(h + 4, base); put32(h + 8, crc); put32(h + 12, ~crc);
}

static int jwrite_hdr(struct cbvmc_wb *wb, uint32_t base, uint32_t crc, int valid)
{
    uint8_t h[CBVMC_WB_JHDR];
    jhdr(h, base, crc, valid);
    wb->stats.lower_writes++;
    return wb->lower.write(wb->lower.context, wb->jpath, 0, h, CBVMC_WB_JHDR);
}

static int jflush(struct cbvmc_wb *wb)
{
    wb->stats.lower_flushes++;
    return wb->lower.flush ? wb->lower.flush(wb->lower.context, wb->jpath) : 0;
}

/* make sure the journal file exists with a zero header (the data area is created by the first data write, which appends at offset 16) */
static int jprepare(struct cbvmc_wb *wb)
{
    uint32_t n;
    uint8_t zero[CBVMC_WB_JHDR];
    if (wb->jready) return 0;
    if (wb->lower.size(wb->lower.context, wb->jpath, &n) != 0) {
        if (wb->lower.create(wb->lower.context, wb->jpath) != 0) return -1;
        n = 0;
    }
    if (n < CBVMC_WB_JHDR) {
        memset(zero, 0, sizeof(zero));
        wb->stats.lower_writes++;
        if (wb->lower.write(wb->lower.context, wb->jpath, n, zero, CBVMC_WB_JHDR - n) != 0) return -1;
        if (jflush(wb) != 0) return -1;
    }
    wb->jready = 1;
    return 0;
}

/* read the card block at `base` back from the lower store and compare with `data` (8 KiB), sector by sector */
static int verify_block(struct cbvmc_wb *wb, uint32_t base, const uint8_t *data)
{
#ifdef CBVMC_WB_NO_VERIFY
    (void)wb; (void)base; (void)data;
    return 0;
#else
    uint32_t o;
    for (o = 0; o < CBVMC_WB_WINDOW; o += CBVMC_WB_SCRATCH) {
        wb->stats.verify_reads++;
        wb->stats.lower_reads++;
        if (wb->lower.read(wb->lower.context, wb->path, base + o, wb->scratch, CBVMC_WB_SCRATCH) != 0) return -1;
        if (memcmp(wb->scratch, data + o, CBVMC_WB_SCRATCH) != 0) { wb->stats.verify_mismatches++; return -1; }
    }
    return 0;
#endif
}

/* write the dirty runs of `data` (a full 8 KiB block image whose dirty mask is `mask`) to the card file at `base` */
static int write_runs(struct cbvmc_wb *wb, uint32_t base, const uint8_t *data, uint16_t *mask)
{
    unsigned s = 0;
    int wrote = 0;
    while (s < NSECT) {
        unsigned e;
        if (!(*mask & (1u << s))) { ++s; continue; }
        e = s;
        while (e < NSECT && (*mask & (1u << e))) ++e;
        wb->stats.lower_writes++;
        if (wb->lower.write(wb->lower.context, wb->path, base + s * CBVMC_WB_SECTOR, data + s * CBVMC_WB_SECTOR,
                            (e - s) * CBVMC_WB_SECTOR) != 0) return -1;
        wb->stats.bytes_to_lower += (e - s) * CBVMC_WB_SECTOR;
        *mask &= (uint16_t)~(((1u << e) - 1u) & ~((1u << s) - 1u));
        wrote = 1;
        s = e;
    }
    if (wrote && wb->lower.flush) {
        wb->stats.lower_flushes++;
        if (wb->lower.flush(wb->lower.context, wb->path) != 0) return -1;
    }
    return 0;
}

/* One attempt of the commit sequence of the header comment.  The dirty mask is restored on any failure. */
static int commit_once(struct cbvmc_wb *wb)
{
    uint16_t mask = wb->dirty;
    uint16_t need = wb->dirty;
    uint32_t crc = 0;
    int journal = wb->jpath[0] != 0;
    uint32_t s;

    /* the journal and the verify need the whole block image: load sectors that were never touched */
    if (journal) {
        for (s = 0; s < NSECT; ++s)
            if (!(wb->valid & (1u << s)) && load_sector(wb, s) != 0) return -1;
    }
    if (journal) {
        crc = crc32_of(wb->win, CBVMC_WB_WINDOW);
        if (jprepare(wb) != 0) return -1;
        wb->stats.lower_writes++; wb->stats.journal_writes++;
        if (wb->lower.write(wb->lower.context, wb->jpath, CBVMC_WB_JHDR, wb->win, CBVMC_WB_WINDOW) != 0) return -1;
        if (jwrite_hdr(wb, wb->base, crc, 1) != 0) return -1;
        if (jflush(wb) != 0) return -1;
    }
    if (write_runs(wb, wb->base, wb->win, &mask) != 0) { wb->dirty = need; return -1; }
    /* verify what the drive holds: the written sectors always; with a journal the whole block image (it is complete) */
    if (journal) {
        if (verify_block(wb, wb->base, wb->win) != 0) { wb->dirty = need; return -1; }
    } else {
        for (s = 0; s < NSECT; ++s) {
            if (!(need & (1u << s))) continue;
#ifndef CBVMC_WB_NO_VERIFY
            wb->stats.verify_reads++; wb->stats.lower_reads++;
            if (wb->lower.read(wb->lower.context, wb->path, wb->base + s * CBVMC_WB_SECTOR, wb->scratch, CBVMC_WB_SECTOR) != 0 ||
                memcmp(wb->scratch, wb->win + s * CBVMC_WB_SECTOR, CBVMC_WB_SECTOR) != 0) {
                wb->stats.verify_mismatches++;
                wb->dirty = need;
                return -1;
            }
#endif
        }
    }
    wb->dirty = 0;
    if (journal) {
        if (jwrite_hdr(wb, 0, 0, 0) != 0 || jflush(wb) != 0) {
            /* data is safe on the card; the journal still names the same (now identical) block: a replay is harmless only if no
               newer save happened, so keep the window "dirty" and clear the journal on the next trigger before anything else */
            wb->dirty = need;
            return -1;
        }
    }
    return 0;
}

static int commit(struct cbvmc_wb *wb, int why)
{
    unsigned attempt;
    uint16_t was_dirty = wb->dirty;
    if (!wb->active || wb->dirty == 0) return 0;
    for (attempt = 0; attempt <= CBVMC_WB_RETRIES; ++attempt) {
        if (commit_once(wb) == 0) {
            if (wb->on_commit) wb->on_commit(wb, wb->base, was_dirty, 0, why);
            wb->err = 0;
            wb->stats.commits++;
            switch (why) {
            case WHY_COMPLETE: wb->stats.commits_complete++; break;
            case WHY_SWITCH: wb->stats.commits_switch++; break;
            case WHY_SYNC: wb->stats.commits_sync++; break;
            default: wb->stats.commits_idle++; break;
            }
            return 0;
        }
    }
    wb->err = 1;
    wb->stats.commit_failures++;
    if (wb->on_commit) wb->on_commit(wb, wb->base, was_dirty, -1, why);
    return -1;
}

static int learn_size(struct cbvmc_wb *wb)
{
    uint32_t n;
    if (wb->file_size) return 0;
    if (wb->lower.size(wb->lower.context, wb->path, &n) != 0) return -1;
    wb->file_size = n;
    return 0;
}

/* The card file exists, is a whole number of 512-byte sectors and [off, off+len) lies inside it and inside one window. */
static int bufferable(struct cbvmc_wb *wb, const char *p, uint32_t off, uint32_t len)
{
    if (!is_card(wb, p) || len == 0) return 0;
    if (learn_size(wb) != 0 || (wb->file_size & (CBVMC_WB_SECTOR - 1u))) return 0;
    if (off > wb->file_size || len > wb->file_size - off) return 0;
    return (off / CBVMC_WB_WINDOW) == ((off + len - 1u) / CBVMC_WB_WINDOW);
}

static int load_sector(struct cbvmc_wb *wb, unsigned s)
{
    wb->stats.lower_reads++;
    wb->stats.sector_loads++;
    if (lower_read(wb, wb->path, wb->base + s * CBVMC_WB_SECTOR, wb->win + s * CBVMC_WB_SECTOR, CBVMC_WB_SECTOR) != 0) return -1;
    wb->valid |= (uint16_t)(1u << s);
    return 0;
}

static int w_size(void *c, const char *p, uint32_t *bytes)
{
    struct cbvmc_wb *wb = c;
    int r = wb->lower.size(wb->lower.context, p, bytes);
    if (r == 0 && is_card(wb, p)) wb->file_size = *bytes;
    return r;
}

/* 1 when the byte at card offset `off` is served from the window */
static int in_window(const struct cbvmc_wb *wb, uint32_t off)
{
    return wb->active && off >= wb->base && off < wb->base + CBVMC_WB_WINDOW &&
           (wb->valid & (1u << ((off - wb->base) / CBVMC_WB_SECTOR))) != 0;
}

static int w_read(void *c, const char *p, uint32_t off, void *buf, uint32_t len)
{
    struct cbvmc_wb *wb = c;
    uint8_t *out = buf;
    wb->stats.model_reads++;
    if (!wb->active || !is_card(wb, p) || len == 0) {
        wb->stats.lower_reads++;
        return lower_read(wb, p, off, buf, len);
    }
    while (len != 0) {
        uint32_t n = ((off & ~(CBVMC_WB_SECTOR - 1u)) + CBVMC_WB_SECTOR) - off;
        if (n > len) n = len;
        if (in_window(wb, off)) {
            memcpy(out, wb->win + (off - wb->base), n);
        } else {
            /* extend the lower read over following sectors that the window does not serve */
            while (n < len && !in_window(wb, off + n)) {
                uint32_t step = CBVMC_WB_SECTOR;
                n += step;
                if (n > len) n = len;
            }
            wb->stats.lower_reads++;
            if (lower_read(wb, p, off, out, n) != 0) return -1;
        }
        out += n; off += n; len -= n;
    }
    return 0;
}

static int w_write(void *c, const char *p, uint32_t off, const void *buf, uint32_t len)
{
    struct cbvmc_wb *wb = c;
    const uint8_t *in = buf;
    uint32_t win_base, rel, s0, s1, s;
    wb->stats.model_writes++;
    if (!bufferable(wb, p, off, len)) {
        if (is_card(wb, p) && wb->active && commit(wb, WHY_SWITCH) != 0) return -1;
        if (is_card(wb, p)) wb->active = 0;
        wb->stats.lower_writes++;
        wb->stats.bytes_to_lower += len;
        return wb->lower.write(wb->lower.context, p, off, buf, len);
    }
    win_base = off & ~(CBVMC_WB_WINDOW - 1u);
    if (!wb->active || wb->base != win_base) {
        if (wb->active && commit(wb, WHY_SWITCH) != 0) return -1;
        window_reset(wb, win_base);
    }
    rel = off - win_base;
    s0 = rel / CBVMC_WB_SECTOR;
    s1 = (rel + len - 1u) / CBVMC_WB_SECTOR;
    for (s = s0; s <= s1; ++s) {
        uint32_t a = s * CBVMC_WB_SECTOR, b = a + CBVMC_WB_SECTOR;
        int whole = rel <= a && rel + len >= b;
        if (!whole && !(wb->valid & (1u << s)) && load_sector(wb, s) != 0) return -1;
    }
    memcpy(wb->win + rel, in, len);
    for (s = s0; s <= s1; ++s) {
        uint32_t a = s * CBVMC_WB_SECTOR, b = a + CBVMC_WB_SECTOR;
        wb->valid |= (uint16_t)(1u << s);
        wb->dirty |= (uint16_t)(1u << s);
        (void)a; (void)b;
    }
    /* erase-style call: whole 512-byte aligned sectors (the model's erase fill) -> the pages there are blank again;
       anything shorter is a page program -> mark the 128-byte page(s) programmed */
    if ((rel & (CBVMC_WB_SECTOR - 1u)) == 0 && (len & (CBVMC_WB_SECTOR - 1u)) == 0) {
        uint32_t p0 = rel / CBVMC_WB_PAGE, p1 = (rel + len) / CBVMC_WB_PAGE, k;
        for (k = p0; k < p1; ++k) wb->programmed &= ~(1ull << k);
    } else {
        uint32_t p0 = rel / CBVMC_WB_PAGE, p1 = (rel + len - 1u) / CBVMC_WB_PAGE, k;
        for (k = p0; k <= p1; ++k) wb->programmed |= 1ull << k;
    }
    wb->quiet = 0;
    return 0;
}

static int w_flush(void *c, const char *p)
{
    struct cbvmc_wb *wb = c;
    wb->stats.model_flushes++;
    if (!is_card(wb, p)) {
        wb->stats.lower_flushes++;
        return wb->lower.flush ? wb->lower.flush(wb->lower.context, p) : 0;
    }
#ifdef CBVMC_WB_PAGE_COMMIT
    if (wb->active && wb->dirty) return commit(wb, WHY_COMPLETE);
#else
    if (wb->active && wb->dirty && wb->programmed == ALL_PAGE) return commit(wb, WHY_COMPLETE);
#endif
    return 0;
}

static int before_namespace_change(struct cbvmc_wb *wb)
{
    if (wb->active && commit(wb, WHY_SWITCH) != 0) return -1;
    wb->active = 0;
    wb->file_size = 0;
    return 0;
}

static int w_create(void *c, const char *p)
{
    struct cbvmc_wb *wb = c;
    if (before_namespace_change(wb) != 0) return -1;
    return wb->lower.create(wb->lower.context, p);
}

static int w_rename(void *c, const char *from, const char *to)
{
    struct cbvmc_wb *wb = c;
    if (before_namespace_change(wb) != 0) return -1;
    return wb->lower.rename(wb->lower.context, from, to);
}

static int w_remove(void *c, const char *p)
{
    struct cbvmc_wb *wb = c;
    if (before_namespace_change(wb) != 0) return -1;
    return wb->lower.remove(wb->lower.context, p);
}

int cbvmc_wb_init(struct cbvmc_wb *wb, struct cubeboot_vmc_store *out, const struct cubeboot_vmc_store *lower,
                  const char *path, const char *journal, uint8_t *window, uint32_t idle_ticks)
{
    size_t n = strlen(path);
    if (n >= sizeof(wb->path)) return CUBEBOOT_VMC_ERR_PATH;
    if (journal && (strlen(journal) >= sizeof(wb->jpath) || strcmp(journal, path) == 0)) return CUBEBOOT_VMC_ERR_PATH;
    memset(wb, 0, sizeof(*wb));
    memcpy(wb->path, path, n + 1);
    if (journal) memcpy(wb->jpath, journal, strlen(journal) + 1);
    wb->lower = *lower;
    wb->win = window;
    wb->idle_ticks = idle_ticks;
    memset(out, 0, sizeof(*out));
    out->context = wb;
    out->size = w_size;
    out->read = w_read;
    out->write = w_write;
    out->create = w_create;
    out->rename = w_rename;
    out->remove = w_remove;
    out->flush = w_flush;
    return CUBEBOOT_VMC_OK;
}

int cbvmc_wb_recover(struct cbvmc_wb *wb)
{
    uint8_t h[CBVMC_WB_JHDR];
    uint32_t n, base, crc, i;
    static const uint8_t zero[CBVMC_WB_JHDR];
    if (!wb->jpath[0]) return 0;
    if (wb->lower.size(wb->lower.context, wb->jpath, &n) != 0 || n < CBVMC_WB_JHDR + CBVMC_WB_WINDOW) return 0;
    wb->stats.lower_reads++;
    if (wb->lower.read(wb->lower.context, wb->jpath, 0, h, CBVMC_WB_JHDR) != 0) return -1;
    if (memcmp(h, zero, CBVMC_WB_JHDR) == 0) { wb->jready = 1; return 0; }
    if (get32(h) != JMAGIC || get32(h + 8) != ~get32(h + 12)) return 0;      /* torn header: nothing was committed to the card yet */
    base = get32(h + 4); crc = get32(h + 8);
    if ((base & (CBVMC_WB_WINDOW - 1u)) != 0 || learn_size(wb) != 0 || base > wb->file_size || wb->file_size - base < CBVMC_WB_WINDOW) return 0;
    /* load the journalled block into the window sector by sector and check its CRC */
    window_reset(wb, base);
    for (i = 0; i < NSECT; ++i) {
        wb->stats.lower_reads++;
        if (wb->lower.read(wb->lower.context, wb->jpath, CBVMC_WB_JHDR + i * CBVMC_WB_SECTOR, wb->win + i * CBVMC_WB_SECTOR,
                           CBVMC_WB_SECTOR) != 0) { wb->active = 0; return -1; }
    }
    if (crc32_of(wb->win, CBVMC_WB_WINDOW) != crc) { wb->active = 0; return 0; }   /* data torn: the card was never touched */
    wb->valid = wb->dirty = ALL_SECT;
    wb->jready = 1;
    if (commit(wb, WHY_SYNC) != 0) return -1;          /* writes the block, verifies, clears the journal */
    wb->active = 0;
    wb->stats.recovered++;
    return 0;
}

int cbvmc_wb_rebind(struct cbvmc_wb *wb, const char *path, const char *journal)
{
    size_t n = strlen(path), j = journal ? strlen(journal) : 0;
    if (n >= sizeof(wb->path) || j >= sizeof(wb->jpath) || (journal && strcmp(journal, path) == 0)) return CUBEBOOT_VMC_ERR_PATH;
    if (wb->active && wb->dirty && commit(wb, WHY_SWITCH) != 0) return -1;      /* the old card's block must be safe before the window is reused */
    memcpy(wb->path, path, n + 1);
    if (journal) memcpy(wb->jpath, journal, j + 1); else wb->jpath[0] = 0;
    wb->jready = 0;
    wb->active = 0;
    wb->err = 0;
    wb->file_size = 0;
    wb->valid = wb->dirty = 0;
    wb->programmed = 0;
    wb->quiet = 0;
    return CUBEBOOT_VMC_OK;
}

int cbvmc_wb_prepare_journal(struct cbvmc_wb *wb)
{
    uint32_t n;
    uint8_t zero[CBVMC_WB_SECTOR];
    if (!wb->jpath[0]) return 0;
    if (jprepare(wb) != 0) return -1;
    if (wb->lower.size(wb->lower.context, wb->jpath, &n) != 0) return -1;
    memset(zero, 0, sizeof(zero));
    while (n < CBVMC_WB_JHDR + CBVMC_WB_WINDOW) {
        uint32_t k = CBVMC_WB_JHDR + CBVMC_WB_WINDOW - n;
        if (k > sizeof(zero)) k = sizeof(zero);
        wb->stats.lower_writes++;
        if (wb->lower.write(wb->lower.context, wb->jpath, n, zero, k) != 0) return -1;     /* the volume is full or read-only: refuse now, not in the middle of a save */
        n += k;
    }
    return jflush(wb);
}

int cbvmc_wb_sync(struct cbvmc_wb *wb) { return commit(wb, WHY_SYNC); }

void cbvmc_wb_tick(struct cbvmc_wb *wb)
{
    if (!wb->active || wb->dirty == 0) return;
    if (++wb->quiet >= wb->idle_ticks) {
        (void)commit(wb, WHY_IDLE);
        wb->quiet = 0;
    }
}

int cbvmc_wb_dirty(const struct cbvmc_wb *wb) { return wb->active && wb->dirty != 0; }
