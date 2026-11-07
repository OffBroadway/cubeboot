/* Copyright (C) 2026 Nigzel / Bjerreman. SPDX-License-Identifier: GPL-2.0-only */
/* Host test of the T2 console-result-1 sequence (docs/archive/CUBEBOOT_VMC_T2_CONSOLE_PLAN.md): menu refused, two reboot-stub copies over 0x80000300 from
 * pc 0x81300330 and 0x817F8A14 (ALLOW), then the game's own OSExceptionInit copy from game text (OWN), plus the KEEP cases of the window rules.
 * The breakpoint is modelled with mprotect: while the fake DABR is armed the RAM page holding 0x80000300 is read-only, so a store by the handler with
 * the breakpoint armed (the nested DSI of the console) faults here.
 *   cbvmc_t2_reboot_test MELEE_DOL */
#include <signal.h>
#include <unistd.h>
#include <sys/mman.h>

#include "cbvmc_t2_util.h"
#include "cbvmc_t2.h"
#include "cbvmc_scan.h"
#include "cbvmc_words.h"

#define DSISR_ST 0x02400000u       /* console value for a store that matched DABR */
#define PC_BOOT  0x81300330u       /* console result 1, stage 1 */
#define PC_TOP   0x817F8A14u       /* console result 1, stage 2 */
#define R13_RB   0x8131B900u       /* r13 of both console reboot traps */
#define R1_RB    0x8131B8E0u
#define OLDCHAIN 0x7C9043A6u
#define OURVEC   0x48001802u

static uint8_t *MEM;               /* page aligned (mmap) */
static size_t PG;
static uint32_t CHAIN, FIXP;
static uint8_t SNAP[CBVMC_T2_SNAP_BYTES];
static struct cbvmc_t2 T;

/* ---- fake CPU: DABR as page protection ---- */
struct fk {
    uint32_t dabr, sets, syncs, resets, notes, nows;
    int in_trap, first_set_seen, first_set_clear;
    char note[256];
};
static struct fk FK;

static void prot(int ro) { CHECK(mprotect(MEM, PG, ro ? PROT_READ : PROT_READ | PROT_WRITE) == 0); }
static int armed(void) { return (FK.dabr & 2u) != 0; }
/* a store by the test itself standing for the game's CPU (legal on hardware when it does not hit 0x300..0x307) */
static void gstore8(uint32_t a, uint8_t b) { int ro = armed() && (a - RAMB) < PG; if (ro) prot(0); MEM[a - RAMB] = b; if (ro) prot(1); }
static void gstore32(uint32_t a, uint32_t w) { unsigned i; for (i = 0; i < 4; ++i) gstore8(a + i, (uint8_t)(w >> (24 - 8 * i))); }

static void on_fault(int sig)
{
    static const char m[] = "cbvmc_t2_reboot_test: store to the 0x80000300 page while the DABR was armed (nested DSI on the console)\n";
    (void)sig;
    if (write(2, m, sizeof(m) - 1)) {}
    _exit(2);
}

static void f_flush(void *c, uint32_t a, uint32_t n) { (void)c; (void)a; (void)n; }
static void f_dabr(void *c, uint32_t v)
{
    (void)c;
    if (FK.in_trap && !FK.first_set_seen) { FK.first_set_seen = 1; FK.first_set_clear = (v == 0); }
    FK.dabr = v; FK.sets++;
    prot((v & 2u) != 0);
}
static int f_sync(void *c) { (void)c; FK.syncs++; return 0; }
static void f_reset(void *c) { (void)c; FK.resets++; }
static void f_note(void *c, const char *s, unsigned n) { (void)c; FK.notes++; CHECK(n < sizeof(FK.note)); memcpy(FK.note, s, n); FK.note[n] = 0; }
static uint32_t f_now(void *c) { (void)c; return ++FK.nows * 5u; }

/* fresh trigger over MEM, armed, our vector word at 0x300, the old stub's first word in the chain */
static void t2_setup(void)
{
    struct cbvmc_t2_env env;
    prot(0);
    memset(&FK, 0, sizeof(FK));
    memset(&env, 0, sizeof(env));
    env.mem = MEM; env.mem_size = RAMSZ; env.chain = &CHAIN; env.fix_pending = &FIXP;
    env.flush = f_flush; env.set_dabr = f_dabr; env.sync = f_sync; env.card_reset = f_reset; env.stage_note = f_note; env.now = f_now;
    CHAIN = OLDCHAIN; FIXP = 7;
    wr(MEM, 0x80000300u, OURVEC);
    wr(MEM, 0x80000304u, 0x808000C0u);
    cbvmc_t2_init(&T, &env);
    cbvmc_t2_arm(&T, SNAP);
    CHECK(T.armed == 1 && FK.dabr == CBVMC_T2_DABR_VALUE && FIXP == 0 && armed());
}

/* One byte-wise copy of src[0..n) to 0x80000300 by `stbu r0,1(r6)` at `pc`, as the SDK memcpy does.  Bytes inside the doubleword go through the handler,
   each from a fresh synthetic context; the rest are plain stores.  Every trap: swallowed (srr0 + 4), r6 updated, nothing else in the context changed,
   DABR cleared before any other handler action and armed again on return.  Returns the trap count. */
static unsigned copy_seq(const uint8_t *src, unsigned n, uint32_t pc, uint32_t r1, uint32_t r13)
{
    unsigned i, k, traps = 0;
    uint32_t r6 = 0x800002FFu;
    gstore32(pc, enc_d(OP_STBU, 0, 6, 1));
    for (i = 0; i < n; ++i) {
        uint32_t dst = 0x80000300u + i;
        struct cbvmc_ctx x, x0;
        if (dst >= 0x80000308u) { gstore8(dst, src[i]); r6 = dst; continue; }
        for (k = 0; k < 32; ++k) x.gpr[k] = 0xA5000000u | (k << 8) | i;
        x.gpr[0] = src[i]; x.gpr[1] = r1; x.gpr[13] = r13; x.gpr[6] = r6;
        x.srr0 = pc; x.srr1 = 0x00003032u; x.dar = dst; x.dsisr = DSISR_ST; x.lr = 0x81300100u; x.ctr = 0x98u - i; x.cr = 0x24000088u; x.xer = 0x20000000u;
        CHECK(CBVMC_T2_MATCH(x.dar, x.dsisr));
        x0 = x;
        FK.in_trap = 1; FK.first_set_seen = 0;
        CHECK(cbvmc_t2_trap(&T, &x) == 0);
        FK.in_trap = 0;
        CHECK(FK.first_set_seen && FK.first_set_clear);               /* the breakpoint went off first */
        CHECK(armed() && FK.dabr == CBVMC_T2_DABR_VALUE && T.armed == 1);
        x0.srr0 = pc + 4; x0.gpr[6] = dst;
        CHECK(memcmp(&x, &x0, sizeof(x)) == 0);                        /* the saved context: only srr0 and the update base changed */
        r6 = dst;
        traps++;
    }
    return traps;
}

/* The SDK exception stub as the image holds it (__OSEVStart, 0x98 bytes). */
static void stub_of(const struct exe *e, uint8_t out[0x98])
{
    uint32_t s = find_stub(MEM, e);
    CHECK(s != 0);
    memcpy(out, MEM + (s - RAMB), 0x98);
}

/* MEM equals REF with src copied over 0x300..0x300+n, except 0x300..0x303 (our vector word) and the words in `skip` */
static void same_as_plain(const uint8_t *ref, const uint8_t *src, unsigned n, uint32_t skip)
{
    uint32_t o;
    for (o = 0; o < RAMSZ; ++o) {
        uint8_t want = ref[o];
        if (o >= 0x300u && o < 0x300u + n) want = src[o - 0x300u];
        if (o >= 0x300u && o < 0x304u) want = (uint8_t)(OURVEC >> (24 - 8 * (o - 0x300u)));
        if (skip && (o & ~3u) == skip - RAMB) continue;
        if (MEM[o] != want) { fprintf(stderr, "differs at %08X: %02X, plain store gives %02X\n", RAMB + o, MEM[o], want); CHECK(0); }
    }
}

static uint8_t *REF;
static const struct cbvmc_t2_stage *last(void) { CHECK(T.stages > 0); return &T.log[(T.stages - 1u) % CBVMC_T2_LOGN]; }
static void hex8(char *o, uint32_t v) { snprintf(o, 9, "%08X", v); }

/* Load `e` into MEM; `menu` removes its EXI library (every lis rD,0xCC00 becomes a nop) so a scan refuses it, like the Collector's Edition menu DOL. */
static void load(const struct exe *e, int menu)
{
    uint32_t a;
    unsigned nlis = 0;
    prot(0);
    memset(MEM, 0, RAMSZ);
    exe_load(e, MEM);
    if (!menu) return;
    for (a = 0x80003100u; a < e->r13; a += 4)
        if ((rd(MEM, a) >> 26) == 15u && (rd(MEM, a) & 0xFFFFu) == 0xCC00u) { wr(MEM, a, 0x60000000u); nlis++; }
    CHECK(nlis > 8);
}

/* ---- (a) reboot-stub copies: ALLOW, bytes routed like a game's, no commit, reset or scan, breakpoint armed ---- */
static void reboot_seq(const uint8_t *stub, uint32_t pc, uint32_t r1, uint32_t r13)
{
    const struct cbvmc_t2_stage *s;
    uint32_t st = T.stages, own = T.stages_own, keep = T.stages_keep, al = T.stages_allow, sy = FK.syncs, rs = FK.resets, nt = FK.notes;
    char h[9];
    memcpy(REF, MEM, RAMSZ);
    CHECK(copy_seq(stub, 0x98, pc, r1, r13) == 8);
    s = last();
    CHECK(T.stages == st + 1 && T.stages_allow == al + 1 && T.stages_own == own && T.stages_keep == keep);
    CHECK(s->mode == CBVMC_T2_ALLOW && s->why == CBVMC_T2_WHY_TEXT && s->srr0 == pc && s->r1 == r1 && s->r13 == r13);
    CHECK(s->lo == 0 && s->hi == 0 && s->words == 0 && s->hook == 0 && s->ticks == 0);      /* nothing scanned */
    CHECK(FK.syncs == sy && FK.resets == rs && FK.notes == nt + 1);                         /* no commit, no EXI reset, one line */
    CHECK(CHAIN == be(stub) && T.chain_bad == 0 && FIXP == 1 && T.active == 0);
    same_as_plain(REF, stub, 0x98, pc);                                                     /* no patch anywhere, 0x304.. as a plain copy */
    CHECK(strstr(FK.note, " ALLOW pc ") != NULL);
    hex8(h, pc); CHECK(strstr(FK.note, h) != NULL);
    hex8(h, r1); CHECK(strstr(FK.note, h) != NULL);
    hex8(h, r13); CHECK(strstr(FK.note, h) != NULL);
    CHECK(strstr(FK.note, cbvmc_t2_reason(CBVMC_T2_WHY_TEXT)) != NULL);
    FIXP = 0;                                                       /* the real-mode repair entry clears it */
}

static void test_reboot(const struct exe *e)
{
    uint8_t stub[0x98];
    load(e, 1);
    stub_of(e, stub);
    t2_setup();
    reboot_seq(stub, PC_BOOT, R1_RB, R13_RB);
    reboot_seq(stub, PC_TOP, R1_RB, R13_RB);
    CHECK(T.stages == 2 && T.stages_allow == 2 && T.unsupported == 0 && T.outside == 0);
    printf("  reboot: menu %s refused; stub copies from %08X and %08X (r1 %08X r13 %08X) ALLOW, memory == plain copy, breakpoint armed\n", e->name,
           PC_BOOT, PC_TOP, R1_RB, R13_RB);
    /* the same copies over an unmodified image with that image's own registers: a scan would accept it, ALLOW must still patch nothing */
    load(e, 0);
    t2_setup();
    reboot_seq(stub, PC_BOOT, e->r1, e->r13);
    reboot_seq(stub, PC_TOP, e->r1, e->r13);
    printf("  reboot: same copies over a scannable %s with its r1/r13: ALLOW, no word patched\n", e->name);
}

/* scan of the image's DOL text sections (the reference table) */
static struct cbvmc_scan_result HDR;
static void scan_hdr(const struct exe *e, struct cbvmc_scan_result *r)
{
    struct cbvmc_range rg[7];
    unsigned i;
    for (i = 0; i < e->ntext; ++i) { rg[i].lo = e->text_lo[i]; rg[i].hi = e->text_hi[i]; }
    cbvmc_scan_ex(MEM, RAMB, rg, e->ntext, 0, r);
}
static int in_table(uint32_t a)
{
    unsigned k;
    for (k = 0; k < HDR.nwords; ++k) if ((HDR.words[k].addr & ~3u) == a) return 1;
    return 0;
}

/* ---- (b) after the reboot copies, the game's own OSExceptionInit copy from its text: OWN, table applied, window <= 8 MiB and containing pc ---- */
static void test_game(const struct exe *e)
{
    uint8_t stub[0x98];
    const struct cbvmc_t2_stage *s;
    uint32_t pc = e->text_hi[0] - 4u, hi = (e->r1 < e->r13 ? e->r1 : e->r13) & ~3u, o, a;
    unsigned k, nw;
    load(e, 1);
    stub_of(e, stub);
    t2_setup();
    reboot_seq(stub, PC_BOOT, R1_RB, R13_RB);
    reboot_seq(stub, PC_TOP, R1_RB, R13_RB);
    /* the reboot program loads the game over the menu */
    prot(0); exe_load(e, MEM); prot(armed());
    gstore32(pc, enc_d(OP_STBU, 0, 6, 1));
    prot(0); scan_hdr(e, &HDR); prot(armed());
    CHECK(HDR.ok == 1 && HDR.nwords > 0 && !in_table(pc));
    nw = HDR.nwords;
    memcpy(REF, MEM, RAMSZ);
    CHECK(copy_seq(stub, 0x98, pc, e->r1, e->r13) == 8);
    s = last();
    CHECK(T.stages == 3 && T.stages_own == 1 && T.stages_allow == 2 && T.stages_keep == 0);
    CHECK(s->mode == CBVMC_T2_OWN && s->why == CBVMC_T2_WHY_OK && s->srr0 == pc && s->words == nw && s->hook == HDR.hook_addr);
    CHECK(s->lo == CBVMC_T2_SCAN_LO && s->hi == hi && s->hi - s->lo <= 0x00800000u && pc >= s->lo && pc < s->hi);
    CHECK(FK.syncs == 1 && FK.resets == 1 && CHAIN == be(stub) && FIXP == 1);
    for (k = 0; k < nw; ++k) CHECK(rd(MEM, HDR.words[k].addr) == HDR.words[k].patched);
    for (o = 0; o < RAMSZ; o += 4) {                    /* apart from the table and the stub copy, memory is unchanged */
        a = RAMB + o;
        if (o >= 0x300u && o < 0x398u) continue;
        if (rd(MEM, a) != rd(REF, a)) CHECK(in_table(a));
    }
    CHECK(rd(MEM, 0x80000300u) == OURVEC && memcmp(MEM + 0x304, stub + 4, 0x94) == 0);
    CHECK(strstr(FK.note, " OWN pc ") != NULL && strstr(FK.note, cbvmc_t2_reason(CBVMC_T2_WHY_OK)) != NULL);
    printf("  game: %s OSExceptionInit copy from %08X after two ALLOW copies: OWN %u words, window %08X-%08X\n", e->name, pc, nw, s->lo, s->hi);
}

/* ---- (c) window rules on a scannable image: pc outside [lo, hi) or a window over 8 MiB is KEEP and patches nothing ---- */
static const struct cbvmc_t2_stage *one_copy(const struct exe *e, const uint8_t *stub, uint32_t pc, uint32_t r1, uint32_t r13)
{
    load(e, 0);
    t2_setup();
    memcpy(REF, MEM, RAMSZ);
    CHECK(copy_seq(stub, 0x98, pc, r1, r13) == 8);
    CHECK(T.stages == 1 && T.armed == 1 && CHAIN == be(stub) && FIXP == 1);
    return last();
}
static void keep_case(const struct exe *e, const uint8_t *stub, uint32_t pc, uint32_t r1, uint32_t r13, uint32_t why)
{
    const struct cbvmc_t2_stage *s = one_copy(e, stub, pc, r1, r13);
    CHECK(s->mode == CBVMC_T2_KEEP && s->why == why && s->words == 0 && T.stages_keep == 1 && T.stages_own == 0 && T.stages_allow == 0);
    same_as_plain(REF, stub, 0x98, pc);
    CHECK(strstr(FK.note, " KEEP pc ") != NULL && strstr(FK.note, cbvmc_t2_reason(why)) != NULL);
    printf("  keep: pc %08X r1 %08X r13 %08X -> KEEP (%s), nothing patched\n", pc, r1, r13, cbvmc_t2_reason(why));
}

static void test_keep(const struct exe *e)
{
    uint8_t stub[0x98];
    uint32_t hi = (e->r1 < e->r13 ? e->r1 : e->r13) & ~3u, top = CBVMC_T2_SCAN_LO + 0x00800000u;
    const struct cbvmc_t2_stage *s;
    load(e, 0);
    stub_of(e, stub);
    keep_case(e, stub, 0x80F00000u, e->r1, e->r13, CBVMC_T2_WHY_PC);           /* game-text range, above its own window */
    keep_case(e, stub, hi, e->r1, e->r13, CBVMC_T2_WHY_PC);                    /* the window is half-open */
    keep_case(e, stub, 0x80900000u, 0x80A00000u, 0x80A00000u, CBVMC_T2_WHY_BIG);
    keep_case(e, stub, top - 4u, top + 4u, top + 4u, CBVMC_T2_WHY_BIG);      /* 8 MiB + 4 */
    s = one_copy(e, stub, top - 4u, top, top);                                 /* exactly 8 MiB: scanned */
    CHECK(s->mode == CBVMC_T2_OWN && s->hi - s->lo == 0x00800000u && s->words > 0);
    printf("  keep: an 8 MiB window containing pc is still scanned (OWN %u words)\n", s->words);
}

/* ---- (d) the stages-file line: one per copy sequence, format of cbvmc_t2.h ---- */
static void line_is(uint32_t mode, uint32_t why, const char *mode_text)
{
    struct cbvmc_t2_stage s;
    char got[256], want[256];
    unsigned n;
    memset(&s, 0, sizeof(s));
    s.index = 3; s.mode = mode; s.why = why; s.srr0 = PC_TOP; s.r1 = R1_RB; s.r13 = R13_RB;
    s.lo = 0x80003100u; s.hi = 0x804DB6A0u; s.words = 25; s.hook = 0x80342E24u; s.sync_rc = 0; s.ticks = 1234;
    n = cbvmc_t2_line(&s, got, sizeof(got));
    snprintf(want, sizeof(want), "stage 3 %s pc 817F8A14 r1 8131B8E0 r13 8131B900 window 80003100-804DB6A0 words 25 hook 80342E24 sync 0 ticks 1234: %s\n",
             mode_text, cbvmc_t2_reason(why));
    if (strcmp(got, want) != 0) { fprintf(stderr, "line:\n  got  %s  want %s", got, want); CHECK(0); }
    CHECK(n == strlen(want));
    CHECK(n < 192u);                                    /* fits the handler's line buffer */
    CHECK(strcmp(cbvmc_t2_reason(why), "?") != 0 && strlen(cbvmc_t2_reason(why)) >= 2u);   /* a readable reason, not a code */
}

static void test_line(void)
{
    char small[20];
    struct cbvmc_t2_stage s;
    unsigned w;
    line_is(CBVMC_T2_ALLOW, CBVMC_T2_WHY_TEXT, "ALLOW");
    line_is(CBVMC_T2_OWN, CBVMC_T2_WHY_OK, "OWN");
    line_is(CBVMC_T2_KEEP, CBVMC_T2_WHY_PC, "KEEP");
    line_is(CBVMC_T2_KEEP, CBVMC_T2_WHY_BIG, "KEEP");
    line_is(CBVMC_T2_KEEP, CBVMC_T2_WHY_WINDOW, "KEEP");
    for (w = 1; w <= 12; ++w) line_is(CBVMC_T2_KEEP, w, "KEEP");           /* every scanner refusal has a text */
    memset(&s, 0, sizeof(s));
    CHECK(cbvmc_t2_line(&s, small, sizeof(small)) == sizeof(small) - 1u && small[sizeof(small) - 1u] == 0);   /* truncated, terminated */
    printf("  line: ALLOW/OWN/KEEP lines carry pc, r1, r13, window and a readable reason for every code\n");
}

int main(int argc, char **argv)
{
    static struct exe ex;
    struct sigaction sa;
    (void)enc_x;                                        /* shared helper header; only the D form is used here */
    PG = (size_t)getpagesize();
    MEM = mmap(NULL, RAMSZ, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANON, -1, 0);
    CHECK(MEM != MAP_FAILED && PG >= 0x400u);
    memset(&sa, 0, sizeof(sa)); sa.sa_handler = on_fault;
    sigaction(SIGSEGV, &sa, NULL); sigaction(SIGBUS, &sa, NULL);
    test_line();
    if (argc < 2 || !exe_open(&ex, "GALE01", argv[1], 0)) { puts("cbvmc_t2_reboot_test: no Melee DOL, sequence groups skipped; line group passed"); return 0; }
    REF = malloc(RAMSZ);
    CHECK(REF);
    test_reboot(&ex);
    test_game(&ex);
    test_keep(&ex);
    prot(0);
    free(REF); free(ex.file);
    munmap(MEM, RAMSZ);
    puts("cbvmc_t2_reboot_test: all passed");
    return 0;
}
