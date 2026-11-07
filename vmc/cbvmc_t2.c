/* Copyright (C) 2026 Nigzel / Bjerreman. SPDX-License-Identifier: GPL-2.0-only */
/* T2 multi-executable trigger, see cbvmc_t2.h.  Original work.  Freestanding (no libc), host-testable. */
#include "cbvmc_t2.h"
#include "cbvmc_words.h"

const uint32_t cbvmc_t2_ranges[CBVMC_T2_NRANGES] = { 0x80000C00u, 0x80000D00u, 0x80000F00u, 0x80001300u, 0x80001400u, 0x80001700u };

static uint32_t off_of(const struct cbvmc_t2 *t, uint32_t ea, int *ok)
{
    uint32_t o = ea & 0x3FFFFFFFu;          /* 0x8..., 0xC... and physical aliases of MEM1 */
    *ok = o < t->env.mem_size;
    return o;
}

static void flush(struct cbvmc_t2 *t, uint32_t addr, uint32_t len)
{
    if (t->env.flush) t->env.flush(t->env.c, addr, len);
}

static void set_dabr(struct cbvmc_t2 *t, uint32_t v)
{
    if (t->env.set_dabr) t->env.set_dabr(t->env.c, v);
}

static uint32_t rd32(const uint8_t *p) { return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) | ((uint32_t)p[2] << 8) | p[3]; }

void cbvmc_t2_init(struct cbvmc_t2 *t, const struct cbvmc_t2_env *env)
{
    uint8_t *p = (uint8_t *)t;
    unsigned i;
    for (i = 0; i < sizeof(*t); ++i) p[i] = 0;
    t->env = *env;
    t->magic = CBVMC_T2_MAGIC;
}

void cbvmc_t2_arm(struct cbvmc_t2 *t, uint8_t *snap)
{
    unsigned r, i;
    int ok;
    for (r = 0; r < CBVMC_T2_NRANGES; ++r) {
        uint32_t o = off_of(t, cbvmc_t2_ranges[r], &ok);
        for (i = 0; i < CBVMC_T2_RANGE_BYTES; ++i) snap[r * CBVMC_T2_RANGE_BYTES + i] = t->env.mem[o + i];
    }
    if (t->env.fix_pending) *t->env.fix_pending = 0;
    t->armed = 1;
    set_dabr(t, CBVMC_T2_DABR_VALUE);
}

int cbvmc_t2_window(uint32_t r1, uint32_t r13, uint32_t mem_top, uint32_t *lo, uint32_t *hi)
{
    uint32_t h = 0;
    int ok1 = r1 >= CBVMC_T2_SCAN_LO + 0x1000u && r1 <= mem_top;
    int ok13 = r13 >= CBVMC_T2_SCAN_LO + 0x8000u && r13 <= mem_top;
    if (ok1) h = r1;
    if (ok13 && (h == 0 || r13 < h)) h = r13;
    if (h == 0) return -1;
    *lo = CBVMC_T2_SCAN_LO;
    *hi = h & ~3u;
    return 0;
}

void cbvmc_t2_exi_reset(struct cubeboot_vmc_exi *e)
{
    if (!e->port) return;
    if (e->cs0_selected) cubeboot_vmc_card_select(&e->port->card, 0);
    cubeboot_vmc_exi_init(e, e->port, e->translate, e->translate_context);
}

/* first word of a new exception stub: anything but a branch or sc can run from the chain word (it then continues at 0x304) */
static int chain_ok(uint32_t w)
{
    uint32_t op = w >> 26;
    return w != 0 && op != 16u && op != 17u && op != 18u && op != 19u;
}

/* ---- per-stage line, no libc ---- */
static unsigned ps(char *o, unsigned n, unsigned cap, const char *s) { while (*s && n + 1u < cap) o[n++] = *s++; return n; }
static unsigned px(char *o, unsigned n, unsigned cap, uint32_t v)
{
    unsigned i;
    for (i = 0; i < 8u && n + 1u < cap; ++i) o[n++] = "0123456789ABCDEF"[(v >> (28u - 4u * i)) & 15u];
    return n;
}
static unsigned pd(char *o, unsigned n, unsigned cap, uint32_t v)
{
    char b[10]; unsigned k = 0;
    do { b[k++] = (char)('0' + v % 10u); v /= 10u; } while (v);
    while (k && n + 1u < cap) o[n++] = b[--k];
    return n;
}

const char *cbvmc_t2_reason(uint32_t why)
{
    static const char *const scan[] = { "ok", "range", "no EXI lib", "no PI cause", "no EXIInit", "EXIInit calls", "no install point (syscall)", "no VerifyID",
                                        "VerifyID twice", "no DoMount", "DoMount twice", "mixed site", "overflow" };
    if (why < sizeof(scan) / sizeof(scan[0])) return scan[why];
    switch (why) {
    case CBVMC_T2_WHY_WINDOW: return "no r1/r13 window";
    case CBVMC_T2_WHY_APPLY: return "apply failed";
    case CBVMC_T2_WHY_TEXT: return "not game text (boot/reboot code)";
    case CBVMC_T2_WHY_PC: return "pc outside window";
    case CBVMC_T2_WHY_BIG: return "window over budget";
    default: return "?";
    }
}

unsigned cbvmc_t2_line(const struct cbvmc_t2_stage *s, char *o, unsigned cap)
{
    unsigned n = 0;
    if (!cap) return 0;
    n = ps(o, n, cap, "stage "); n = pd(o, n, cap, s->index);
    n = ps(o, n, cap, s->mode == CBVMC_T2_OWN ? " OWN" : s->mode == CBVMC_T2_ALLOW ? " ALLOW" : " KEEP");
    n = ps(o, n, cap, " pc "); n = px(o, n, cap, s->srr0);
    n = ps(o, n, cap, " r1 "); n = px(o, n, cap, s->r1);
    n = ps(o, n, cap, " r13 "); n = px(o, n, cap, s->r13);
    n = ps(o, n, cap, " window "); n = px(o, n, cap, s->lo); n = ps(o, n, cap, "-"); n = px(o, n, cap, s->hi);
    n = ps(o, n, cap, " words "); n = pd(o, n, cap, s->words);
    n = ps(o, n, cap, " hook "); n = px(o, n, cap, s->hook);
    n = ps(o, n, cap, " sync "); n = pd(o, n, cap, s->sync_rc);
    n = ps(o, n, cap, " ticks "); n = pd(o, n, cap, s->ticks);
    n = ps(o, n, cap, ": "); n = ps(o, n, cap, cbvmc_t2_reason(s->why));
    n = ps(o, n, cap, "\n");
    o[n] = 0;
    return n;
}

/* A new executable has started its OSExceptionInit: commit, reset the channel model, scan the executable that is running now, apply. */
static void new_stage(struct cbvmc_t2 *t, struct cbvmc_ctx *ctx)
{
    struct cbvmc_t2_stage *s = &t->log[t->stages % CBVMC_T2_LOGN];
    struct cbvmc_range w;
    uint32_t t0 = t->env.now ? t->env.now(t->env.c) : 0, i;
    unsigned bad = 0;
    int rc;

    t->stages++;
    s->index = t->stages; s->srr0 = ctx->srr0; s->r1 = ctx->gpr[1]; s->r13 = ctx->gpr[13]; s->lo = 0; s->hi = 0; s->words = 0; s->hook = 0; s->why = CBVMC_T2_WHY_OK;
    s->mode = CBVMC_T2_KEEP; s->sync_rc = 0; s->ticks = 0;
    if (!(ctx->srr0 >= CBVMC_T2_SCAN_LO && ctx->srr0 < CBVMC_T2_TEXT_HI)) {
        /* not game text: the boot/reboot code of the SDK on its way to the next executable.  Its bytes are routed by the caller; no commit, no reset, no scan */
        s->mode = CBVMC_T2_ALLOW; s->why = CBVMC_T2_WHY_TEXT;
        t->stages_allow++;
        t->mode = s->mode;
        goto note;
    }
    /* the card first: everything the previous executable wrote is on the SD before anything of the new one runs (power-cut rule), and no transfer is left half-way */
    rc = t->env.sync ? t->env.sync(t->env.c) : 0;
    s->sync_rc = (uint32_t)(rc < 0 ? -rc : rc);
    if (rc != 0) t->sync_fail++;
    if (t->env.card_reset) t->env.card_reset(t->env.c);

    if (cbvmc_t2_window(ctx->gpr[1], ctx->gpr[13], CBVMC_T2_TEXT_HI, &w.lo, &w.hi) != 0) {
        s->why = CBVMC_T2_WHY_WINDOW;
    } else if (ctx->srr0 < w.lo || ctx->srr0 >= w.hi) {
        s->lo = w.lo; s->hi = w.hi; s->why = CBVMC_T2_WHY_PC;
    } else if (w.hi - w.lo > CBVMC_T2_WINDOW_MAX) {
        s->lo = w.lo; s->hi = w.hi; s->why = CBVMC_T2_WHY_BIG;
    } else {
        s->lo = w.lo; s->hi = w.hi;
        cbvmc_scan_ex(t->env.mem, 0x80000000u, &w, 1, CBVMC_SCAN_IDEMPOTENT, &t->sr);
        if (t->sr.ok > 0) {
            if (cbvmc_words_apply(t->env.mem, 0x80000000u, t->env.mem_size, t->sr.words, t->sr.nwords, &bad) == CBVMC_WORDS_OK) {
                for (i = 0; i < t->sr.nwords; ++i) flush(t, t->sr.words[i].addr, 4);
                s->mode = CBVMC_T2_OWN; s->words = t->sr.nwords; s->hook = t->sr.hook_addr;
            } else s->why = CBVMC_T2_WHY_APPLY;
        } else s->why = t->sr.refuse;
    }
    if (s->mode == CBVMC_T2_OWN) t->stages_own++; else t->stages_keep++;
    t->mode = s->mode;
    s->ticks = t->env.now ? t->env.now(t->env.c) - t0 : 0;
note:
    if (t->env.stage_note) {
        char line[192];
        unsigned n = cbvmc_t2_line(s, line, sizeof(line));
        t->env.stage_note(t->env.c, line, n);
    }
}

int cbvmc_t2_trap(struct cbvmc_t2 *t, struct cbvmc_ctx *ctx)
{
    struct cbvmc_access a;
    uint32_t insn, ea, v, i, o;
    int ok, real = 0;

    t->traps++;
    /* The breakpoint goes off FIRST: this handler itself writes 0x80000304..307 (and the outside bytes) with translation on, and an armed DABR would raise a
       DSI inside the DSI handler, which overwrites the saved context and the handler stack (console result 1: the second "stage" at pc 817F8A14 was this
       very store).  It is set again on the way out. */
    set_dabr(t, 0);
    o = off_of(t, ctx->srr0, &ok);
    if (!ok || (ctx->srr0 & 3u) || o + 4u > t->env.mem_size || !cbvmc_decode(insn = rd32(t->env.mem + o), &a) || !a.store) {
        /* not a store we can take apart: let it happen (the breakpoint goes off for good, the executable runs as it would without us) */
        t->unsupported++;
        t->armed = 0;
        set_dabr(t, 0);
        return 1;
    }
    ea = (a.ra ? ctx->gpr[a.ra] : 0u) + (a.indexed ? ctx->gpr[a.rb] : (uint32_t)a.disp);
    v = ctx->gpr[a.rt];
    if (a.byterev) v = a.size == 4 ? ((v << 24) | ((v & 0xFF00u) << 8) | ((v >> 8) & 0xFF00u) | (v >> 24)) : (((v & 0xFFu) << 8) | ((v >> 8) & 0xFFu));

    if (!t->active || (ctx->srr0 > t->stage_srr0 ? ctx->srr0 - t->stage_srr0 : t->stage_srr0 - ctx->srr0) > CBVMC_T2_STAGE_SPAN) {
        t->active = 1; t->mask = 0; t->chain_done = 0; t->stage_srr0 = ctx->srr0; t->shadow = *t->env.chain;
        new_stage(t, ctx);
    }
    for (i = 0; i < a.size; ++i) {
        uint8_t b = (uint8_t)(v >> (8u * (a.size - 1u - i)));
        uint32_t bo = off_of(t, ea + i, &ok);
        if (!ok) continue;
        if (bo >= 0x300u && bo < 0x308u) {
            unsigned k = bo - 0x300u;
            if (k < 4u) t->shadow = (t->shadow & ~(0xFFu << (24u - 8u * k))) | ((uint32_t)b << (24u - 8u * k));
            else { t->env.mem[bo] = b; real = 1; }
            t->mask |= 1u << k;
        } else {
            t->env.mem[bo] = b;                 /* an unaligned store reaching across the doubleword: the outside bytes are ordinary memory */
            flush(t, 0x80000000u | bo, 1);
            t->outside++;
        }
    }
    if (real) flush(t, CBVMC_T2_VEC, 8);
    if (!t->chain_done && (t->mask & 0x0Fu) == 0x0Fu) {
        t->chain_done = 1;
        if (chain_ok(t->shadow)) {
            *t->env.chain = t->shadow;
            flush(t, (uint32_t)(uintptr_t)t->env.chain, 4);
        } else t->chain_bad++;                  /* zeros or a branch (a loader clearing low memory, not an OS init): keep the old chain word */
    }
    if (t->mask == 0xFFu) {
        t->active = 0;                          /* the next trap is the next executable */
        if (t->env.fix_pending) *t->env.fix_pending = 1;      /* the rest of this OSExceptionInit rewrites the vectors inside the blob: repair at the next entry */
    }
    if (a.ra_update) ctx->gpr[a.ra] = ea;
    ctx->srr0 += 4u;
    (void)insn;
    if (t->armed) set_dabr(t, CBVMC_T2_DABR_VALUE);
    return 0;
}
