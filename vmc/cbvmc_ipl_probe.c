/* Copyright (C) 2026 Nigzel / Bjerreman. SPDX-License-Identifier: GPL-2.0-only */
/* G3 IPL probe, portable part, see cbvmc_ipl_probe.h.  Original work.  No libc.  Read-only: it only reads through env->mem and writes its own buffers. */
#if defined(CBVMC_IPL_PROBE) || defined(CBVMC_IPL_PROBE_HOST)

#include "cbvmc_ipl_probe.h"

/* Reuse of the scanner: this translation unit includes it under other names, so the game path's object file (cbvmc_scan.o) is not touched at all and the
   probe can use the scanner's static helpers (follow, verifyid_at, domount_at, match_all, func_start) on the BS2 image. */
#define cbvmc_scan ipl_probe_scan
#define cbvmc_scan_ex ipl_probe_scan_ex
#define cbvmc_scan_report ipl_probe_scan_report
#include "cbvmc_scan.c"

#define MEM_LO 0x80000000u
#define MEM_HI 0x81800000u
#define BUFSZ 8192u
#define MAXLIST 48u

/* ---- text builder: always terminated, never overruns ---- */
struct tb { char *p; unsigned n, cap; };
static void t_c(struct tb *t, char c) { if (t->n + 1u < t->cap) t->p[t->n++] = c; t->p[t->n] = 0; }
static void t_s(struct tb *t, const char *s) { while (*s) t_c(t, *s++); }
static void t_x(struct tb *t, uint32_t v) { unsigned i; for (i = 0; i < 8u; ++i) t_c(t, "0123456789ABCDEF"[(v >> (28u - 4u * i)) & 15u]); }
static void t_d(struct tb *t, unsigned v) { char b[12]; unsigned k = 0; do { b[k++] = (char)('0' + v % 10u); v /= 10u; } while (v); while (k) t_c(t, b[--k]); }
static void t_sp(struct tb *t) { t_c(t, ' '); }
static void t_nl(struct tb *t) { t_c(t, '\n'); }
static void t_kx(struct tb *t, const char *k, uint32_t v) { t_s(t, k); t_sp(t); t_x(t, v); }       /* "key XXXXXXXX" */

static int seq(const char *a, const char *b) { while (*a && *a == *b) { ++a; ++b; } return *a == *b; }

/* ---- bounds-checked memory ---- */
static uint32_t mrd(const struct cbvmc_ipl_env *e, uint32_t a)
{
    const uint8_t *p;
    if (a < MEM_LO || a > MEM_HI - 4u) return 0;
    p = e->mem + ((a & ~3u) - MEM_LO);
    return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) | ((uint32_t)p[2] << 8) | p[3];
}
static int mok(uint32_t a) { return a >= MEM_LO && a <= MEM_HI - 4u && !(a & 3u); }

static unsigned ms_since(const struct cbvmc_ipl_env *e, uint64_t t0)
{
    if (!e->now || !e->ticks_per_ms) return 0;
    return (unsigned)((e->now() - t0) / e->ticks_per_ms);
}

static char g_buf[BUFSZ];
static struct cbvmc_scan_result g_sr, g_sr2;

/* ---- stage 2: CPU state and low memory ---- */
static void stage2(const struct cbvmc_ipl_env *e, struct tb *t)
{
    static const uint16_t vec[] = { 0x0100, 0x0200, 0x0300, 0x0400, 0x0500, 0x0600, 0x0700, 0x0800, 0x0900, 0x0C00, 0x0D00 };
    static const uint32_t low[] = { 0x00, 0x04, 0x08, 0x20, 0x28, 0x2C, 0x30, 0x34, 0x38, 0xCC, 0xD0, 0xD4, 0xDC, 0xE0, 0xE4, 0xE8, 0xEC, 0xF0, 0xF8, 0xFC };
    unsigned i, j;
    t_s(t, "[cpu]\nipl_rev_id "); t_d(t, e->ipl_rev); t_nl(t);
    t_kx(t, "r2", e->r2); t_sp(t); t_kx(t, "r13", e->r13); t_sp(t); t_kx(t, "msr", e->msr); t_nl(t);
    t_kx(t, "pvr", e->pvr); t_sp(t); t_kx(t, "hid0", e->hid0); t_sp(t); t_kx(t, "sdr1", e->sdr1); t_nl(t);
    t_s(t, "msr bits: EE "); t_d(t, (e->msr >> 15) & 1u); t_s(t, " IR "); t_d(t, (e->msr >> 5) & 1u); t_s(t, " DR "); t_d(t, (e->msr >> 4) & 1u);
    t_s(t, " PR "); t_d(t, (e->msr >> 14) & 1u); t_s(t, " IP "); t_d(t, (e->msr >> 6) & 1u); t_nl(t);
    for (i = 0; i < 8u; ++i) {
        t_s(t, i < 4u ? "ibat" : "dbat"); t_d(t, i & 3u); t_s(t, " u "); t_x(t, e->bat[2u * i]); t_s(t, " l "); t_x(t, e->bat[2u * i + 1u]); t_nl(t);
    }
    t_s(t, "[lowmem]\n");
    for (i = 0; i < sizeof(low) / sizeof(low[0]); ++i) { t_s(t, "80000"); t_c(t, "0123456789ABCDEF"[(low[i] >> 8) & 15u]); t_c(t, "0123456789ABCDEF"[(low[i] >> 4) & 15u]); t_c(t, "0123456789ABCDEF"[low[i] & 15u]); t_sp(t); t_x(t, mrd(e, MEM_LO + low[i])); t_nl(t); }
    t_s(t, "[vectors] first words (the exception stubs, 8 for DSI and syscall, else 4)\n");
    for (i = 0; i < sizeof(vec) / sizeof(vec[0]); ++i) {
        unsigned n = (vec[i] == 0x0300 || vec[i] == 0x0C00) ? 8u : 4u;
        t_s(t, "vec "); t_x(t, MEM_LO + vec[i]);
        for (j = 0; j < n; ++j) { t_sp(t); t_x(t, mrd(e, MEM_LO + vec[i] + 4u * j)); }
        t_nl(t);
    }
}

/* ---- stage 3: memory at the top of MEM1, the blob window in low memory ---- */
static void stage3(const struct cbvmc_ipl_env *e, struct tb *t)
{
    uint32_t a, run_lo = 0;
    unsigned nz = 0, shown = 0, runs = 0, b;
    unsigned bucket[32];
    t_s(t, "[memory]\n");
    t_kx(t, "patches_end", e->patches_end); t_sp(t); t_kx(t, "lowmem_end", e->lowmem_end); t_nl(t);
    {
        uint32_t first[6];
        for (a = 0x80000C00u; a < 0x80003000u; a += 4u) if (mrd(e, a)) { if (nz < 6u) first[nz] = a; ++nz; }
        t_s(t, "blob window 80000C00-80003000 (the runtime blob goes here): nonzero words "); t_d(t, nz);
        if (nz) { t_s(t, ", first at"); for (shown = 0; shown < nz && shown < 6u; ++shown) { t_sp(t); t_x(t, first[shown]); } }
        t_nl(t);
    }
    for (b = 0; b < 32u; ++b) bucket[b] = 0;
    for (a = 0x81600000u; a < MEM_HI; a += 4u) if (mrd(e, a)) ++bucket[(a - 0x81600000u) >> 16];
    t_s(t, "nonzero words per 64 KB from 81600000 (of 16384):");
    for (b = 0; b < 32u; ++b) { if (!(b & 7u)) { t_nl(t); t_x(t, 0x81600000u + (b << 16)); t_c(t, ':'); } t_sp(t); t_d(t, bucket[b]); }
    t_nl(t);
    t_s(t, "all-zero 4 KB-page runs in 81600000-81800000 (zero is not proof of free):\n");
    for (a = 0x81600000u; a <= MEM_HI; a += 0x1000u) {
        int zero = 0;
        if (a < MEM_HI) { uint32_t q; zero = 1; for (q = 0; q < 0x1000u; q += 4u) if (mrd(e, a + q)) { zero = 0; break; } }
        if (zero && !run_lo) run_lo = a;
        if (!zero && run_lo) {
            if (runs < 40u) { t_x(t, run_lo); t_c(t, '-'); t_x(t, a); t_sp(t); t_d(t, (a - run_lo) >> 10); t_s(t, " KB\n"); }
            ++runs; run_lo = 0;
        }
    }
    t_s(t, "zero runs "); t_d(t, runs); t_nl(t);
}

/* ---- stage 4: DI, EXI, PI and the OS thread list ---- */
static void stage4(const struct cbvmc_ipl_env *e, struct tb *t)
{
    static const char *const dn[10] = { "SR", "CVR", "CMDBUF0", "CMDBUF1", "CMDBUF2", "MAR", "LENGTH", "CR", "IMMBUF", "CFG" };
    unsigned i, ch, nthr = 0;
    uint32_t th;
    t_s(t, "[di]");
    for (i = 0; i < 10u; ++i) { t_sp(t); t_s(t, dn[i]); t_c(t, '='); t_x(t, e->di[i]); }
    t_nl(t);
    t_s(t, "[exi] CSR MAR LENGTH CR DATA\n");
    for (ch = 0; ch < 3u; ++ch) {
        t_s(t, "ch"); t_d(t, ch);
        for (i = 0; i < 5u; ++i) { t_sp(t); t_x(t, e->exi[ch][i]); }
        t_s(t, "  ext_in "); t_d(t, (e->exi[ch][0] >> 12) & 1u); t_s(t, " tcint_mask "); t_d(t, (e->exi[ch][0] >> 3) & 1u); t_nl(t);
    }
    t_kx(t, "[pi] cause", e->pi_cause); t_sp(t); t_kx(t, "mask", e->pi_mask); t_nl(t);
    t_kx(t, "[cubeboot] vmc_enabled", e->vmc_enabled); t_s(t, " (menu file thread not started yet)\n");
    t_s(t, "[threads] OSActiveThreadQueue 800000DC, current 800000E4: "); t_x(t, mrd(e, 0x800000E4u)); t_nl(t);
    t_s(t, "addr state attr suspend prio stackBase stackEnd srr0 lr\n");
    th = mrd(e, 0x800000DCu);
    while (mok(th) && th + 0x310u <= MEM_HI && nthr < 16u) {
        t_x(t, th); t_sp(t); t_d(t, (mrd(e, th + 0x2C8u) >> 16) & 0xFFFFu); t_sp(t); t_d(t, mrd(e, th + 0x2C8u) & 0xFFFFu);
        t_sp(t); t_d(t, mrd(e, th + 0x2CCu)); t_sp(t); t_d(t, mrd(e, th + 0x2D0u)); t_sp(t); t_x(t, mrd(e, th + 0x304u)); t_sp(t); t_x(t, mrd(e, th + 0x308u));
        t_sp(t); t_x(t, mrd(e, th + 0x198u)); t_sp(t); t_x(t, mrd(e, th + 0x84u)); t_nl(t);
        ++nthr;
        th = mrd(e, th + 0x2FCu);             /* linkActive.next */
    }
    t_s(t, "threads "); t_d(t, nthr); t_nl(t);
}

/* ---- stage 5: the BS2 image: hashes, known symbols, the scanner, the sites ---- */
#define NEXI 8u
static const char *const exi_names[NEXI] = { "EXIImm", "EXIImmEx", "EXISync", "EXISelect", "EXIDeselect", "EXILock", "EXIUnlock", "CARDProbe" };

static uint32_t sym_find(const struct cbvmc_ipl_env *e, const char *set, const char *name)
{
    unsigned i;
    for (i = 0; i < e->nsyms; ++i) if (seq(e->syms[i].set, set) && seq(e->syms[i].name, name)) return e->syms[i].addr;
    return 0;
}

static int prologue_ok(uint32_t w) { return w == 0x7C0802A6u || (w >> 16) == 0x9421u; }

static void scan_text(struct tb *t, const char *tag, const struct cbvmc_scan_result *r)
{
    static const char *const fn[CBVMC_F_COUNT] = { "exi_lib", "pi_cause", "exiinit", "verifyid", "domount" };
    static const char *const st[3] = { "not-found", "found", "ambiguous" };
    unsigned i;
    t_s(t, tag); t_s(t, " ok "); t_d(t, (unsigned)(r->ok > 0)); t_s(t, " refuse_code "); t_d(t, r->refuse);
    t_s(t, " lis_cc00 "); t_d(t, r->lis_sites); t_s(t, " words "); t_d(t, r->nwords); t_s(t, " mixed "); t_d(t, r->mixed_sites);
    t_s(t, " dbg_left_real "); t_d(t, r->dbg_sites); t_s(t, " syscall_init "); t_d(t, r->syscall_init); t_nl(t);
    for (i = 0; i < CBVMC_F_COUNT; ++i) { t_s(t, "  "); t_s(t, fn[i]); t_sp(t); t_s(t, st[r->status[i] < 3u ? r->status[i] : 0u]); t_s(t, " hits "); t_d(t, r->count[i]); t_nl(t); }
    t_s(t, "  hook "); t_x(t, r->hook_addr); t_s(t, " -> EXIInit "); t_x(t, r->hook_target); t_nl(t);
}

static void stage5(const struct cbvmc_ipl_env *e, struct tb *t)
{
    static const uint32_t sysvec[8] = { 0x276AA0FCu, 0x3B705A52u, 0x270AA1FCu, 0x165A5B76u, 0x265A5EF6u, 0x276AA1FCu, 0x165A5A3Eu, 0x3A5A5A5Au };     /* the SDK's system-call vector, XORed with the scanner's T_KEY (see cbvmc_scan.c) */
    const uint32_t lo = e->bs2_lo, hi = e->bs2_hi;
    const uint8_t *im = e->mem + (lo - MEM_LO);
    struct cbvmc_range rg;
    uint32_t a, h, bmin = 0xFFFFFFFFu, bmax = 0, svec = 0, w;
    unsigned i, j, k, nlis = 0, nsite = 0, bestn = 0, nsets = 0, hits;
    const char *best = 0, *seen[8];
    rg.lo = lo; rg.hi = hi;

    t_s(t, "[bs2] window "); t_x(t, lo); t_c(t, '-'); t_x(t, hi); t_s(t, ", hashes are FNV-1a 32 of 64 KB chunks of the (Cubeboot-patched) image:");
    for (k = 0; k < 16u && (k + 1u) * 0x10000u <= hi - lo; ++k) {
        if (!(k & 7u)) t_nl(t);
        h = 0x811C9DC5u;
        for (a = lo + (k << 16); a < lo + ((k + 1u) << 16); a += 4u) { w = mrd(e, a); h = (h ^ (w >> 24)) * 16777619u; h = (h ^ ((w >> 16) & 255u)) * 16777619u; h = (h ^ ((w >> 8) & 255u)) * 16777619u; h = (h ^ (w & 255u)) * 16777619u; }
        t_sp(t); t_x(t, h);
    }
    t_nl(t);

    /* the symbol sets of the 7 known revisions: which one has function prologues where it says EXILock, EXIImm ... are? */
    t_s(t, "[known symbols] prologue = mflr r0 or stwu r1 at the symbol; 8 EXI/CARDProbe functions per set\n");
    for (i = 0; i < e->nsyms; ++i) {
        int dup = 0;
        for (j = 0; j < nsets; ++j) if (seq(seen[j], e->syms[i].set)) dup = 1;
        if (!dup && nsets < 8u) seen[nsets++] = e->syms[i].set;
    }
    for (j = 0; j < nsets; ++j) {
        unsigned ok = 0;
        uint32_t lo = 0xFFFFFFFFu, hi = 0;
        t_s(t, "set "); t_s(t, seen[j]);
        for (k = 0; k < NEXI; ++k) {
            uint32_t ad = sym_find(e, seen[j], exi_names[k]);
            if (!ad) continue;
            w = mrd(e, ad);
            if (prologue_ok(w)) ++ok;
            if (ad < lo) lo = ad;
            if (ad > hi) hi = ad;
        }
        t_s(t, " prologues "); t_d(t, ok); t_s(t, "/8 range "); t_x(t, lo); t_c(t, '-'); t_x(t, hi);
        t_s(t, " EXILock "); t_x(t, sym_find(e, seen[j], "EXILock")); t_c(t, ':'); t_x(t, mrd(e, sym_find(e, seen[j], "EXILock")));
        t_s(t, " EXIImm "); t_x(t, sym_find(e, seen[j], "EXIImm")); t_c(t, ':'); t_x(t, mrd(e, sym_find(e, seen[j], "EXIImm")));
        t_nl(t);
        if (ok > bestn) { bestn = ok; best = seen[j]; bmin = lo; bmax = hi; }
    }
    t_s(t, "best set "); t_s(t, best && bestn >= 5u ? best : "NONE (unknown or unmatched BS2)"); t_s(t, " prologues "); t_d(t, bestn); t_nl(t);
    if (!best || bestn < 5u) { bmin = 0xFFFFFFFFu; bmax = 0; }

    /* the scanner exactly as the game path runs it (strict), then lenient (CBVMC_SCAN_LIBOGC) so that the counts exist even when strict refuses on the first check */
    scan_text(t, "[scan strict]", (ipl_probe_scan_ex(im, lo, &rg, 1, 0u, &g_sr), &g_sr));
    scan_text(t, "[scan lenient]", (ipl_probe_scan_ex(im, lo, &rg, 1, CBVMC_SCAN_LIBOGC, &g_sr2), &g_sr2));
    {
        const struct cbvmc_scan_result *r = g_sr.hook_target ? &g_sr : &g_sr2;
        uint32_t target = r->hook_target;
        if (target) {
            t_s(t, "EXIInit "); t_x(t, target); t_s(t, " callers (bl):");
            hits = 0;
            for (a = lo; a + 4u <= hi; a += 4u) {
                w = rd32(im + (a - lo));
                if ((w >> 26) == 18u && (w & 3u) == 1u) {
                    uint32_t off = w & 0x03FFFFFCu;
                    if (off & 0x02000000u) off |= 0xFC000000u;
                    if (a + off == target) { if (hits < 8u) { t_sp(t); t_x(t, a); } ++hits; }
                }
            }
            t_s(t, "  total "); t_d(t, hits); t_nl(t);
        } else t_s(t, "EXIInit not located\n");
    }
    hits = match_all(im, lo, &rg, 1, sysvec, 8, &svec);
    t_s(t, "SystemCallVector template matches "); t_d(t, hits); t_s(t, " first "); t_x(t, svec);
    if (hits == 1u) {
        t_s(t, " __OSInitSystemCall callers (bl):");
        for (a = lo; a + 4u <= hi; a += 4u) {
            w = rd32(im + (a - lo));
            if ((w >> 26) == 18u && (w & 3u) == 1u) {
                uint32_t off = w & 0x03FFFFFCu;
                if (off & 0x02000000u) off |= 0xFC000000u;
                if (a + off == svec + 0x20u) { t_sp(t); t_x(t, a); }
            }
        }
    }
    t_nl(t);
    t_s(t, "VerifyID shape candidates:");
    hits = 0;
    for (a = lo; a + 60u <= hi; a += 4u) if (verifyid_at(im, lo, a, 0u)) { if (hits < 6u) { t_sp(t); t_x(t, a); } ++hits; }
    t_s(t, " total "); t_d(t, hits); t_nl(t);
    t_s(t, "DoMount compare candidates (branch addr orig patched):");
    hits = 0;
    for (a = lo + 36u; a + 12u <= hi; a += 4u) {
        uint32_t pa, po, pn;
        if (domount_at(im, lo, &rg, 1, a, &pa, &po, &pn, 0u)) { if (hits < 6u) { t_sp(t); t_x(t, pa); t_c(t, '/'); t_x(t, po); t_c(t, '/'); t_x(t, pn); } ++hits; }
    }
    t_s(t, " total "); t_d(t, hits); t_nl(t);

    /* every site that touches the EXI or PI registers, with its class (E exi, P pi cause, M pi mask, O other register) and csr (status regs of channel 0,1,2 = 7, D = EXI2 debugger registers) */
    t_s(t, "[sites] lis rD,0xCC00 reaching EXI/PI registers: addr class csr near-known-EXI-functions\n");
    for (a = lo; a + 4u <= hi; a += 4u) {
        w = rd32(im + (a - lo));
        if ((w >> 26) == 15u && (w & 0xFFFFu) == 0xCC00u) {
            unsigned csr = 0, cls = follow(im, lo, &rg, 1, a, (w >> 21) & 31u, ((w >> 16) & 31u) != 0, &csr);
            ++nlis;
            if (cls & (C_EXI | C_PI)) {
                ++nsite;
                if (nsite <= MAXLIST) {
                    t_x(t, a); t_sp(t);
                    if (cls & C_EXI) t_c(t, 'E');
                    if (cls & C_PI) t_c(t, 'P');
                    if (cls & C_PIM) t_c(t, 'M');
                    if (cls & C_OTHER) t_c(t, 'O');
                    if (csr & K_CH2X) t_c(t, 'D');
                    t_sp(t); t_d(t, csr & 63u);
                    t_s(t, (bmax && a + 0x1000u >= bmin && a <= bmax + 0x400u) ? " near\n" : " far\n");
                }
            }
        }
    }
    t_s(t, "lis_cc00 total "); t_d(t, nlis); t_s(t, " exi_or_pi_sites "); t_d(t, nsite); if (nsite > MAXLIST) t_s(t, " (list cut)"); t_nl(t);
}

unsigned cbvmc_ipl_probe_run(const struct cbvmc_ipl_env *e)
{
    struct tb t;
    unsigned stage;
    uint64_t t0;
    if (!e || !e->mem || !e->emit) return 0;
    t0 = e->now ? e->now() : 0;
    for (stage = 2; stage <= CBVMC_IPL_PROBE_STAGES; ++stage) {
        t.p = g_buf; t.n = 0; t.cap = BUFSZ; g_buf[0] = 0;
        if (stage == 2) stage2(e, &t);
        else if (stage == 3) stage3(e, &t);
        else if (stage == 4) stage4(e, &t);
        else if (stage == 5) { if (e->bs2_lo >= MEM_LO && (e->bs2_lo & 3u) == 0 && e->bs2_hi > e->bs2_lo && e->bs2_hi <= MEM_HI && e->bs2_hi - e->bs2_lo <= 0x400000u) stage5(e, &t); else t_s(&t, "[bs2] window invalid, not scanned\n"); }
        else { t_s(&t, "[done] probe finished, ms since stage 2 "); t_d(&t, ms_since(e, t0)); t_nl(&t); }
        if (t.n + 1u >= t.cap) { t.n = t.cap - 16u; t_s(&t, "\nTRUNCATED\n"); }
        if (e->emit(e->ctx, stage, g_buf, t.n)) return stage - 1u;
    }
    return CBVMC_IPL_PROBE_STAGES - 1u;
}

#endif
