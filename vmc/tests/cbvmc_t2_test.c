/* Copyright (C) 2026 Nigzel / Bjerreman. SPDX-License-Identifier: GPL-2.0-only */
/* Host test of the T2 multi-executable trigger (cbvmc_t2.c): synthetic breakpoint DSI frames for the stub copy over 0x80000300, a session of several
 * executables placed in one RAM image, the EXI channel reset on the real card model.
 *   cbvmc_t2_test MELEE_DOL [OTHER_DOL ...]      (a missing file is skipped; a relocated copy of the first DOL is always used as a second image) */
#include "cbvmc_t2_util.h"
#include "cbvmc_t2.h"
#include "cbvmc_scan.h"
#include "cbvmc_words.h"
#include "cbvmc_ramstore.h"
#include "cubeboot_vmc.h"
#include "cubeboot_vmc_exi.h"

/* where the synthetic copy loop's store sits.  Build 2 treats a pc outside [0x80003100, 0x81200000) as boot/reboot code (ALLOW), so the store is placed
   in game text: 0x80100000 for the unit groups (r1 = r13 = 0, no window), the last word of the executable's first text section for the session groups. */
#define INSN_UNIT 0x80100000u
static uint32_t INSN_AT = INSN_UNIT;
#define DSISR_ST 0x02400000u       /* console value for a store that matched DABR */

/* ---- fake CPU / resident glue ---- */
struct fk {
    uint32_t dabr, dabr_sets, syncs, resets, notes, flushes, tick;
    int sync_rc;
    char ev[256]; unsigned nev;    /* S sync, R reset, N note: order of the stage-change work */
    char note[256];
    struct cubeboot_vmc_exi *exi;
};
static struct fk FK;
static uint32_t CHAIN, FIXP;
static uint8_t *MEM;
static uint8_t SNAP[CBVMC_T2_SNAP_BYTES];

static void ev(char c) { if (FK.nev + 1 < sizeof(FK.ev)) { FK.ev[FK.nev++] = c; FK.ev[FK.nev] = 0; } }
static void f_flush(void *c, uint32_t a, uint32_t n) { (void)c; (void)a; (void)n; FK.flushes++; }
static void f_dabr(void *c, uint32_t v) { (void)c; FK.dabr = v; FK.dabr_sets++; }
static int f_sync(void *c) { (void)c; FK.syncs++; ev('S'); return FK.sync_rc; }
static void f_reset(void *c) { (void)c; FK.resets++; ev('R'); if (FK.exi) cbvmc_t2_exi_reset(FK.exi); }
static void f_note(void *c, const char *s, unsigned n) { (void)c; FK.notes++; ev('N'); CHECK(n < sizeof(FK.note)); memcpy(FK.note, s, n); FK.note[n] = 0; }
static uint32_t f_now(void *c) { (void)c; return FK.tick += 7; }

static struct cbvmc_t2 T;

/* fresh trigger over MEM, armed, with our `ba cbvmc_vec` stand-in at 0x300 and the old stub's first word in the chain */
static void t2_setup(void)
{
    struct cbvmc_t2_env env;
    struct cubeboot_vmc_exi *x = FK.exi;
    memset(&FK, 0, sizeof(FK)); FK.exi = x;
    memset(&env, 0, sizeof(env));
    env.mem = MEM; env.mem_size = RAMSZ; env.chain = &CHAIN; env.fix_pending = &FIXP;
    env.flush = f_flush; env.set_dabr = f_dabr; env.sync = f_sync; env.card_reset = f_reset; env.stage_note = f_note; env.now = f_now;
    CHAIN = 0x7C9043A6u; FIXP = 7;
    wr(MEM, 0x80000300u, 0x48001802u);                 /* ba 0x1800 (any absolute branch: only its bytes matter) */
    wr(MEM, 0x80000304u, 0x808000C0u);
    cbvmc_t2_init(&T, &env);
    cbvmc_t2_arm(&T, SNAP);
    CHECK(T.armed == 1 && FK.dabr == CBVMC_T2_DABR_VALUE && FIXP == 0);
}

/* one breakpoint trap: store `insn` at INSN_AT, registers as given; returns the handler's result */
static int trap1(uint32_t insn, struct cbvmc_ctx *x, uint32_t dar)
{
    wr(MEM, INSN_AT, insn);
    x->srr0 = INSN_AT; x->dar = dar; x->dsisr = DSISR_ST;
    CHECK(CBVMC_T2_MATCH(dar, DSISR_ST));
    return cbvmc_t2_trap(&T, x);
}

enum form { F_STBU, F_STB, F_STBX, F_STBUX, F_STWU, F_STW, F_STWX, F_STWUX, F_N };

/* An OSExceptionInit-style copy of `n` bytes from `src` to 0x80000300 with the store form `f`.  Stores that hit 0x300..0x307 go through the
   handler (they would trap); the rest are written directly.  Checks the swallow and the base-register update of every trapped store.  Returns the trap count. */
static unsigned osinit_copy(const uint8_t *src, unsigned n, enum form f, uint32_t r1, uint32_t r13)
{
    unsigned i, traps = 0, step = (f >= F_STWU) ? 4u : 1u;
    uint32_t r6 = 0x80000300u - step;
    for (i = 0; i < n; i += step) {
        uint32_t dst = 0x80000300u + i, v = step == 4 ? be(src + i) : src[i], insn = 0, upd = 0;
        if (dst >= 0x80000308u) {
            if (step == 4) wr(MEM, dst, v); else MEM[dst - RAMB] = (uint8_t)v;
            r6 = dst;
            continue;
        }
        {
            struct cbvmc_ctx x;
            memset(&x, 0, sizeof(x));
            x.gpr[0] = v; x.gpr[1] = r1; x.gpr[13] = r13; x.gpr[6] = r6; x.gpr[7] = 0;
            switch (f) {
            case F_STBU: insn = enc_d(OP_STBU, 0, 6, 1); upd = 1; break;
            case F_STWU: insn = enc_d(OP_STWU, 0, 6, 4); upd = 1; break;
            case F_STB:  insn = enc_d(OP_STB, 0, 6, (int)step); break;
            case F_STW:  insn = enc_d(OP_STW, 0, 6, (int)step); break;
            case F_STBX: insn = enc_x(XO_STBX, 0, 6, 7); x.gpr[7] = step; break;
            case F_STWX: insn = enc_x(XO_STWX, 0, 6, 7); x.gpr[7] = step; break;
            case F_STBUX: insn = enc_x(XO_STBUX, 0, 6, 7); x.gpr[7] = step; upd = 1; break;
            case F_STWUX: insn = enc_x(XO_STWUX, 0, 6, 7); x.gpr[7] = step; upd = 1; break;
            default: CHECK(0);
            }
            CHECK(trap1(insn, &x, dst) == 0);
            CHECK(x.srr0 == INSN_AT + 4);                       /* swallowed */
            CHECK(x.gpr[6] == (upd ? dst : r6));                 /* base update only for the update forms */
            CHECK(x.gpr[0] == v && x.gpr[7] == (f == F_STBX || f == F_STWX || f == F_STBUX || f == F_STWUX ? step : 0u));
            r6 = dst;
            traps++;
        }
    }
    return traps;
}

/* ---- group 1: every supported store form, window refused (r1 = r13 = 0) so no scan runs ---- */
static void test_forms(void)
{
    uint8_t stub[0x98];
    unsigned i, f;
    for (i = 0; i < sizeof(stub); ++i) stub[i] = (uint8_t)(0x40 + i);
    stub[0] = 0x7C; stub[1] = 0x91; stub[2] = 0x43; stub[3] = 0xA6;          /* mtsprg 1,r4: a valid chain word that differs from the old one */
    stub[4] = 0x11; stub[5] = 0x22; stub[6] = 0x33; stub[7] = 0x44;
    for (f = 0; f < F_N; ++f) {
        unsigned n;
        memset(MEM, 0, 0x4000);
        t2_setup();
        n = osinit_copy(stub, sizeof(stub), (enum form)f, 0, 0);
        CHECK(n == (f >= F_STWU ? 2u : 8u) && T.traps == n);
        CHECK(rd(MEM, 0x80000300u) == 0x48001802u);                          /* our vector word stays */
        CHECK(CHAIN == 0x7C9143A6u);                                         /* 0x300..0x303 went to the chain word */
        CHECK(rd(MEM, 0x80000304u) == 0x11223344u);                          /* 0x304..0x307 written normally */
        CHECK(memcmp(MEM + 0x308, stub + 8, sizeof(stub) - 8) == 0);
        CHECK(FIXP == 1 && T.active == 0 && T.mask == 0xFFu);                /* repair flagged after the eighth byte */
        CHECK(T.stages == 1 && T.stages_keep == 1 && T.stages_own == 0 && T.log[0].why == CBVMC_T2_WHY_WINDOW && T.log[0].mode == CBVMC_T2_KEEP);
        CHECK(T.armed == 1 && FK.dabr == CBVMC_T2_DABR_VALUE);              /* breakpoint stays armed for the next executable */
        CHECK(FK.syncs == 1 && FK.resets == 1 && FK.notes == 1 && strcmp(FK.ev, "SRN") == 0);
        CHECK(T.unsupported == 0 && T.chain_bad == 0 && T.outside == 0);
    }
    printf("  forms: stb/stbu/stbx/stbux/stw/stwu/stwx/stwux swallowed, base update only for update forms, chain word and 0x304..0x307 correct\n");
}

/* ---- group 2: chain word rules ---- */
static void test_chain_reject(void)
{
    static const uint32_t bad[] = { 0x00000000u, 0x48000100u, 0x4BFFFF00u, 0x41820010u, 0x44000002u, 0x4E800020u, 0x4C000064u };
    uint8_t stub[16];
    unsigned i;
    for (i = 0; i < sizeof(bad) / sizeof(bad[0]); ++i) {
        memset(MEM, 0, 0x4000);
        t2_setup();
        stub[0] = (uint8_t)(bad[i] >> 24); stub[1] = (uint8_t)(bad[i] >> 16); stub[2] = (uint8_t)(bad[i] >> 8); stub[3] = (uint8_t)bad[i];
        memset(stub + 4, 0xAB, 12);
        CHECK(osinit_copy(stub, sizeof(stub), F_STBU, 0, 0) == 8);
        CHECK(CHAIN == 0x7C9043A6u && T.chain_bad == 1);                     /* old chain word kept */
        CHECK(rd(MEM, 0x80000300u) == 0x48001802u && rd(MEM, 0x80000304u) == 0xABABABABu);
    }
    printf("  chain: zero, b, bc, sc and opcode-19 first words rejected, old chain word kept\n");
}

/* ---- group 3: stores the handler cannot take apart: returns 1, disarms, leaves SRR0 and memory alone ---- */
static void test_unsupported(void)
{
    static const uint32_t ins[] = { 0xBC060000u /* stmw r0,0(r6) */, 0xD8060000u /* stfd f0,0(r6) */, 0x80060000u /* lwz r0,0(r6) */, 0x7C0037ECu /* dcbz r0,r6 */ };
    unsigned i;
    for (i = 0; i < sizeof(ins) / sizeof(ins[0]) + 1; ++i) {
        struct cbvmc_ctx x;
        uint8_t before[0x400];
        int rc;
        memset(MEM, 0, 0x4000);
        t2_setup();
        memcpy(before, MEM, sizeof(before));
        memset(&x, 0, sizeof(x));
        x.gpr[6] = 0x80000300u;
        if (i < sizeof(ins) / sizeof(ins[0])) rc = trap1(ins[i], &x, 0x80000300u);
        else { wr(MEM, INSN_AT, enc_d(OP_STB, 0, 6, 0)); x.srr0 = INSN_AT + 2; x.dar = 0x80000300u; x.dsisr = DSISR_ST; rc = cbvmc_t2_trap(&T, &x); }   /* misaligned pc */
        CHECK(rc == 1);
        CHECK(T.armed == 0 && FK.dabr == 0 && T.unsupported == 1);
        CHECK(x.srr0 == (i < sizeof(ins) / sizeof(ins[0]) ? INSN_AT : INSN_AT + 2) && x.gpr[6] == 0x80000300u);
        CHECK(memcmp(before, MEM, sizeof(before)) == 0 && CHAIN == 0x7C9043A6u && T.stages == 0 && FK.syncs == 0);
    }
    printf("  unsupported: stmw, stfd, a load, dcbz and a misaligned pc return 1 and disarm, nothing written\n");
}

/* ---- group 4: an unaligned word store reaching past 0x307: the outside bytes are ordinary memory ---- */
static void test_outside(void)
{
    struct cbvmc_ctx x;
    memset(MEM, 0, 0x4000);
    t2_setup();
    memset(&x, 0, sizeof(x));
    x.gpr[0] = 0xA1B2C3D4u; x.gpr[6] = 0x80000306u;
    CHECK(trap1(enc_d(OP_STW, 0, 6, 0), &x, 0x80000306u) == 0 && x.srr0 == INSN_AT + 4);
    CHECK(MEM[0x306] == 0xA1 && MEM[0x307] == 0xB2 && MEM[0x308] == 0xC3 && MEM[0x309] == 0xD4 && T.outside == 2);
    CHECK(T.mask == 0xC0u && T.active == 1 && T.stages == 1 && FIXP == 0);
    printf("  outside: bytes past the doubleword written for real, stage still open\n");
}

/* ---- group 5: window rule, sync failure, status line ---- */
static void test_window_sync_line(void)
{
    uint32_t lo = 1, hi = 1;
    uint8_t stub[8] = { 0x7C, 0x90, 0x43, 0xA6, 1, 2, 3, 4 };
    char line[256];
    CHECK(cbvmc_t2_window(0x804EEC00u, 0x804DB6A0u, 0x81800000u, &lo, &hi) == 0 && lo == 0x80003100u && hi == 0x804DB6A0u);   /* min(r1, r13) */
    CHECK(cbvmc_t2_window(0x804EEC00u, 0x80004000u, 0x81800000u, &lo, &hi) == 0 && hi == 0x804EEC00u);                       /* r13 too low: r1 */
    CHECK(cbvmc_t2_window(0x80003200u, 0x804DB6A2u, 0x81800000u, &lo, &hi) == 0 && hi == 0x804DB6A0u);                       /* r1 too low: r13, aligned */
    lo = hi = 1;
    CHECK(cbvmc_t2_window(0, 0, 0x81800000u, &lo, &hi) != 0 && lo == 1 && hi == 1);
    CHECK(cbvmc_t2_window(0x81900000u, 0x00001000u, 0x81800000u, &lo, &hi) != 0);                                           /* above memory / not an address */
    CHECK(cbvmc_t2_window(0x80003100u, 0x80003100u, 0x81800000u, &lo, &hi) != 0);
    memset(MEM, 0, 0x4000);
    t2_setup();
    FK.sync_rc = -5;
    CHECK(osinit_copy(stub, sizeof(stub), F_STBU, 0x10u, 0x20u) == 8);
    CHECK(T.sync_fail == 1 && T.log[0].sync_rc == 5 && T.stages == 1 && T.armed == 1);
    CHECK(cbvmc_t2_line(&T.log[0], line, sizeof(line)) == strlen(line) && strcmp(line, FK.note) == 0);
#define LINE0 "stage 1 KEEP pc 80100000 r1 00000010 r13 00000020 window 00000000-00000000 words 0 hook 00000000 sync 5 ticks "
    CHECK(strncmp(line, LINE0, sizeof(LINE0) - 1) == 0);
    CHECK(line[strlen(line) - 1] == '\n');
    CHECK(cbvmc_t2_line(&T.log[0], line, 10) == 9 && strlen(line) == 9 && cbvmc_t2_line(&T.log[0], line, 0) == 0);
    printf("  window: min(r1, r13) with plausibility limits; sync failure counted; status line: %s", FK.note);
}

/* ---- group 6: the EXI channel reset on the real card model; the shared card survives stages ---- */
static void *x_translate(void *c, uint32_t a, uint32_t n) { (void)c; (void)n; return MEM + (a & 0x01FFFFFFu); }
static uint8_t CARDIMG[0x80000];
static void test_exi_reset(void)
{
    static struct cbvmc_ramstore rs;
    static struct cubeboot_vmc_store st;
    static struct cubeboot_vmc_port port;
    static struct cubeboot_vmc_exi exi;
    static uint8_t ref[sizeof(CARDIMG)];
    struct cubeboot_vmc_identity id = { 0x1122334455667788ull, 0x99aabbccddeeff00ull };
    struct cubeboot_vmc_geometry g = { 4, CUBEBOOT_VMC_ENC_WINDOWS_1252 };
    uint8_t stub[8] = { 0x7C, 0x90, 0x43, 0xA6, 9, 9, 9, 9 };
    unsigned k;
    const char *path = "/cubeboot/saves/MemoryCardA.USA.raw";
    memset(CARDIMG, 0x5a, sizeof(CARDIMG));
    CHECK(cbvmc_ramstore_init(&rs, &st, CARDIMG, sizeof(CARDIMG), path) == 0);
    CHECK(cubeboot_vmc_port_init(&port, &st, CUBEBOOT_VMC_SLOT_A, path, &id, &g) == 0);
    CHECK(cubeboot_vmc_port_service(&port) == CUBEBOOT_VMC_EVENT_VIRTUAL_INSERTED && port.source == CUBEBOOT_VMC_SOURCE_VIRTUAL);
    cubeboot_vmc_exi_init(&exi, &port, x_translate, 0);
    memcpy(ref, CARDIMG, sizeof(ref));
    FK.exi = &exi;
    for (k = 0; k < 3; ++k) {
        uint8_t status;
        /* the previous executable left the card selected in the middle of a status read, with registers set */
        cubeboot_vmc_card_select(&port.card, 1); exi.cs0_selected = 1;
        (void)cubeboot_vmc_card_transfer_byte(&port.card, 0x83);
        exi.csr = 0x1234u; exi.mar = 0x00123400u; exi.length = 0x200u; exi.cr = 3u; exi.data = 0xDEADBEEFu;
        status = port.card.status;
        memset(MEM, 0, 0x4000);
        t2_setup();
        CHECK(osinit_copy(stub, sizeof(stub), F_STBU, 0, 0) == 8);
        CHECK(FK.resets == 1 && exi.cs0_selected == 0 && port.card.selected == 0);
        CHECK(exi.csr == 0 && exi.mar == 0 && exi.length == 0 && exi.cr == 0 && exi.data == 0);
        CHECK(exi.port == &port && exi.translate == x_translate);
        CHECK(port.source == CUBEBOOT_VMC_SOURCE_VIRTUAL && port.card.status == status && strcmp(port.card.path, path) == 0);
        CHECK(memcmp(ref, CARDIMG, sizeof(ref)) == 0);                       /* the one shared card, unchanged by the stage change */
    }
    FK.exi = 0;
    {   /* a channel without a port is left alone */
        struct cubeboot_vmc_exi none;
        memset(&none, 0, sizeof(none)); none.csr = 5;
        cbvmc_t2_exi_reset(&none);
        CHECK(none.csr == 5);
    }
    printf("  exi: three stage changes deselect and reset the channel model, card image, status and path unchanged\n");
}

/* ---- group 7: header scan (DOL text sections) == window scan ([0x80003100, min(r1, r13)), data and bss included) ---- */
static void scan_hdr(const struct exe *e, unsigned flags, struct cbvmc_scan_result *r)
{
    struct cbvmc_range rg[7];
    unsigned i;
    for (i = 0; i < e->ntext; ++i) { rg[i].lo = e->text_lo[i]; rg[i].hi = e->text_hi[i]; }
    cbvmc_scan_ex(MEM, RAMB, rg, e->ntext, flags, r);
}
static void scan_win(const struct exe *e, unsigned flags, struct cbvmc_scan_result *r)
{
    struct cbvmc_range w;
    CHECK(cbvmc_t2_window(e->r1, e->r13, RAMB + RAMSZ, &w.lo, &w.hi) == 0);
    cbvmc_scan_ex(MEM, RAMB, &w, 1, flags, r);
}
static int same_table(const struct cbvmc_scan_result *a, const struct cbvmc_scan_result *b)
{
    return a->ok == b->ok && a->refuse == b->refuse && a->nwords == b->nwords && a->hook_addr == b->hook_addr && a->hook_target == b->hook_target &&
           memcmp(a->words, b->words, a->nwords * sizeof(a->words[0])) == 0;
}
static struct cbvmc_scan_result HDR[8], R1, R2;

static void test_corpus(struct exe *ex, unsigned n)
{
    unsigned i, k;
    for (i = 0; i < n; ++i) {
        memset(MEM, 0, RAMSZ);
        exe_load(&ex[i], MEM);
        scan_hdr(&ex[i], 0, &HDR[i]);
        CHECK(HDR[i].ok == 1);
        scan_win(&ex[i], 0, &R1);
        scan_win(&ex[i], CBVMC_SCAN_IDEMPOTENT, &R2);
        if (!same_table(&HDR[i], &R1) || !same_table(&HDR[i], &R2)) {
            fprintf(stderr, "%s: window scan differs: hdr ok=%d n=%u refuse=%u, win ok=%d n=%u refuse=%u, idem ok=%d n=%u refuse=%u\n", ex[i].name,
                    HDR[i].ok, HDR[i].nwords, HDR[i].refuse, R1.ok, R1.nwords, R1.refuse, R2.ok, R2.nwords, R2.refuse);
            CHECK(0);
        }
        /* idempotence: after applying the table a re-scan finds the same sites, every row a no-op, and applying it changes nothing */
        CHECK(cbvmc_words_apply(MEM, RAMB, RAMSZ, HDR[i].words, HDR[i].nwords, 0) == CBVMC_WORDS_OK);
        scan_win(&ex[i], CBVMC_SCAN_IDEMPOTENT, &R2);
        CHECK(R2.ok == 1 && R2.nwords == HDR[i].nwords && R2.hook_addr == HDR[i].hook_addr);
        for (k = 0; k < R2.nwords; ++k)
            CHECK(R2.words[k].addr == HDR[i].words[k].addr && R2.words[k].orig == HDR[i].words[k].patched && R2.words[k].patched == R2.words[k].orig);
        scan_win(&ex[i], 0, &R1);
        CHECK(R1.ok == 0);                                   /* without the flag a patched image is refused (the old behaviour) */
        printf("  corpus %s: header == window scan (%u words, hook %08X, window %08X-%08X), re-scan of the patched image is a no-op\n", ex[i].name,
               HDR[i].nwords, HDR[i].hook_addr, 0x80003100u, (ex[i].r1 < ex[i].r13 ? ex[i].r1 : ex[i].r13));
    }
}

/* ---- group 8: one session, menu -> game -> menu -> game -> same image again, all in one RAM image ---- */
static uint8_t *REF;
static int in_table(const struct cbvmc_scan_result *r, uint32_t a)
{
    unsigned k;
    for (k = 0; k < r->nwords; ++k) if (r->words[k].addr == a && r->words[k].orig != r->words[k].patched) return 1;
    return 0;
}
/* one stage: (re)load `e` unless reload == 0, run its stub copy through the handler, check what changed */
static void stage(struct exe *ex, unsigned i, int reload, unsigned *nst)
{
    const struct cbvmc_scan_result *h = &HDR[i];
    struct cbvmc_t2_stage *s;
    uint32_t stub, a, changed = 0, want = 0, hi = ex[i].r1 < ex[i].r13 ? ex[i].r1 : ex[i].r13;
    unsigned k;
    if (reload) exe_load(&ex[i], MEM);
    INSN_AT = ex[i].text_hi[0] - 4u;
    stub = find_stub(MEM, &ex[i]);
    CHECK(stub != 0);
    memcpy(REF, MEM, RAMSZ);
    CHECK(osinit_copy(MEM + (stub - RAMB), 0x98, F_STBU, ex[i].r1, ex[i].r13) == 8);
    ++*nst;
    s = &T.log[(*nst - 1) % CBVMC_T2_LOGN];
    CHECK(T.stages == *nst && s->index == *nst && s->mode == CBVMC_T2_OWN && s->why == CBVMC_T2_WHY_OK);
    CHECK(s->words == h->nwords && s->hook == h->hook_addr && s->lo == 0x80003100u && s->hi == (hi & ~3u));
    CHECK(CHAIN == rd(MEM, stub) && T.chain_bad == 0 && FIXP == 1 && T.armed == 1 && FK.dabr == CBVMC_T2_DABR_VALUE);
    for (k = 0; k < h->nwords; ++k) {
        CHECK(rd(MEM, h->words[k].addr) == h->words[k].patched);
        if (reload && h->words[k].orig != h->words[k].patched) want++;
    }
    for (a = RAMB; a < RAMB + RAMSZ; a += 4) {
        if (rd(MEM, a) == rd(REF, a)) continue;
        if (a >= 0x80000300u && a < 0x80000398u) continue;            /* the stub copy itself (0x304..0x397; 0x300 checked below) */
        if (a == INSN_AT) continue;                                   /* the test's own store instruction */
        if (!(reload && in_table(h, a))) fprintf(stderr, "unexpected change at %08X: %08X -> %08X\n", a, rd(REF, a), rd(MEM, a));
        CHECK(reload && in_table(h, a));
        changed++;
    }
    CHECK(changed == want && rd(MEM, 0x80000300u) == 0x48001802u);
    CHECK(memcmp(MEM + 0x304, MEM + (stub - RAMB) + 4, 0x94) == 0);
    FIXP = 0;                                                       /* the real-mode repair entry clears it */
    printf("  session stage %u: %s%s, OWN %u words (%u changed), hook %08X, window %08X-%08X\n", *nst, ex[i].name, reload ? "" : " (again, not reloaded)",
           s->words, changed, s->hook, s->lo, s->hi);
}

static void test_session(struct exe *ex, unsigned n)
{
    unsigned nst = 0, k;
    uint8_t snap0[CBVMC_T2_SNAP_BYTES];
    REF = malloc(RAMSZ);
    CHECK(REF);
    /* stage 0: the first executable as the loader and the install image leave it (table applied at OSInit, vector ours, breakpoint armed) */
    memset(MEM, 0, RAMSZ);
    exe_load(&ex[0], MEM);
    CHECK(cbvmc_words_apply(MEM, RAMB, RAMSZ, HDR[0].words, HDR[0].nwords, 0) == CBVMC_WORDS_OK);
    for (k = 0; k < CBVMC_T2_SNAP_BYTES; ++k) MEM[0xC00 + k] = (uint8_t)(k * 3 + 1);   /* stand-in for the blob */
    t2_setup();
    for (k = 0; k < CBVMC_T2_NRANGES; ++k) CHECK(memcmp(SNAP + k * CBVMC_T2_RANGE_BYTES, MEM + (cbvmc_t2_ranges[k] - RAMB), CBVMC_T2_RANGE_BYTES) == 0);
    memcpy(snap0, SNAP, sizeof(snap0));
    stage(ex, 1 % n, 1, &nst);          /* menu -> game */
    stage(ex, 0, 1, &nst);              /* -> menu (fresh, unpatched copy) */
    stage(ex, n > 2 ? 2 : 1 % n, 1, &nst);   /* -> another game */
    stage(ex, n > 2 ? 2 : 1 % n, 0, &nst);   /* the same image re-initialised from RAM: idempotent re-scan, nothing written */
    for (k = 1; k < n; ++k) { stage(ex, k, 1, &nst); stage(ex, 0, 1, &nst); }   /* every other image of the corpus, back to the menu each time */
    CHECK(T.stages_own == nst && T.stages_keep == 0 && T.unsupported == 0 && T.sync_fail == 0 && T.traps == 8u * nst);
    CHECK(FK.syncs == nst && FK.resets == nst && FK.notes == nst);
    for (k = 0; k < nst; ++k) CHECK(strncmp(FK.ev + 3 * k, "SRN", 3) == 0);   /* commit, then channel reset, then the report, once per stage */
    CHECK(memcmp(snap0, SNAP, sizeof(snap0)) == 0);                     /* the repair source never changes during the session */
    free(REF);
}

/* ---- group 9: a refused scan writes nothing but 0x304..0x307 and keeps the breakpoint ---- */
static void test_refused(struct exe *e)
{
    uint32_t a, stub, d = 0;
    uint8_t first8[8];
    unsigned nlis = 0;
    memset(MEM, 0, RAMSZ);
    exe_load(e, MEM);
    INSN_AT = e->text_hi[0] - 4u;
    stub = find_stub(MEM, e);
    memcpy(first8, MEM + (stub - RAMB), 8);
    for (a = 0x80003100u; a < e->r13; a += 4)                        /* no EXI library any more: every lis rD,0xCC00 becomes a nop */
        if ((rd(MEM, a) >> 26) == 15u && (rd(MEM, a) & 0xFFFFu) == 0xCC00u) { wr(MEM, a, 0x60000000u); nlis++; }
    CHECK(nlis > 8);
    t2_setup();
    memcpy(REF = malloc(RAMSZ), MEM, RAMSZ);
    CHECK(osinit_copy(first8, 8, F_STBU, e->r1, e->r13) == 8);
    CHECK(T.stages_keep == 1 && T.log[0].mode == CBVMC_T2_KEEP && T.log[0].why == CBVMC_R_EXI_LIB && T.log[0].words == 0);
    CHECK(T.armed == 1 && FK.dabr == CBVMC_T2_DABR_VALUE && CHAIN == 0x7C9043A6u && FK.syncs == 1 && FK.resets == 1);
    for (a = 0; a < RAMSZ; ++a) if (MEM[a] != REF[a] && (a & ~3u) != INSN_AT - RAMB) { CHECK(a >= 0x304 && a < 0x308); d++; }
    CHECK(d == 4 || (d < 4 && memcmp(MEM + 0x304, first8 + 4, 4) == 0));
    free(REF);
    printf("  refused: %s without its EXI library -> KEEP (why %u), only 0x304..0x307 written, breakpoint armed\n", e->name, T.log[0].why);
}

/* ---- group 10: keep-alive install (zero words) on the hook the scanner found in Melee ---- */
#include "cbvmc_loader.h"
static void test_keepalive(struct exe *e, const struct cbvmc_scan_result *h)
{
    struct cbvmc_loader_cfg c;
    uint8_t rimg[1024], iimg[512];
    uint32_t a, dmax = 0, changed = 0, w;
    unsigned i;
    memset(MEM, 0, RAMSZ);
    exe_load(e, MEM);
    for (i = 0; i < 18; ++i) { uint32_t sa = be(e->file + 0x48 + 4 * i), n = be(e->file + 0x90 + 4 * i); if (be(e->file + 4 * i) && n && sa + n > dmax) dmax = sa + n; }
    memset(rimg, 0x11, sizeof(rimg)); memset(iimg, 0x22, sizeof(iimg));
    memset(&c, 0, sizeof(c));
    c.ram = MEM; c.ram_base = RAMB; c.ram_size = RAMSZ; c.game_code = "GXXE"; c.dol_max = dmax; c.bi2_addr = 0x817EF840u;
    c.res_image = rimg; c.res_len = sizeof(rimg); c.ins_image = iimg; c.ins_len = sizeof(iimg);
    memcpy(REF = malloc(RAMSZ), MEM, RAMSZ);
    CHECK(cbvmc_loader_install_generic(&c, NULL, 0, h->hook_addr, h->hook_target) == CBVMC_LOADER_OK);
    for (a = 0x80003100u; a < dmax; a += 4) if (rd(MEM, a) != rd(REF, a)) { CHECK(a == h->hook_addr); changed++; }
    w = rd(MEM, h->hook_addr);
    CHECK(changed == 1 && (w & 0xFC000003u) == 0x48000001u && h->hook_addr + (uint32_t)(((int32_t)((w & 0x03FFFFFCu) << 6)) >> 6) == CBVMC_B_BASE);
    CHECK(rd(MEM, CBVMC_HOOKBLK) == CBVMC_HOOKBLK_MAGIC && rd(MEM, CBVMC_HOOKBLK + 4) == h->hook_addr && rd(MEM, CBVMC_HOOKBLK + 8) == h->hook_target &&
          rd(MEM, CBVMC_HOOKBLK + 12) == 0);
    CHECK(cbvmc_words_check(MEM, RAMB, RAMSZ, NULL, 0, 0) == CBVMC_WORDS_OK);     /* what the install image runs on a zero-row block */
    memcpy(MEM, REF, RAMSZ);
    CHECK(cbvmc_loader_install_generic(&c, NULL, 0, h->hook_addr, h->hook_target + 4) == CBVMC_LOADER_ERR_HOOK && memcmp(MEM, REF, RAMSZ) == 0);
    free(REF);
    printf("  keep-alive: zero-word install on %s's hook %08X -> %08X accepted, only the hook word changed\n", e->name, h->hook_addr, h->hook_target);
}

int main(int argc, char **argv)
{
    static struct exe ex[8];
    unsigned n = 0, i;
    MEM = malloc(RAMSZ);
    CHECK(MEM);
    test_forms();
    test_chain_reject();
    test_unsupported();
    test_outside();
    test_window_sync_line();
    test_exi_reset();
    if (argc < 2 || !exe_open(&ex[0], "GALE01", argv[1], 0)) { puts("cbvmc_t2_test: no Melee DOL, session groups skipped; unit groups passed"); return 0; }
    CHECK(exe_open(&ex[1], "GALE01+0x20000 (relocated copy)", argv[1], 0x20000u));
    n = 2;
    for (i = 2; i < (unsigned)argc && n < 8; ++i) {
        const char *b = strrchr(argv[i], '/');
        if (exe_open(&ex[n], b ? b + 1 : argv[i], argv[i], 0)) n++;
    }
    test_corpus(ex, n);
    CHECK(HDR[1].nwords == HDR[0].nwords && HDR[1].hook_addr == HDR[0].hook_addr + 0x20000u);
    test_session(ex, n);
    test_refused(&ex[0]);
    test_keepalive(&ex[0], &HDR[0]);
    for (i = 0; i < n; ++i) free(ex[i].file);
    free(MEM);
    printf("cbvmc_t2_test: all passed (%u executables)\n", n);
    return 0;
}
