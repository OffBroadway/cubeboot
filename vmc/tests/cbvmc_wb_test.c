/* Copyright (C) 2026 Nigzel / Bjerreman. SPDX-License-Identifier: GPL-2.0-only */
/*
 * Host tests of the write-coalescing card store (cbvmc_wbstore.c):
 *   A. model-based random test against a plain byte array (reads always equal the truth, durable state equals the truth after sync)
 *   B. the real card model on top of the Flippy file API and its strict simulated drive, starting from a golden image made by
 *      tools/vmc_builder: a Melee-style block save costs a handful of DI commands, other blocks stay byte-identical, cold restart
 *      reads the data back, the host file passes the Python validator (checked by test_cbvmc.py through the dump)
 *   C. power-cut sweep: the bus dies at every command of a block save; the card file differs from the golden image only inside
 *      the block being saved, and a retry after the bus returns completes the save
 *
 *   cbvmc_wb_test golden.raw [dump.raw]
 */
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "cbvmc_ramstore.h"
#include "cbvmc_wbstore.h"
#include "cubeboot_vmc_flippy.h"
#include "flippy_fake_drive.h"

#define CHECK(c) do { if (!(c)) { fprintf(stderr, "%s:%d: CHECK failed: %s\n", __FILE__, __LINE__, #c); abort(); } } while (0)

/* ---- A. random model test ---------------------------------------------------------------------------------------- */

#define FSZ (64u * 1024u)
static uint32_t rng_state = 12345;
static uint32_t rnd(void) { rng_state = rng_state * 1664525u + 1013904223u; return rng_state >> 8; }

struct count { struct cubeboot_vmc_store s; unsigned reads, writes, flushes; };
static int c_size(void *c, const char *p, uint32_t *b) { struct count *k = c; return k->s.size(k->s.context, p, b); }
static int c_read(void *c, const char *p, uint32_t o, void *b, uint32_t l) { struct count *k = c; k->reads++; return k->s.read(k->s.context, p, o, b, l); }
static int c_write(void *c, const char *p, uint32_t o, const void *b, uint32_t l) { struct count *k = c; k->writes++; return k->s.write(k->s.context, p, o, b, l); }
static int c_flush(void *c, const char *p) { (void)p; ((struct count *)c)->flushes++; return 0; }

static void test_random(void)
{
    static uint8_t image[FSZ + 1024], truth[FSZ], window[CBVMC_WB_WINDOW], buf[FSZ], expect[FSZ];
    struct cbvmc_ramstore rs;
    struct cubeboot_vmc_store ram, low, top;
    struct count cnt;
    struct cbvmc_wb wb;
    unsigned i, round;
    uint32_t n;

    for (round = 0; round < 40; ++round) {
        memset(image, 0, sizeof(image));
        CHECK(cbvmc_ramstore_init(&rs, &ram, image, sizeof(image), "/card") == 0);
        CHECK(ram.create(&rs, "/card.tmp") == 0);
        for (i = 0; i < FSZ; ++i) truth[i] = (uint8_t)(rnd());
        CHECK(ram.write(&rs, "/card.tmp", 0, truth, FSZ) == 0);
        CHECK(ram.rename(&rs, "/card.tmp", "/card") == 0);
        memset(&cnt, 0, sizeof(cnt));
        cnt.s = ram;
        low = ram;
        low.context = &cnt; low.size = c_size; low.read = c_read; low.write = c_write; low.flush = c_flush;
        CHECK(cbvmc_wb_init(&wb, &top, &low, "/card", NULL, window, 3) == 0);
        CHECK(top.size(top.context, "/card", &n) == 0 && n == FSZ);
        for (i = 0; i < 400; ++i) {
            uint32_t op = rnd() % 8, off, len;
            switch (op) {
            case 0: case 1: case 2:             /* arbitrary write, sometimes crossing windows */
                len = 1 + rnd() % (rnd() % 4 == 0 ? 20000u : 700u);
                off = rnd() % (FSZ - len < 1 ? 1 : FSZ - len + 1);
                if (off + len > FSZ) len = FSZ - off;
                for (n = 0; n < len; ++n) buf[n] = (uint8_t)rnd();
                CHECK(top.write(top.context, "/card", off, buf, len) == 0);
                memcpy(truth + off, buf, len);
                break;
            case 3:                             /* erase fill of a block, then page programs */
                off = (rnd() % (FSZ / 8192u)) * 8192u;
                memset(buf, 0xff, 512);
                for (n = 0; n < 8192; n += 512) CHECK(top.write(top.context, "/card", off + n, buf, 512) == 0);
                memset(truth + off, 0xff, 8192);
                if (rnd() & 1) {
                    for (n = 0; n < 8192; n += 128) {
                        unsigned k;
                        for (k = 0; k < 128; ++k) buf[k] = (uint8_t)rnd();
                        CHECK(top.write(top.context, "/card", off + n, buf, 128) == 0);
                        memcpy(truth + off + n, buf, 128);
                        CHECK(top.flush(top.context, "/card") == 0);
                    }
                }
                break;
            case 4: case 5:                     /* read anywhere and compare with the truth */
                len = 1 + rnd() % 3000u;
                off = rnd() % FSZ;
                if (off + len > FSZ) len = FSZ - off;
                CHECK(top.read(top.context, "/card", off, buf, len) == 0);
                CHECK(memcmp(buf, truth + off, len) == 0);
                break;
            case 6: cbvmc_wb_tick(&wb); break;
            default: if (rnd() % 4 == 0) CHECK(cbvmc_wb_sync(&wb) == 0); break;
            }
        }
        /* everything visible through the wrapper, durable after sync */
        CHECK(top.read(top.context, "/card", 0, expect, FSZ) == 0 && memcmp(expect, truth, FSZ) == 0);
        CHECK(cbvmc_wb_sync(&wb) == 0 && !cbvmc_wb_dirty(&wb));
        CHECK(memcmp(image, truth, FSZ) == 0);
    }
}

/* ---- B/C. the card model over Flippy file API ------------------------------------------------------------------- */

#define SLOT_A "/swiss/saves/MemoryCardA.USA.raw"
static struct fake fk;
static unsigned di_cmds;
static struct cubeboot_vmc_flippy fctx;
static struct cubeboot_vmc_store flippy_store, top_store;
static struct cbvmc_wb WB;
static uint8_t WIN[CBVMC_WB_WINDOW];
static uint8_t golden[1u << 20];
static uint32_t golden_len;
static const struct cubeboot_vmc_identity ident = { 0x00c0ffee12345678ULL, 3 };

static int counting_command(void *ctx, const uint32_t cmd[3], enum cubeboot_flippy_dir dir, void *buf, uint32_t len)
{
    di_cmds++;
    return fake_command(ctx, cmd, dir, buf, len);
}

static void host_file(char *out, size_t n)
{
    snprintf(out, n, "%s%s", fk.root, SLOT_A);
}

static void install_golden(void)
{
    char p[512], dir[600];
    FILE *f;
    fake_new_root(&fk, 1, 1);
    snprintf(dir, sizeof(dir), "mkdir -p '%s/swiss/saves'", fk.root);
    CHECK(system(dir) == 0);
    host_file(p, sizeof(p));
    f = fopen(p, "wb");
    CHECK(f && fwrite(golden, 1, golden_len, f) == golden_len);
    fclose(f);
}

static void bring_up(int use_wb, int reopen)
{
    struct cubeboot_flippy_transport t;
    t.context = &fk; t.command = counting_command; t.begin = NULL; t.end = NULL;
    memset(fk.fd, 0, sizeof(fk.fd));
    memset(fk.status, 0, sizeof(fk.status));
    cubeboot_vmc_flippy_init(&fctx, &t, reopen ? CUBEBOOT_FLIPPY_FLUSH_REOPEN : CUBEBOOT_FLIPPY_FLUSH_NONE, &flippy_store);
    if (use_wb) CHECK(cbvmc_wb_init(&WB, &top_store, &flippy_store, SLOT_A, SLOT_A ".wal", WIN, 4) == 0);
    else top_store = flippy_store;
}

static void cmd5(uint8_t *b, uint8_t cmd, uint32_t a)
{
    b[0] = cmd; b[1] = (uint8_t)(a >> 17); b[2] = (uint8_t)(a >> 9);
    b[3] = (uint8_t)((a >> 7) & 3); b[4] = (uint8_t)(a & 0x7f);
}
static void send(struct cubeboot_vmc_card *c, const uint8_t *tx, unsigned n)
{
    unsigned i;
    for (i = 0; i < n; ++i) cubeboot_vmc_card_transfer_byte(c, tx[i]);
}
/* what the SDK does for one block: sector erase, then 64 page programs by DMA; returns the card status bits seen */
static unsigned save_block(struct cubeboot_vmc_card *c, uint32_t block, const uint8_t *data)
{
    uint8_t e[3] = { 0xf1, (uint8_t)((block * 8192u) >> 17), (uint8_t)((block * 8192u) >> 9) };
    unsigned p;
    cubeboot_vmc_card_select(c, 1); send(c, e, 3); cubeboot_vmc_card_select(c, 0);
    for (p = 0; p < 64; ++p) {
        uint8_t cmd[5];
        cmd5(cmd, 0xf2, block * 8192u + p * 128u);
        cubeboot_vmc_card_select(c, 1);
        send(c, cmd, 5);
        (void)cubeboot_vmc_card_dma_write(c, data + p * 128u, 128);
        cubeboot_vmc_card_select(c, 0);
    }
    return c->status & 0x18u;
}
static void load_block(struct cubeboot_vmc_card *c, uint32_t block, uint8_t *buf)
{
    uint8_t cmd[9];
    cmd5(cmd, 0x52, block * 8192u);
    memset(cmd + 5, 0, 4);
    cubeboot_vmc_card_select(c, 1); send(c, cmd, 9);
    CHECK(cubeboot_vmc_card_dma_read(c, buf, 8192) == 0);
    cubeboot_vmc_card_select(c, 0);
}

static uint32_t read_host(uint8_t *out, uint32_t cap)
{
    char p[512];
    FILE *f;
    size_t n;
    host_file(p, sizeof(p));
    f = fopen(p, "rb");
    CHECK(f != NULL);
    n = fread(out, 1, cap, f);
    fclose(f);
    return (uint32_t)n;
}

static void test_flippy(const char *dump)
{
    static uint8_t data[8192], back[8192], after[1u << 20];
    struct cubeboot_vmc_port port;
    uint32_t blk = 20, i;
    unsigned direct_cmds, wb_cmds, commits_before;

    for (i = 0; i < sizeof(data); ++i) data[i] = (uint8_t)(i * 13 + 5);

    /* baseline: the model straight on the Flippy store (reopen policy), the way the plain RAM design would have to behave */
    install_golden();
    bring_up(0, 1);
    CHECK(cubeboot_vmc_port_init(&port, &top_store, CUBEBOOT_VMC_SLOT_A, SLOT_A, &ident, NULL) == 0);
    CHECK(cubeboot_vmc_port_service(&port) == CUBEBOOT_VMC_EVENT_VIRTUAL_INSERTED);
    di_cmds = 0;
    CHECK(save_block(cubeboot_vmc_port_card(&port), blk, data) == 0);
    direct_cmds = di_cmds;
    CHECK(cubeboot_vmc_flippy_close(&fctx) == 0);
    fake_clean(&fk);

    /* with the write-back window */
    install_golden();
    bring_up(1, 1);
    CHECK(cubeboot_vmc_port_init(&port, &top_store, CUBEBOOT_VMC_SLOT_A, SLOT_A, &ident, NULL) == 0);
    CHECK(cubeboot_vmc_port_service(&port) == CUBEBOOT_VMC_EVENT_VIRTUAL_INSERTED);
    CHECK(port.source == CUBEBOOT_VMC_SOURCE_VIRTUAL);
    di_cmds = 0;
    commits_before = WB.stats.commits;
    CHECK(save_block(cubeboot_vmc_port_card(&port), blk, data) == 0);
    wb_cmds = di_cmds;
    CHECK(WB.stats.commits == commits_before + 1 && WB.stats.commits_complete >= 1);
    CHECK(!cbvmc_wb_dirty(&WB));
    load_block(cubeboot_vmc_port_card(&port), blk, back);
    CHECK(memcmp(back, data, sizeof(data)) == 0);
    printf("block save over the Flippy API: %u DI commands direct, %u with the window (%u lower writes, %u bytes, %u flushes)\n",
           direct_cmds, wb_cmds, WB.stats.lower_writes, WB.stats.bytes_to_lower, WB.stats.lower_flushes);
    CHECK(wb_cmds * 4 < direct_cmds);      /* about 37 with the 8 KiB chunk, about 50 with the 2 KiB chunk of the resident build */
    CHECK(WB.stats.verify_mismatches == 0 && WB.stats.journal_writes == 1);

    /* on the host file: only block `blk` changed, and it holds the data */
    CHECK(read_host(after, sizeof(after)) == golden_len);
    CHECK(memcmp(after + blk * 8192u, data, 8192) == 0);
    CHECK(memcmp(after, golden, blk * 8192u) == 0);
    CHECK(memcmp(after + (blk + 1) * 8192u, golden + (blk + 1) * 8192u, golden_len - (blk + 1) * 8192u) == 0);

    /* cold restart: no state in RAM */
    CHECK(cbvmc_wb_sync(&WB) == 0);
    CHECK(cubeboot_vmc_flippy_close(&fctx) == 0);
    bring_up(1, 1);
    CHECK(cubeboot_vmc_port_init(&port, &top_store, CUBEBOOT_VMC_SLOT_A, SLOT_A, &ident, NULL) == 0);
    CHECK(cubeboot_vmc_port_service(&port) == CUBEBOOT_VMC_EVENT_VIRTUAL_INSERTED);
    load_block(cubeboot_vmc_port_card(&port), blk, back);
    CHECK(memcmp(back, data, sizeof(data)) == 0);
    CHECK(cubeboot_vmc_validate(&top_store, SLOT_A) == 0);
    CHECK(cubeboot_vmc_flippy_close(&fctx) == 0);
    CHECK(fk.violations == 0 && fk.forbidden == 0);
    if (dump) {
        FILE *f = fopen(dump, "wb");
        CHECK(f && fwrite(after, 1, golden_len, f) == golden_len);
        fclose(f);
    }
    fake_clean(&fk);
}

static void test_power_cut(void)
{
    static uint8_t data[8192], after[1u << 20], back[8192];
    struct cubeboot_vmc_port port;
    uint32_t blk = 21, i, total = 0, k;
    unsigned finished = 0, torn = 0, old = 0, recovered_runs = 0;

    for (i = 0; i < sizeof(data); ++i) data[i] = (uint8_t)(i * 29 + 1);

    for (k = 0; k < 90; ++k) {
        install_golden();
        bring_up(1, 1);
        CHECK(cubeboot_vmc_port_init(&port, &top_store, CUBEBOOT_VMC_SLOT_A, SLOT_A, &ident, NULL) == 0);
        CHECK(cubeboot_vmc_port_service(&port) == CUBEBOOT_VMC_EVENT_VIRTUAL_INSERTED);
        if (k == 0) {                           /* count the commands of a clean save once */
            di_cmds = 0;
            CHECK(save_block(cubeboot_vmc_port_card(&port), blk, data) == 0);
            total = di_cmds;
            CHECK(cubeboot_vmc_flippy_close(&fctx) == 0);
            fake_clean(&fk);
            install_golden();
            bring_up(1, 1);
            CHECK(cubeboot_vmc_port_init(&port, &top_store, CUBEBOOT_VMC_SLOT_A, SLOT_A, &ident, NULL) == 0);
            CHECK(cubeboot_vmc_port_service(&port) == CUBEBOOT_VMC_EVENT_VIRTUAL_INSERTED);
        }
        fk.fail_after = (int)k;
        (void)save_block(cubeboot_vmc_port_card(&port), blk, data);
        fk.fail_after = -1;
        CHECK(read_host(after, sizeof(after)) == golden_len);
        /* the damage of a cut stays inside the block being saved */
        CHECK(memcmp(after, golden, blk * 8192u) == 0);
        CHECK(memcmp(after + (blk + 1) * 8192u, golden + (blk + 1) * 8192u, golden_len - (blk + 1) * 8192u) == 0);
        if (memcmp(after + blk * 8192u, data, 8192) == 0) finished++;
        else if (memcmp(after + blk * 8192u, golden + blk * 8192u, 8192) == 0) old++;
        else torn++;
        /* reboot after the cut: new contexts, recovery replays or ignores the journal; the block is entirely old or entirely new */
        if (k > 2) {
            (void)cubeboot_vmc_flippy_close(&fctx);
            bring_up(1, 1);
            CHECK(cbvmc_wb_recover(&WB) == 0);
            CHECK(read_host(after, sizeof(after)) == golden_len);
            CHECK(memcmp(after + blk * 8192u, data, 8192) == 0 || memcmp(after + blk * 8192u, golden + blk * 8192u, 8192) == 0);
            CHECK(memcmp(after, golden, blk * 8192u) == 0);
            CHECK(!cbvmc_wb_dirty(&WB));
            recovered_runs++;
            (void)cubeboot_vmc_flippy_close(&fctx);
            fake_clean(&fk);
            continue;
        }
        /* the bus comes back: the retry path (sync) leaves the card either with the new block or reports the failure, never silently
           with a stale window */
        if (cbvmc_wb_dirty(&WB)) {
            CHECK(cbvmc_wb_sync(&WB) == 0);
            load_block(cubeboot_vmc_port_card(&port), blk, back);
            CHECK(memcmp(back, data, 8192) == 0);
            CHECK(read_host(after, sizeof(after)) == golden_len);
            CHECK(memcmp(after + blk * 8192u, data, 8192) == 0);
        }
        (void)cubeboot_vmc_flippy_close(&fctx);
        fake_clean(&fk);
    }
    printf("power-cut sweep over DI command budgets 0..89 (a clean save is %u commands): %u finished, %u old block kept, %u torn before recovery (never after), %u reboot-recoveries checked\n",
           total, finished, old, torn, recovered_runs);
    CHECK(finished > 0 && old > 0 && recovered_runs > 10);      /* a torn block BEFORE recovery is expected when an 8 KiB write is several DI commands; after recovery it is always old or new (checked above) */
}

/* the lower store tears a write after N bytes (power lost in the middle of the 8 KiB write) */
struct tear { struct cubeboot_vmc_store s; int budget; };
static int t_size(void *c, const char *p, uint32_t *b) { struct tear *t = c; return t->s.size(t->s.context, p, b); }
static int t_read(void *c, const char *p, uint32_t o, void *b, uint32_t l) { struct tear *t = c; return t->s.read(t->s.context, p, o, b, l); }
static int t_write(void *c, const char *p, uint32_t o, const void *b, uint32_t l)
{
    struct tear *t = c;
    if (t->budget >= 0 && (uint32_t)t->budget < l) {
        uint32_t part = (uint32_t)t->budget;
        t->budget = 0;
        if (part) (void)t->s.write(t->s.context, p, o, b, part);
        return -1;
    }
    if (t->budget >= 0) t->budget -= (int)l;
    return t->s.write(t->s.context, p, o, b, l);
}
static int t_flush(void *c, const char *p) { (void)c; (void)p; return 0; }

static void test_tear(void)
{
    static uint8_t image[1u << 20], data[8192], back[8192];
    uint32_t blk = 22, i, cut;
    unsigned torn_seen = 0;
    for (i = 0; i < sizeof(data); ++i) data[i] = (uint8_t)(i * 31 + 7);
    for (cut = 0; cut <= 8192; cut += 640) {
        struct cbvmc_ramstore rs;
        struct cubeboot_vmc_store ram, low, top;
        struct tear tr;
        struct cbvmc_wb wb;
        struct cubeboot_vmc_port port;
        CHECK(cbvmc_ramstore_init(&rs, &ram, image, sizeof(image), SLOT_A) == 0);
        CHECK(ram.create(&rs, SLOT_A ".tmp") == 0);
        CHECK(ram.write(&rs, SLOT_A ".tmp", 0, golden, golden_len) == 0);
        CHECK(ram.rename(&rs, SLOT_A ".tmp", SLOT_A) == 0);
        tr.s = ram; tr.budget = -1;
        low = ram; low.context = &tr; low.size = t_size; low.read = t_read; low.write = t_write; low.flush = t_flush;
        CHECK(cbvmc_wb_init(&wb, &top, &low, SLOT_A, NULL, WIN, 4) == 0);
        CHECK(cubeboot_vmc_port_init(&port, &top, CUBEBOOT_VMC_SLOT_A, SLOT_A, &ident, NULL) == 0);
        CHECK(cubeboot_vmc_port_service(&port) == CUBEBOOT_VMC_EVENT_VIRTUAL_INSERTED);
        tr.budget = (int)cut;
        (void)save_block(cubeboot_vmc_port_card(&port), blk, data);
        /* damage only inside the block */
        CHECK(memcmp(image, golden, blk * 8192u) == 0);
        CHECK(memcmp(image + (blk + 1) * 8192u, golden + (blk + 1) * 8192u, golden_len - (blk + 1) * 8192u) == 0);
        if (cut < 8192) {
            CHECK(cbvmc_wb_dirty(&wb));                                 /* the failed commit is remembered ... */
            if (memcmp(image + blk * 8192u, golden + blk * 8192u, 8192) != 0 && memcmp(image + blk * 8192u, data, 8192) != 0) torn_seen++;
            tr.budget = -1;                                             /* ... and the retry completes it */
            CHECK(cbvmc_wb_sync(&wb) == 0);
        }
        CHECK(!cbvmc_wb_dirty(&wb));
        CHECK(memcmp(image + blk * 8192u, data, 8192) == 0);
        load_block(cubeboot_vmc_port_card(&port), blk, back);
        CHECK(memcmp(back, data, 8192) == 0);
    }
    printf("torn 8 KiB commit sweep: %u cut points left a torn block that the retry repaired\n", torn_seen);
    CHECK(torn_seen > 5);
}

/* ---- D. tiny two-file store with byte-exact tearing, corruption and a reboot -------------------------------------------------------- */

struct mfs {
    uint8_t card[0x80000], wal[CBVMC_WB_JHDR + CBVMC_WB_WINDOW];
    uint32_t card_n, wal_n; int wal_exists;
    long budget;              /* bytes that may still be written before the power goes (-1 = unlimited) */
    int dead;
    int corrupt;              /* 0 none, 1 flip one byte of the next card write, 2 flip a byte of every card write */
    unsigned writes;
};
static int m_card(const char *p) { return strcmp(p, "/card") == 0; }
static int m_wal(const char *p) { return strcmp(p, "/card.wal") == 0; }
static int m_size(void *c, const char *p, uint32_t *b)
{
    struct mfs *m = c;
    if (m_card(p)) { *b = m->card_n; return 0; }
    if (m_wal(p) && m->wal_exists) { *b = m->wal_n; return 0; }
    return CUBEBOOT_VMC_ERR_MISSING;
}
static int m_read(void *c, const char *p, uint32_t o, void *b, uint32_t l)
{
    struct mfs *m = c;
    uint8_t *f = m_card(p) ? m->card : m->wal;
    uint32_t n = m_card(p) ? m->card_n : m->wal_n;
    if (m->dead || (!m_card(p) && !(m_wal(p) && m->wal_exists)) || o > n || l > n - o) return -1;
    memcpy(b, f + o, l);
    return 0;
}
static int m_write(void *c, const char *p, uint32_t o, const void *b, uint32_t l)
{
    struct mfs *m = c;
    uint8_t *f = m_card(p) ? m->card : m->wal;
    uint32_t *n = m_card(p) ? &m->card_n : &m->wal_n;
    uint32_t part = l;
    if (m->dead || (!m_card(p) && !(m_wal(p) && m->wal_exists)) || o > *n || o + l > (m_card(p) ? sizeof(m->card) : sizeof(m->wal))) return -1;
    if (m->budget >= 0 && (uint32_t)m->budget < l) { part = (uint32_t)m->budget; m->dead = 1; }
    if (m->budget >= 0) m->budget -= (long)part;
    memcpy(f + o, b, part);
    if (m_card(p) && m->corrupt && part) {
        f[o + part / 2] ^= 0x40;
        if (m->corrupt == 1) m->corrupt = 0;
    }
    if (o + part > *n) *n = o + part;
    m->writes++;
    return m->dead ? -1 : 0;
}
static int m_create(void *c, const char *p)
{
    struct mfs *m = c;
    if (m->dead || !m_wal(p) || m->wal_exists) return -1;
    m->wal_exists = 1; m->wal_n = 0;
    return 0;
}
static int m_rename(void *c, const char *a, const char *b) { (void)c; (void)a; (void)b; return -1; }
static int m_remove(void *c, const char *p) { (void)c; (void)p; return -1; }
static int m_flush(void *c, const char *p) { (void)p; return ((struct mfs *)c)->dead ? -1 : 0; }

static void mfs_bind(struct mfs *m, struct cubeboot_vmc_store *s)
{
    memset(s, 0, sizeof(*s));
    s->context = m; s->size = m_size; s->read = m_read; s->write = m_write; s->create = m_create;
    s->rename = m_rename; s->remove = m_remove; s->flush = m_flush;
}

static struct mfs MF;

static void mfs_setup_commit(struct cubeboot_vmc_store *top, struct cbvmc_wb *wb, struct cubeboot_vmc_store *lower, const uint8_t *card_img)
{
    memset(&MF, 0, sizeof(MF));
    MF.budget = -1;
    memcpy(MF.card, card_img, golden_len);
    MF.card_n = golden_len;
    mfs_bind(&MF, lower);
    CHECK(cbvmc_wb_init(wb, top, lower, "/card", "/card.wal", WIN, 4) == 0);
}

/* one full block save by direct store calls: erase fill then 64 page programs of 128 bytes, flush after each (what the model does) */
static int save_direct(struct cubeboot_vmc_store *top, uint32_t blk, const uint8_t *data)
{
    uint8_t ff[512];
    uint32_t n;
    int bad = 0;
    memset(ff, 0xff, sizeof(ff));
    for (n = 0; n < 8192; n += 512) bad |= top->write(top->context, "/card", blk * 8192u + n, ff, 512);
    bad |= top->flush(top->context, "/card");
    for (n = 0; n < 8192; n += 128) {
        bad |= top->write(top->context, "/card", blk * 8192u + n, data + n, 128);
        bad |= top->flush(top->context, "/card");
    }
    return bad;
}

static void test_journal_cut(void)
{
    static uint8_t data[8192], oldb[8192], img[1u << 20];
    struct cubeboot_vmc_store top, lower;
    struct cbvmc_wb wb, wb2;
    uint32_t blk = 23, i;
    long cut, total;
    unsigned old = 0, neu = 0, replays = 0;
    for (i = 0; i < sizeof(data); ++i) data[i] = (uint8_t)(i * 17 + 9);
    memcpy(img, golden, golden_len);
    memcpy(oldb, golden + blk * 8192u, 8192);

    /* clean run: how many bytes does one commit write? (journal 16 + 8192 + header, card 8192, header clear 16) */
    mfs_setup_commit(&top, &wb, &lower, img);
    CHECK(save_direct(&top, blk, data) == 0);
    CHECK(memcmp(MF.card + blk * 8192u, data, 8192) == 0 && wb.stats.commits == 1 && wb.stats.journal_writes == 1);
    CHECK(wb.stats.verify_mismatches == 0);
    { uint8_t z[CBVMC_WB_JHDR]; memset(z, 0, sizeof(z)); CHECK(memcmp(MF.wal, z, sizeof(z)) == 0); }     /* journal left empty */
    total = 0;
    {   /* bytes written by the commit = journal creation + data + hdr + card + hdr */
        struct mfs probe; (void)probe;
    }
    total = 2 * CBVMC_WB_JHDR /*prepare + header*/ + CBVMC_WB_WINDOW + 8192 + 16;

    /* the power goes after `cut` bytes of the commit sequence, anywhere; then reboot + recover */
    for (cut = 0; cut <= total + 64; cut += 97) {
        mfs_setup_commit(&top, &wb, &lower, img);
        MF.budget = cut;
        (void)save_direct(&top, blk, data);
        /* reboot: power back, fresh wrapper over the same bytes */
        MF.budget = -1; MF.dead = 0;
        CHECK(cbvmc_wb_init(&wb2, &top, &lower, "/card", "/card.wal", WIN, 4) == 0);
        CHECK(cbvmc_wb_recover(&wb2) == 0);
        replays += wb2.stats.recovered;
        CHECK(!cbvmc_wb_dirty(&wb2));
        CHECK(memcmp(MF.card, golden, blk * 8192u) == 0);
        CHECK(memcmp(MF.card + (blk + 1) * 8192u, golden + (blk + 1) * 8192u, golden_len - (blk + 1) * 8192u) == 0);
        if (memcmp(MF.card + blk * 8192u, data, 8192) == 0) neu++;
        else if (memcmp(MF.card + blk * 8192u, oldb, 8192) == 0) old++;
        else {
            /* the only tolerated non-old state is the erase-fill + partial programs of a window that never reached its commit: not
               possible, because nothing reaches the card file before the journal is valid */
            CHECK(!"torn block after recovery");
        }
    }
    printf("byte-exact power cut sweep (%ld byte budgets): %u blocks old, %u new, 0 torn; %u journal replays\n", total / 97 + 2, old, neu, replays);
    CHECK(old > 5 && neu > 5 && replays > 0);

    /* silent corruption by the drive: caught by the read-back, retried, committed correctly */
    mfs_setup_commit(&top, &wb, &lower, img);
    MF.corrupt = 1;
    CHECK(save_direct(&top, blk, data) == 0);
    CHECK(wb.stats.verify_mismatches >= 1 && memcmp(MF.card + blk * 8192u, data, 8192) == 0);
    /* corruption that never goes away: the commit fails, the window stays dirty, the model sees an error */
    mfs_setup_commit(&top, &wb, &lower, img);
    MF.corrupt = 2;
    CHECK(save_direct(&top, blk, data) != 0);
    CHECK(wb.err && cbvmc_wb_dirty(&wb));
    MF.corrupt = 0;
    CHECK(cbvmc_wb_sync(&wb) == 0 && !cbvmc_wb_dirty(&wb) && memcmp(MF.card + blk * 8192u, data, 8192) == 0);
    /* with the journal valid on the medium the failed commit above is also recoverable after a reboot with the bad card block */
}

int main(int argc, char **argv)
{
    FILE *f;
    if (argc < 2) { fprintf(stderr, "usage: %s golden.raw [dump.raw]\n", argv[0]); return 2; }
    f = fopen(argv[1], "rb");
    CHECK(f != NULL);
    golden_len = (uint32_t)fread(golden, 1, sizeof(golden), f);
    fclose(f);
    CHECK(golden_len == 0x80000u || golden_len == 0x100000u);
    test_random();
    test_flippy(argc > 2 ? argv[2] : NULL);
    test_power_cut();
    test_tear();
    test_journal_cut();
    puts("cbvmc_wb_test: all passed");
    return 0;
}
