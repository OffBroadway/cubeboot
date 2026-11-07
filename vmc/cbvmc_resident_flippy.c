/* Copyright (C) 2026 Nigzel / Bjerreman. SPDX-License-Identifier: GPL-2.0-only */
/*
 * cbvmc_resident_flippy: the resident image of the loader build (PowerPC, freestanding).  Original work.
 * Card file on the FlippyDrive's SD through the drive's file API over polled DI, behind the write-back window (cbvmc_wbstore.c) with journal
 * and read-back.  Everything here stays in memory for the whole game; the one-shot checks live in the install image.
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
/* Written by the Cubeboot patch (flash build) before the game starts: [0] magic "DIAG", [1] slot mask the probe decided, [2] cbvmc_loader_install result, [3] probe A state, [4] probe A id,
   [5] probe B state, [6] probe B id, [7] reserved.  The install image copies it into the status note. */
uint32_t cbvmc_diag[8];
uint32_t cbvmc_resident_slot_allow(void) { return cbvmc_slot_allow; }
uint32_t cbvmc_card_geom = 0;
uint32_t cbvmc_diag_files = 0;        /* 1 = periodic counters / trace / trap-ring files during the game (diagnostics only) */         /* config.ini vmc_blocks / vmc_region, packed (cbvmc_geom_pack); 0 = 128 Mbit Windows-1252 */
uint32_t cbvmc_resident_card_geom(void) { return cbvmc_card_geom; }
#include "cbvmc_wbmux.h"
#include "cbvmc_blob.h"          /* cbvmc_state (the blob's counters), for the diagnostics trace */
#include "cbvmc_t2.h"            /* cbvmc_t2_state (cbvmc_resident_t2.c), for the counters */
extern struct cbvmc_t2 cbvmc_t2_state;
#include "cubeboot_flippy_di.h"
#include "cubeboot_vmc_flippy.h"

#define IDLE_TICKS  8u
#define DI_POLLS    (20u * 1000u * 1000u)

static struct cubeboot_flippy_di_regs di_regs;
static struct cubeboot_flippy_queue di_queue;
static struct cubeboot_flippy_sync di_sync;
static struct cubeboot_flippy_transport di_transport;
static struct cubeboot_vmc_flippy flippy;
static struct cbvmc_wb wb;
static struct cubeboot_vmc_store lower, top[2];
static struct cbvmc_wbmux mux;
static struct cbvmc_wbmux_slot slots[2];
static struct cubeboot_vmc_port port_b;
static uint8_t ready;
static uint8_t window[CBVMC_WB_WINDOW] __attribute__((aligned(32)));
static uint8_t hstack[CBVMC_HSTACK_BYTES] __attribute__((aligned(16)));

static void commit_trace(const struct cbvmc_wb *w, uint32_t base, uint16_t dirty, int result, int why);

struct cubeboot_vmc_port *cbvmc_resident_port_b(void) { return &port_b; }

static int drive_up(int *detail)
{
    struct cubeboot_flippy_drive_info info;
    struct cubeboot_flippy_hw hw;

    cubeboot_flippy_di_console_init(&di_regs);
    cubeboot_flippy_hw_from_regs(&hw, &di_regs);
    cubeboot_flippy_queue_init(&di_queue, &hw);
    cubeboot_flippy_sync_transport(&di_sync, &di_queue, DI_POLLS, &di_transport);
    cubeboot_flippy_api_init(&flippy.api, &di_transport);
    if (cubeboot_flippy_inquire(&flippy.api, &info) != 0 || !info.is_flippy) { *detail = info.is_flippy; return CBVMC_OPEN_ERR_DRIVE; }

    cubeboot_vmc_flippy_init(&flippy, &di_transport, CUBEBOOT_FLIPPY_FLUSH_REOPEN, &lower);
    if (cbvmc_wbmux_init(&mux, &wb, &lower, window, IDLE_TICKS) != 0) return CBVMC_OPEN_ERR_BIND;
    wb.on_commit = commit_trace;
    ready = 1;
    return CBVMC_OPEN_OK;
}

int cbvmc_resident_open(unsigned slot, const struct cubeboot_vmc_store **out, int *detail)
{
    const char *path = cbvmc_resident_card_path(slot);
    uint32_t sz;
    int rc;

    *detail = 0;
    if (slot > 1) return CBVMC_OPEN_ERR_BIND;
    if (!ready && (rc = drive_up(detail)) != CBVMC_OPEN_OK) return rc;
    if (cbvmc_wbmux_add(&mux, &slots[slot], &top[slot], path) != 0) return CBVMC_OPEN_ERR_BIND;
    *out = &top[slot];
    if (top[slot].size(top[slot].context, path, &sz) != 0) return CBVMC_OPEN_ERR_NOFILE;      /* missing: the install image generates it through *out (cbvmc_cardgen) */
    return CBVMC_OPEN_OK;
}

void cbvmc_resident_note(const char *text)
{
    static const char path[] = "/cubeboot/cbvmc_status.txt";
    uint32_t n = 0;
    if (!ready || !(cbvmc_diag[0] == 0x44494147u && (cbvmc_diag[7] & 0x1u))) return;      /* vmc_diag bit 1 (CBVMC_IPLD_STATUS): status files on the SD */
    while (text[n]) ++n;
    (void)lower.remove(lower.context, path);
    if (lower.create(lower.context, path) == 0) (void)lower.write(lower.context, path, 0, text, n);
    if (lower.flush) (void)lower.flush(lower.context, path);
}
/* /cubeboot/cbvmc_stages.txt: one line per executable of this boot (stage 0 = the one Cubeboot installed into, then every executable the T2 breakpoint
   caught), the oldest dropped when 768 bytes are full; rewritten whole, padded with spaces (best effort, like the counters) */
#define STAGES_BYTES 768u
static char stages[STAGES_BYTES];
static unsigned stages_n;
void cbvmc_resident_stage_note(const char *text, unsigned len, int restart)
{
    static const char path[] = "/cubeboot/cbvmc_stages.txt";
    unsigned i;
    if (!cbvmc_diag_files) return;                          /* vmc_diag NOTES bit (build 57): the stage lines are a diagnostic */
    if (restart) stages_n = 0;
    if (len > STAGES_BYTES - 1u) len = STAGES_BYTES - 1u;
    while (stages_n + len > STAGES_BYTES - 1u) {            /* drop the oldest line */
        unsigned cut = 0;
        while (cut < stages_n && stages[cut] != '\n') ++cut;
        if (cut < stages_n) ++cut;
        for (i = cut; i < stages_n; ++i) stages[i - cut] = stages[i];
        stages_n -= cut;
        if (cut == 0) break;
    }
    for (i = 0; i < len; ++i) stages[stages_n++] = text[i];
    for (i = stages_n; i < STAGES_BYTES - 1u; ++i) stages[i] = ' ';
    stages[STAGES_BYTES - 1u] = '\n';
    if (!ready) return;
    (void)lower.write(lower.context, path, 0, stages, STAGES_BYTES);
    if (lower.flush) (void)lower.flush(lower.context, path);
}
int cbvmc_resident_prepare(unsigned slot) { return slot > 1 ? -1 : cbvmc_wbmux_prepare(&mux, &slots[slot]); }
int cbvmc_resident_recover(unsigned slot) { return slot > 1 ? -1 : cbvmc_wbmux_recover(&mux, &slots[slot]); }
#ifdef CBVMC_DIAG
/* ---- every card command and every commit, kept in a ring and written to /cubeboot/cbvmc_events.bin (decode: tools/cbvmc_decode_events.py) ---- */
struct ev { uint32_t tb; uint8_t type, slot, cmd, st; uint32_t addr, info, aux; };
#define NEV 256u
static struct ev ring[NEV];
static uint32_t nev;
static uint16_t cmdcnt[2][256];
static uint32_t ev_dirty;

static void record(char type, unsigned slot, unsigned cmd, unsigned st, uint32_t addr, uint32_t info, uint32_t aux)
{
    struct ev *e = &ring[nev++ % NEV];
    __asm__ volatile("mftb %0" : "=r"(e->tb));
    e->type = (uint8_t)type; e->slot = (uint8_t)slot; e->cmd = (uint8_t)cmd; e->st = (uint8_t)st; e->addr = addr; e->info = info; e->aux = aux;
    ev_dirty = 1;
}

static uint32_t fnv32(const void *p, uint32_t n)
{
    const uint8_t *b = p;
    uint32_t h = 2166136261u;
    while (n--) { h ^= *b++; h *= 16777619u; }
    return h;
}

static void card_trace(const struct cubeboot_vmc_card *c, char type, uint32_t addr, uint32_t len, int result, const void *data)
{
    unsigned slot = (c == &cbvmc_state.port.card) ? 0u : 1u;
    if (type == 'M') {
        if (cmdcnt[slot][c->command] != 0xFFFFu) cmdcnt[slot][c->command]++;
        if (c->command == 0x83 || c->command == 0x00 || c->command == 0x85) return;                 /* status polls and ID reads are only counted */
    }
    record(type, slot, c->command, c->status, addr, type == 'M' ? len : (len & 0xFFFFu) | ((uint32_t)(result & 0xFFFF) << 16), (data && (type == 'R' || type == 'W')) ? fnv32(data, len) : 0u);
}
static void commit_trace(const struct cbvmc_wb *w, uint32_t base, uint16_t dirty, int result, int why)
{
    (void)w;
    record('C', mux.cur == &slots[1] ? 1u : 0u, (unsigned)why, result == 0 ? 0u : 0xFFu, base, dirty, 0u);
}
void (*cbvmc_resident_trace_hook(void))(const struct cubeboot_vmc_card *, char, uint32_t, uint32_t, int, const void *) { return card_trace; }

/* ---- diagnostics trace: /cubeboot/cbvmc_trace.txt, 1536 bytes rewritten in place every few seconds while the counters change (owner/console diagnosis, 2026-10-10) ---- */
static unsigned hx(char *b, unsigned n, uint32_t v)
{
    unsigned i;
    for (i = 0; i < 8; ++i) b[n++] = "0123456789abcdef"[(v >> (28 - 4 * i)) & 15u];
    return n;
}
static unsigned hx2(char *b, unsigned n, unsigned v) { b[n++] = "0123456789abcdef"[(v >> 4) & 15u]; b[n++] = "0123456789abcdef"[v & 15u]; return n; }
static unsigned dc(char *b, unsigned n, uint32_t v)
{
    char t[10]; unsigned k = 0;
    do { t[k++] = (char)('0' + v % 10u); v /= 10u; } while (v);
    while (k) b[n++] = t[--k];
    return n;
}
static unsigned sd(char *b, unsigned n, int32_t v) { if (v < 0) { b[n++] = '-'; return dc(b, n, (uint32_t)(-(int64_t)v)); } return dc(b, n, (uint32_t)v); }
static unsigned lit(char *b, unsigned n, const char *s) { while (*s) b[n++] = *s++; return n; }

static uint32_t trace_next_tb, trace_last_sum, trace_ticks;
#define TRACE_PERIOD_TB (3u * 40500000u)            /* about 3 s */

static void trace_tick(void)
{
    static char t[1536];
    const struct cbvmc_stats *s;
    const struct cbvmc_wb_stats *w = &wb.stats;
    uint32_t tbl, sum, a, b2, b3;
    unsigned n = 0;
    static const char path[] = "/cubeboot/cbvmc_trace.txt";
    if (!ready || cbvmc_state.magic != CBVMC_STATE_MAGIC) return;
    if (!cbvmc_diag_files) return;         /* build 14: periodic file writes from the handler path are off by default (they share the DI with the game's disc reads) */
    ++trace_ticks;
    __asm__ volatile("mftb %0" : "=r"(tbl));
    if ((int32_t)(tbl - trace_next_tb) < 0 && trace_ticks != 30) return;
    trace_next_tb = tbl + TRACE_PERIOD_TB;
    s = &cbvmc_state.rt.stats;
    sum = s->traps + s->exi_virtual + s->pi_reads + w->commits + w->lower_writes + w->model_writes + w->model_reads + w->commit_failures;
    if (sum == trace_last_sum && trace_ticks != 30) return;
    trace_last_sum = sum;
    memset(t, ' ', sizeof(t));
    n = lit(t, n, "cbvmc trace v1 ticks="); n = dc(t, n, trace_ticks); n = lit(t, n, "\n");
    n = lit(t, n, "traps="); n = dc(t, n, s->traps); n = lit(t, n, " exi_virt="); n = dc(t, n, s->exi_virtual); n = lit(t, n, " exi_virt_B="); n = dc(t, n, s->exi_virtual_b);
    n = lit(t, n, " fwd="); n = dc(t, n, s->exi_forwarded); n = lit(t, n, " pi="); n = dc(t, n, s->pi_reads); n = lit(t, n, " irq="); n = dc(t, n, s->pi_irq_merged);
    n = lit(t, n, " dma="); n = dc(t, n, s->dma); n = lit(t, n, " bad="); n = dc(t, n, s->bad_insn); n = lit(t, n, "\n");
    n = lit(t, n, "first pc="); n = hx(t, n, s->first_pc); n = lit(t, n, " off="); n = hx(t, n, s->first_off); n = lit(t, n, " last pc="); n = hx(t, n, s->last_pc); n = lit(t, n, "\n");
    n = lit(t, n, "wb commits="); n = dc(t, n, w->commits); n = lit(t, n, " fail="); n = dc(t, n, w->commit_failures); n = lit(t, n, " mw="); n = dc(t, n, w->model_writes);
    n = lit(t, n, " mr="); n = dc(t, n, w->model_reads); n = lit(t, n, " lw="); n = dc(t, n, w->lower_writes); n = lit(t, n, " lr="); n = dc(t, n, w->lower_reads);
    n = lit(t, n, " jw="); n = dc(t, n, w->journal_writes); n = lit(t, n, " vmis="); n = dc(t, n, w->verify_mismatches); n = lit(t, n, " err="); n = dc(t, n, wb.err); n = lit(t, n, "\n");
    a = *(volatile uint32_t *)0x80000300u; b2 = *(volatile uint32_t *)0x803551D0u; b3 = *(volatile uint32_t *)0x8035644Cu;
    n = lit(t, n, "vec300="); n = hx(t, n, a); n = lit(t, n, " verifyid="); n = hx(t, n, b2); n = lit(t, n, " domount="); n = hx(t, n, b3); n = lit(t, n, "\n");
    {   /* the game's BATs: where the I/O window really lives */
        uint32_t u0, l0, u1, l1, u2, l2, u3, l3, msr;
        __asm__ volatile("mfspr %0,536" : "=r"(u0)); __asm__ volatile("mfspr %0,537" : "=r"(l0));
        __asm__ volatile("mfspr %0,538" : "=r"(u1)); __asm__ volatile("mfspr %0,539" : "=r"(l1));
        __asm__ volatile("mfspr %0,540" : "=r"(u2)); __asm__ volatile("mfspr %0,541" : "=r"(l2));
        __asm__ volatile("mfspr %0,542" : "=r"(u3)); __asm__ volatile("mfspr %0,543" : "=r"(l3));
        __asm__ volatile("mfmsr %0" : "=r"(msr));
        n = lit(t, n, "dbat0="); n = hx(t, n, u0); n = lit(t, n, ","); n = hx(t, n, l0); n = lit(t, n, " 1="); n = hx(t, n, u1); n = lit(t, n, ","); n = hx(t, n, l1); n = lit(t, n, "\n");
        n = lit(t, n, "dbat2="); n = hx(t, n, u2); n = lit(t, n, ","); n = hx(t, n, l2); n = lit(t, n, " 3="); n = hx(t, n, u3); n = lit(t, n, ","); n = hx(t, n, l3);
        n = lit(t, n, " msr="); n = hx(t, n, msr); n = lit(t, n, "\n");
    }
    {   /* what the card model presents: size in Mbit (the ID the game reads is 0x80 then these bytes), bytes, status, encoding, sticky I/O error */
        const struct cubeboot_vmc_card *ca = &cbvmc_state.port.card;
        n = lit(t, n, "cardA mbits="); n = dc(t, n, ca->geometry.mbits); n = lit(t, n, " bytes="); n = dc(t, n, ca->image_bytes); n = lit(t, n, " st="); n = hx2(t, n, ca->status);
        n = lit(t, n, " enc="); n = dc(t, n, ca->geometry.encoding); n = lit(t, n, " ioerr="); n = dc(t, n, ca->io_error); n = lit(t, n, " lastcmd="); n = hx2(t, n, ca->command); n = lit(t, n, "\n");
    }
    n = lit(t, n, "cmds A:"); { unsigned k, m = 0; for (k = 0; k < 256 && n < 1380; ++k) if (cmdcnt[0][k]) { n = hx2(t, n, k); n = lit(t, n, "="); n = dc(t, n, cmdcnt[0][k]); n = lit(t, n, " "); if (++m > 12) break; } }
    n = lit(t, n, "\ncmds B:"); { unsigned k, m = 0; for (k = 0; k < 256 && n < 1470; ++k) if (cmdcnt[1][k]) { n = hx2(t, n, k); n = lit(t, n, "="); n = dc(t, n, cmdcnt[1][k]); n = lit(t, n, " "); if (++m > 6) break; } }
    n = lit(t, n, "\nevents="); n = dc(t, n, nev); n = lit(t, n, "\n");
    t[sizeof(t) - 1] = '\n';
    if (ev_dirty) {
        static struct { uint32_t magic, nev, tb, recsz; } hdr;
        static const char epath[] = "/cubeboot/cbvmc_events.bin";
        hdr.magic = 0x43425645u; hdr.nev = nev; hdr.tb = tbl; hdr.recsz = sizeof(struct ev);
        ev_dirty = 0;
        (void)lower.write(lower.context, epath, 0, &hdr, 16);
        (void)lower.write(lower.context, epath, 16, ring, sizeof(ring));
        if (lower.flush) (void)lower.flush(lower.context, epath);
    }
    (void)lower.write(lower.context, path, 0, t, sizeof(t));                    /* best effort: the file is created by the first write (append at 0) */
    if (lower.flush) (void)lower.flush(lower.context, path);
}

#else
/* ---- production: a few counters only (about 200 bytes of state, one small file) --------------------------------------------------------------- */
static uint32_t cnt_read_err, cnt_commit_fail;
static void card_trace(const struct cubeboot_vmc_card *c, char type, uint32_t addr, uint32_t len, int result, const void *data)
{
    (void)c; (void)addr; (void)len; (void)result; (void)data;
    if (type == 'e') cnt_read_err++;                 /* a store read the drive path failed (should stay 0) */
}
static void commit_trace(const struct cbvmc_wb *w, uint32_t base, uint16_t dirty, int result, int why)
{
    (void)w; (void)base; (void)dirty; (void)why;
    if (result != 0) cnt_commit_fail++;
}
void (*cbvmc_resident_trace_hook(void))(const struct cubeboot_vmc_card *, char, uint32_t, uint32_t, int, const void *) { return card_trace; }

static unsigned dc(char *b, unsigned n, uint32_t v)
{
    char t[10]; unsigned k = 0;
    do { t[k++] = (char)('0' + v % 10u); v /= 10u; } while (v);
    while (k) b[n++] = t[--k];
    return n;
}
static unsigned lit(char *b, unsigned n, const char *s) { while (*s) b[n++] = *s++; return n; }

static unsigned hx(char *b, unsigned n, uint32_t v, unsigned digits)
{
    while (digits--) b[n++] = "0123456789abcdef"[(v >> (4u * digits)) & 15u];
    return n;
}
extern uint32_t cbvmc_depth, cbvmc_depth_max, cbvmc_fix_pending;      /* blob (T2 build): handler nesting, repair flag */
static uint32_t diag_seq;

static uint32_t trace_next_tb, trace_last_sum, trace_ticks;
#define TRACE_PERIOD_TB (20u * 40500000u)             /* about 20 s */

/* /cubeboot/cbvmc_counters.txt, rewritten in place (512 bytes) when something changed: did the game use the card, did commits and reads go through */
static void trace_tick(void)
{
    static char t[512];                      /* 256 before the T2 line, 384 before the diag line */
    const struct cbvmc_wb_stats *w = &wb.stats;
    const struct cbvmc_stats *s;
    uint32_t tbl, sum;
    unsigned n = 0;
    static const char path[] = "/cubeboot/cbvmc_counters.txt";
    if (!ready || cbvmc_state.magic != CBVMC_STATE_MAGIC) return;
    if (!cbvmc_diag_files) return;         /* build 14: periodic file writes from the handler path are off by default (they share the DI with the game's disc reads) */
    ++trace_ticks;
    __asm__ volatile("mftb %0" : "=r"(tbl));
    if ((int32_t)(tbl - trace_next_tb) < 0) return;
    trace_next_tb = tbl + TRACE_PERIOD_TB;
    s = &cbvmc_state.rt.stats;
    sum = s->traps + w->commits + w->model_writes + w->commit_failures + cnt_read_err + w->read_retries + cbvmc_t2_state.traps + cbvmc_t2_state.stages + s->pi_reads;
    if (sum == trace_last_sum) return;
    trace_last_sum = sum;
    memset(t, ' ', sizeof(t));
    n = lit(t, n, "cbvmc counters v1\ntraps="); n = dc(t, n, s->traps); n = lit(t, n, " exi_virt="); n = dc(t, n, s->exi_virtual); n = lit(t, n, " virt_B="); n = dc(t, n, s->exi_virtual_b);
    n = lit(t, n, " bad="); n = dc(t, n, s->bad_insn); n = lit(t, n, "\ncommits="); n = dc(t, n, w->commits); n = lit(t, n, " commit_fail="); n = dc(t, n, w->commit_failures);
    n = lit(t, n, " read_err="); n = dc(t, n, cnt_read_err); n = lit(t, n, " read_retry="); n = dc(t, n, w->read_retries); n = lit(t, n, " wb_err="); n = dc(t, n, wb.err);
    n = lit(t, n, " mw="); n = dc(t, n, w->model_writes); n = lit(t, n, "\n");
    /* T2 (cbvmc_t2.h): breakpoint traps, executables caught after the first (OWN = table applied, KEEP = refused), stores not taken apart, bad chain words, failed commits at a change */
    n = lit(t, n, "t2 armed="); n = dc(t, n, cbvmc_t2_state.armed); n = lit(t, n, " traps="); n = dc(t, n, cbvmc_t2_state.traps); n = lit(t, n, " stages="); n = dc(t, n, cbvmc_t2_state.stages);
    n = lit(t, n, " own="); n = dc(t, n, cbvmc_t2_state.stages_own); n = lit(t, n, " keep="); n = dc(t, n, cbvmc_t2_state.stages_keep); n = lit(t, n, " allow="); n = dc(t, n, cbvmc_t2_state.stages_allow); n = lit(t, n, " unsup="); n = dc(t, n, cbvmc_t2_state.unsupported);
    n = lit(t, n, " chain_bad="); n = dc(t, n, cbvmc_t2_state.chain_bad); n = lit(t, n, " sync_fail="); n = dc(t, n, cbvmc_t2_state.sync_fail); n = lit(t, n, "\n");
    {   /* freeze diagnostics (build 3): where the card traffic stands right now, per slot: last card command, card status, EXI CSR as the model holds it, chip
           select, the virtual interrupt line; the last line levels the PI merge saw, PI reads and merged interrupts, the last trapped pc, handler nesting
           (max above 1 = self-trap), the repair flag */
        const struct cbvmc_rt *rt = &cbvmc_state.rt;
        n = lit(t, n, "diag lastcmd_a="); n = hx(t, n, cbvmc_state.port.card.command, 2); n = lit(t, n, " st_a="); n = hx(t, n, cbvmc_state.port.card.status, 2);
        n = lit(t, n, " csr_a="); n = hx(t, n, rt->exi.csr, 4); n = lit(t, n, " sel_a="); n = dc(t, n, rt->exi.cs0_selected);
        n = lit(t, n, " int_a="); n = dc(t, n, rt->exi.port ? (uint32_t)cubeboot_vmc_exi_irq((struct cubeboot_vmc_exi *)&rt->exi) : 0u);
        n = lit(t, n, " lastcmd_b="); n = hx(t, n, port_b.card.command, 2); n = lit(t, n, " st_b="); n = hx(t, n, port_b.card.status, 2);
        n = lit(t, n, " csr_b="); n = hx(t, n, rt->exi1.csr, 4); n = lit(t, n, " sel_b="); n = dc(t, n, rt->exi1.cs0_selected);
        n = lit(t, n, " int_b="); n = dc(t, n, rt->exi1.port ? (uint32_t)cubeboot_vmc_exi_irq((struct cubeboot_vmc_exi *)&rt->exi1) : 0u);
        n = lit(t, n, "\nint_pending="); n = dc(t, n, rt->exi_line_prev); n = lit(t, n, " pi="); n = dc(t, n, s->pi_reads); n = lit(t, n, " pi_irq="); n = dc(t, n, s->pi_irq_merged);
        n = lit(t, n, " last_pc="); n = hx(t, n, s->last_pc, 8); n = lit(t, n, " in_handler="); n = dc(t, n, cbvmc_depth); n = lit(t, n, " max="); n = dc(t, n, cbvmc_depth_max);
        n = lit(t, n, " fixp="); n = dc(t, n, cbvmc_fix_pending); n = lit(t, n, " seq="); n = dc(t, n, ++diag_seq);
        /* the OS interrupt masks the SDK dispatcher ANDs the cause with (low memory 0x800000C4 global, 0x800000C8 local) and their neighbours */
        n = lit(t, n, "\nosmask="); n = hx(t, n, *(volatile uint32_t *)0x800000C4u, 8); n = lit(t, n, " local="); n = hx(t, n, *(volatile uint32_t *)0x800000C8u, 8);
        n = lit(t, n, " c0="); n = hx(t, n, *(volatile uint32_t *)0x800000C0u, 8); n = lit(t, n, " cc="); n = hx(t, n, *(volatile uint32_t *)0x800000CCu, 8);
        {   /* the SDK's interrupt handler table (fixed low memory 0x80003040, 4 bytes per interrupt): the EXI entries 9..16 */
            unsigned k;
            n = lit(t, n, "\nexi_handlers");
            for (k = 9; k <= 16u; ++k) { n = lit(t, n, " "); n = hx(t, n, *(volatile uint32_t *)(0x80003040u + 4u * k), 8); }
            n = lit(t, n, "\n");
        }
    }
    t[sizeof(t) - 1] = '\n';
    (void)lower.write(lower.context, path, 0, t, sizeof(t));
    if (lower.flush) (void)lower.flush(lower.context, path);
    {   /* /cubeboot/cbvmc_lasttraps.txt (build 4): the last 16 handled accesses, oldest first: pc, register offset (S = store), value; a game waiting
           on the card shows its polling loop here (which registers it reads, what our model answers) */
        static char lt[512];
        static const char lpath[] = "/cubeboot/cbvmc_lasttraps.txt";
        const struct cbvmc_rt *rt = &cbvmc_state.rt;
        unsigned k, m = 0;
        memset(lt, ' ', sizeof(lt));
        m = lit(lt, m, "last traps, oldest first: pc off val\n");
        for (k = 0; k < CBVMC_LAST_N; ++k) {
            uint32_t j = (rt->last_i + k) % CBVMC_LAST_N;
            if (rt->last_i < CBVMC_LAST_N && j >= rt->last_i) continue;
            m = hx(lt, m, rt->last[j].pc, 8); lt[m++] = ' '; lt[m++] = (rt->last[j].off & 0x80000000u) ? 'S' : 'L';
            m = hx(lt, m, rt->last[j].off & 0xFFFFu, 4); lt[m++] = ' '; m = hx(lt, m, rt->last[j].val, 8); lt[m++] = '\n';
        }
        lt[sizeof(lt) - 1] = '\n';
        (void)lower.write(lower.context, lpath, 0, lt, sizeof(lt));
        if (lower.flush) (void)lower.flush(lower.context, lpath);
    }
}

#ifdef CBVMC_STALL
/* ---- stall snapshot (console diagnosis 2026-10-10: the game sits at "Create Game Data?" and A does nothing) ----------------------------------
   /cubeboot/cbvmc_stall.bin, 2048 bytes of big-endian words rewritten in place about every 15 s (decode: tools/cbvmc_decode_stall.py): where the CPU was when the
   interrupt that called us hit (the interrupted OSContext), every thread (state, queue it sleeps on, saved pc/lr/sp), the real DI/PI/SI/EXI/VI registers
   (read only), our virtual channel state, and the OS thread list.  Read-only except for the file; no card behaviour changes. */
#define SW_WORDS 512u
static uint32_t sw[SW_WORDS];
static uint32_t stall_next_tb, stall_n;
#define STALL_PERIOD_TB (15u * 40500000u)
static uint32_t rd32(uint32_t a) { return *(volatile uint32_t *)a; }
static int okp(uint32_t p) { return p >= 0x80000000u && p < 0x81800000u && (p & 3u) == 0; }

static void stall_tick(void)
{
    static const uint16_t regs[] = { 0x6000, 0x6004, 0x6008, 0x600C, 0x6010, 0x6014, 0x6018, 0x601C,          /* DI: SR CVR CMD0-2 MAR LEN CR */
                                     0x3000, 0x3004, 0x3018,                                                  /* PI: INTSR INTMR, reset */
                                     0x6400, 0x6404, 0x6408, 0x6430, 0x6434, 0x6438,                          /* SI: out0 in0h in0l poll comcsr sisr */
                                     0x6800, 0x680C, 0x6814, 0x6820, 0x6828, 0x6834,                          /* EXI real: csr0 cr0 csr1 cr1 csr2 cr2 */
                                     0x2030, 0x2034, 0x2038, 0x203C,                                          /* VI: DI0-3 */
                                     0x6C00 };                                                                /* AI control */
    uint32_t tbl, i = 0, k, th, ctx, ex, n;
    const struct cbvmc_stats *s;
    if (!ready || cbvmc_state.magic != CBVMC_STATE_MAGIC) return;
    if (!cbvmc_diag_files) return;         /* build 14: periodic file writes from the handler path are off by default (they share the DI with the game's disc reads) */
    ++stall_n;
    __asm__ volatile("mftb %0" : "=r"(tbl));
    if ((int32_t)(tbl - stall_next_tb) < 0) return;
    stall_next_tb = tbl + STALL_PERIOD_TB;
    s = &cbvmc_state.rt.stats;
    memset(sw, 0, sizeof(sw));
    sw[i++] = 0x5354414Cu; sw[i++] = 1; sw[i++] = stall_n; sw[i++] = tbl;
    sw[i++] = s->traps; sw[i++] = s->exi_virtual; sw[i++] = s->exi_virtual_b; sw[i++] = s->exi_forwarded; sw[i++] = s->pi_reads; sw[i++] = s->pi_irq_merged;
    sw[i++] = s->dma; sw[i++] = s->first_pc; sw[i++] = s->first_off; sw[i++] = s->last_pc; sw[i++] = s->bad_insn;
    { uint32_t msr, dec; __asm__ volatile("mfmsr %0" : "=r"(msr)); __asm__ volatile("mfspr %0,22" : "=r"(dec)); sw[i++] = msr; sw[i++] = dec; }
    /* model: slot A (exi), slot B (exi1): csr cr cs0 mar length, card status/command/io_error */
    sw[i++] = cbvmc_state.rt.exi.csr; sw[i++] = cbvmc_state.rt.exi.cr; sw[i++] = cbvmc_state.rt.exi.cs0_selected; sw[i++] = cbvmc_state.rt.exi.mar; sw[i++] = cbvmc_state.rt.exi.length;
    sw[i++] = cbvmc_state.port.card.status; sw[i++] = cbvmc_state.port.card.command; sw[i++] = cbvmc_state.port.card.io_error;
    sw[i++] = cbvmc_state.rt.exi1.csr; sw[i++] = cbvmc_state.rt.exi1.cr; sw[i++] = cbvmc_state.rt.exi1.cs0_selected; sw[i++] = cbvmc_state.rt.exi1.mar; sw[i++] = cbvmc_state.rt.exi1.length;
    sw[i++] = port_b.card.status; sw[i++] = port_b.card.command; sw[i++] = port_b.card.io_error;
    sw[i++] = wb.stats.commits; sw[i++] = wb.err; sw[i++] = wb.stats.model_writes; sw[i++] = wb.stats.lower_reads; sw[i++] = wb.stats.read_retries;
    /* real registers (reads of the 0xCC00xxxx page; the same reads the game does) */
    sw[i++] = (uint32_t)(sizeof(regs) / sizeof(regs[0]));
    for (k = 0; k < sizeof(regs) / sizeof(regs[0]); ++k) sw[i++] = rd32(0xCC000000u + regs[k]);
    /* OS: __OSCurrentContext, current thread, active thread queue head/tail */
    ctx = rd32(0x800000D4u); th = rd32(0x800000E4u);
    sw[i++] = ctx; sw[i++] = th; sw[i++] = rd32(0x800000DCu); sw[i++] = rd32(0x800000E0u);
    /* where the CPU was when the interrupt that called us hit: the interrupted context (srr0 srr1 lr sp r3 r4) */
    if (okp((ctx & 0x7FFFFFFFu) | 0x80000000u)) { uint32_t c = (ctx & 0x7FFFFFFFu) | 0x80000000u; sw[i++] = rd32(c + 0x198); sw[i++] = rd32(c + 0x19C); sw[i++] = rd32(c + 0x84); sw[i++] = rd32(c + 4); sw[i++] = rd32(c + 12); sw[i++] = rd32(c + 16); }
    else i += 6;
    /* threads: walk the active list (head @800000DC, linkActive.next at +0x2FC): addr, state<<16|attr, priority, queue, srr0, lr, sp */
    n = 0;
    {   uint32_t t = rd32(0x800000DCu), guard = 0;
        uint32_t cnt_at = i++;
        while (okp(t) && guard++ < 20u && i + 7u <= SW_WORDS) {
            sw[i++] = t; sw[i++] = rd32(t + 0x2C8); sw[i++] = rd32(t + 0x2D0); sw[i++] = rd32(t + 0x2DC);
            sw[i++] = rd32(t + 0x198); sw[i++] = rd32(t + 0x84); sw[i++] = rd32(t + 4);
            ++n; t = rd32(t + 0x2FC);
        }
        sw[cnt_at] = n;
    }
    {   static const char path[] = "/cubeboot/cbvmc_stall.bin";
        (void)lower.write(lower.context, path, 0, sw, sizeof(sw));
        if (lower.flush) (void)lower.flush(lower.context, path);
    }
}
#endif

#endif

void cbvmc_resident_diag(void) { trace_tick(); }
void cbvmc_resident_tick(void *context)
{
    cbvmc_rt_observe_physical(&cbvmc_state.rt);
    cbvmc_wb_tick((struct cbvmc_wb *)context);
    trace_tick();
#ifdef CBVMC_STALL
    stall_tick();
#endif
}
void *cbvmc_resident_tick_context(void) { return &wb; }
int cbvmc_resident_sync(void) { return cbvmc_wbmux_sync(&mux); }
uint32_t cbvmc_resident_stack_top(void) { return (uint32_t)hstack + sizeof(hstack); }
