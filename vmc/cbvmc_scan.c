/* Copyright (C) 2026 Nigzel / Bjerreman. SPDX-License-Identifier: GPL-2.0-only */
/* Runtime scanner, see cbvmc_scan.h.  Original work.  Strict C89, no libc. */
#include "cbvmc_scan.h"

#define WINDOW 28u
#define CLUSTER_GAP 0x1000u
#define PI_NEAR 0x200u
#define MAXSITES 96u            /* trap sites kept (EXI or PI class); the six games looked at have 28 to 38; more is an overflow and refuses */

static uint32_t rd32(const uint8_t *p) { return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) | ((uint32_t)p[2] << 8) | p[3]; }
static int32_t s16(uint32_t x) { return (int32_t)(int16_t)(uint16_t)x; }

struct site { uint32_t addr; unsigned char cls, csr, drop, pad; };     /* 8 bytes: the scanner runs on a small stack (the Dolphin harness has 4 KiB) */      /* cls bits: 1 EXI, 2 PI cause, 4 other register */
#define C_EXI 1u
#define C_PI 2u
#define C_OTHER 4u
#define K_CSR_ALL 7u        /* csr bits 0..2: status registers of channel 0, 1, 2 (EXIInit) */
#define K_CH2X 8u           /* csr bit 3: a direct (non-indexed) access to 0x6830..0x683B, the transfer registers of channel 2: the EXI2 debugger link */
#define K_CR0 16u           /* csr bit 4: direct 0x680C (channel 0 control) */
#define K_CR1 32u           /* csr bit 5: direct 0x6820 (channel 1 control) */
#define DBG_NEAR 0x200u     /* sites this close to a debugger site belong to the debugger code (found in Mario Kart: the debugger link is linked beside the EXI library) */
#define C_PIM 8u            /* the PI mask register 0x3004 is read next to the cause: marks the interrupt dispatcher */

static void note(unsigned *cls, int32_t off, int idx)
{
    if ((off >= 0x6800 && off <= 0x683B) || (idx && off >= 0x6800 && off < 0x6860)) *cls |= C_EXI;
    else if (off == 0x3000) *cls |= C_PI;
    else if (off == 0x3004) *cls |= C_PIM;
    else *cls |= C_OTHER;
}

static int is_load(uint32_t op) { return (op >= 32 && op <= 35) || (op >= 40 && op <= 43) || op == 46 || (op >= 48 && op <= 51); }
static int is_ls(uint32_t op) { return is_load(op) || (op >= 36 && op <= 39) || op == 44 || op == 45 || op == 47 || (op >= 52 && op <= 55); }

static int in_rng(const struct cbvmc_range *rg, unsigned nr, uint32_t pc)
{
    unsigned i;
    for (i = 0; i < nr; ++i) if (pc >= rg[i].lo && pc + 4u <= rg[i].hi) return 1;
    return 0;
}

static unsigned follow(const uint8_t *im, uint32_t base, const struct cbvmc_range *rg, unsigned nr, uint32_t a, unsigned rd, unsigned indexed, unsigned *csr)
{
    int32_t val[32]; unsigned has[32], ix[32], cls = 0, k, r;
    for (r = 0; r < 32; ++r) has[r] = 0;
    val[rd] = 0; has[rd] = 1; ix[rd] = indexed;
    for (k = 1; k < WINDOW; ++k) {
        uint32_t pc = a + 4u * k, n, op, f1, f2, imm, xo, rb;
        int any = 0;
        if (!in_rng(rg, nr, pc)) break;
        n = rd32(im + (pc - base));
        if (n == 0x4E800020u) break;
        op = n >> 26; f1 = (n >> 21) & 31u; f2 = (n >> 16) & 31u; imm = n & 0xFFFFu; xo = (n >> 1) & 0x3FFu; rb = (n >> 11) & 31u;
        if (is_ls(op)) {
            if (f2 != 0 && has[f2]) {
                int32_t o = val[f2] + s16(imm);
                note(&cls, o, (int)ix[f2]);
                if (!ix[f2]) {
                    if (o == 0x6800) *csr |= 1u; else if (o == 0x6814) *csr |= 2u; else if (o == 0x6828) *csr |= 4u;
                    else if (o == 0x680C) *csr |= K_CR0; else if (o == 0x6820) *csr |= K_CR1;
                    if (o >= 0x6830 && o <= 0x683B) *csr |= K_CH2X;
                }
            }
            if (is_load(op)) has[f1] = 0;                                  /* a load overwrites that register */
        } else if (op == 14 && f2 != 0 && has[f2]) {
            val[f1] = val[f2] + s16(imm); ix[f1] = ix[f2]; has[f1] = 1;
        } else if (op == 24 && has[f1]) {
            val[f2] = val[f1] + (int32_t)imm; ix[f2] = ix[f1]; has[f2] = 1;
        } else if (op == 31) {
            if (xo == 266 && (has[f2] || has[rb])) {
                int32_t v = has[f2] ? val[f2] : val[rb];
                val[f1] = v; ix[f1] = 1; has[f1] = 1;
            } else if ((xo == 23 || xo == 55 || xo == 87 || xo == 119 || xo == 151 || xo == 215 || xo == 279 || xo == 311 || xo == 343 || xo == 407 ||
                        xo == 534 || xo == 790 || xo == 918 || xo == 662 || xo == 20 || xo == 150) && f2 != 0 && has[f2]) {
                note(&cls, val[f2], 1);
            }
        } else if (op == 18 && (n & 1u)) {
            for (r = 0; r <= 12; ++r) has[r] = 0;
        } else if ((op == 14 || op == 15 || op == 25 || op == 28 || op == 29 || op == 21 || op == 20 || op == 10 || op == 11 || op == 7) && has[f1]) {
            has[f1] = 0;
        }
        for (r = 0; r < 32; ++r) any |= (int)has[r];
        if (!any) break;
    }
    return cls;
}

/* __OSInitSystemCall's source: the 8 words of the SDK's system-call vector (identical in every SDK build looked at).  A fingerprint, stored
   XORed with T_KEY so that no instruction word of the SDK appears in our images; match_all applies the key to the memory it compares. */
#define T_KEY 0x5A5A5A5Au
static const uint32_t T_SYSVEC[] = {
    0x276AA0FCu, 0x3B705A52u, 0x270AA1FCu, 0x165A5B76u, 0x265A5EF6u, 0x276AA1FCu, 0x165A5A3Eu, 0x3A5A5A5Au
};
#define T_SYSVEC_N (sizeof(T_SYSVEC) / sizeof(T_SYSVEC[0]))

/* VerifyID (CARDMount.c: card header serial / size / encoding against the console), structural: the SDK's compiler builds differ in register
   allocation and in the number of saved registers, not in the shape of the prologue.  Fifteen words:
     mflr r0 ; stw r0,4(r1) ; stwu r1,-N(r1) ; stmw rK,..(r1) ; mr rX,r3 ; lwz r3,0x80(r3) (card->diskID) ; lhz r0,0x20(r3) (deviceID) ;
     addi rY,r3,0 ; cmplwi r0,0 ; bne +0x14 ; lhz r3,0x22(rY) (id->size) ; lhz r0,8(rX) (card->size) ; cmplw r3,r0 ; beq +0xC ; li r3,-6 (CARD_RESULT_BROKEN)
   with rY the register of the addi and rX the register of the mr.  The first two words become `li r3,0 ; blr`. */
static int verifyid_at(const uint8_t *im, uint32_t base, uint32_t a, unsigned idem)
{
    uint32_t w[15], i, rx, ry;
    w[0] = rd32(im + (a - base));                                       /* the first word decides for almost every position: read the rest only then (same result, a header-less window is large) */
    if (w[0] != 0x7C0802A6u && !(idem && w[0] == 0x38600000u)) return 0;
    for (i = 1; i < 15u; ++i) w[i] = rd32(im + (a + 4u * i - base));
    if (idem && w[0] == 0x38600000u && w[1] == 0x4E800020u) { w[0] = 0x7C0802A6u; w[1] = 0x90010004u; }     /* already patched by this rule set: the rest of the shape must still hold */
    if (w[0] != 0x7C0802A6u || w[1] != 0x90010004u) return 0;
    if ((w[2] & 0xFFFF8000u) != 0x94218000u) return 0;                 /* stwu r1,-N(r1) */
    if ((w[3] & 0xFC000000u) != 0xBC000000u) return 0;                 /* stmw */
    if ((w[4] & 0xFFE0FFFFu) != 0x7C601B78u) return 0;                 /* mr rX,r3 */
    rx = (w[4] >> 16) & 31u;
    if (w[5] != 0x80630080u || w[6] != 0xA0030020u) return 0;
    if ((w[7] & 0xFC1FFFFFu) != 0x38030000u) return 0;                 /* addi rY,r3,0 */
    ry = (w[7] >> 21) & 31u;
    if (w[8] != 0x28000000u || w[9] != 0x40820014u) return 0;
    if ((w[10] & 0xFFE0FFFFu) != 0xA0600022u || ((w[10] >> 16) & 31u) != ry) return 0;
    if ((w[11] & 0xFFE0FFFFu) != 0xA0000008u || ((w[11] >> 16) & 31u) != rx) return 0;
    if (w[12] != 0x7C030040u || w[13] != 0x4182000Cu || w[14] != 0x3860FFFAu) return 0;
    return 1;
}

/* DoMount's SRAM flash-ID checksum compare, structural (both the Nintendo SDK build and libogc have it):
     lbz rL,0x26(rY) ; ... ; nor rA,rB,rB ; rlwinm rA,rA,0,24,31 ; cmplw rL,rA ; bne|beq
   The branch is the "checksum differs" test of the SRAM entry of this channel; the virtual card cannot satisfy it (no console history), so the compare is forced
   to succeed: beq -> b (same displacement), bne -> nop. */
static int domount_at(const uint8_t *im, uint32_t base, const struct cbvmc_range *rg, unsigned nr, uint32_t a, uint32_t *patch_addr, uint32_t *orig, uint32_t *patched, unsigned idem)
{
    uint32_t w0, w1, w2, ra, k, found_lbz = 0, found_nor = 0, rl = 0, done = 0;
    if (!in_rng(rg, nr, a - 36u) || !in_rng(rg, nr, a + 8u)) return 0;
    w0 = rd32(im + (a - base));
    if ((w0 & 0xFC00FFFFu) != 0x5400063Eu) return 0;                                 /* rlwinm rA,rS,0,24,31 = clrlwi rA,rS,24 */
    w1 = rd32(im + (a + 4u - base)); w2 = rd32(im + (a + 8u - base));
    if ((w1 & 0xFC4007FFu) != 0x7C000040u) return 0;                                 /* cmplw crf,rL,rR */
    if (idem && (w2 == 0x60000000u || (w2 & 0xFFFF0003u) == 0x48000000u)) done = 1;  /* the compare this rule set already forced (bne -> nop, beq -> b) */
    if (!done && (w2 >> 16) != 0x4082u && (w2 >> 16) != 0x4182u) return 0;
    if (w2 & 3u) return 0;
    ra = (w0 >> 16) & 31u;                                                           /* destination of the clrlwi */
    if (((w0 >> 21) & 31u) != ra) return 0;                                          /* clrlwi rA,rA,24 */
    if ((((w1 >> 11) & 31u) != ra) && (((w1 >> 16) & 31u) != ra)) return 0;
    rl = (((w1 >> 11) & 31u) == ra) ? ((w1 >> 16) & 31u) : ((w1 >> 11) & 31u);
    for (k = 1; k <= 8u; ++k) {
        uint32_t w = rd32(im + (a - 4u * k - base));
        if (!found_nor && (w & 0xFC0007FFu) == 0x7C0000F8u && ((w >> 21) & 31u) == ((w >> 11) & 31u) && ((w >> 16) & 31u) == ra) found_nor = 1;
        if (!found_lbz && (w >> 26) == 34u && (w & 0xFFFFu) == 0x26u && ((w >> 21) & 31u) == rl) found_lbz = 1;
    }
    if (!found_nor || !found_lbz) return 0;
    *patch_addr = a + 8u; *orig = w2;
    *patched = done ? w2 : (w2 >> 16) == 0x4182u ? (0x48000000u | (w2 & 0xFFFCu)) : 0x60000000u;
    return 1;
}
static unsigned match_all(const uint8_t *im, uint32_t base, const struct cbvmc_range *rg, unsigned nr, const uint32_t *t, unsigned n, uint32_t *first)
{
    uint32_t a; unsigned hits = 0, i, k;
    for (k = 0; k < nr; ++k)
        for (a = rg[k].lo; a + 4u * n <= rg[k].hi; a += 4u) {
            for (i = 0; i < n; ++i) if ((rd32(im + (a + 4u * i - base)) ^ T_KEY) != t[i]) break;
            if (i == n) { if (hits == 0) *first = a; ++hits; }
        }
    return hits;
}

static void add_word(struct cbvmc_scan_result *r, uint32_t addr, uint32_t orig, uint32_t patched)
{
    unsigned i, j;
    if (r->nwords >= CBVMC_SCAN_MAX_WORDS) { r->ok = -1; return; }
    for (i = 0; i < r->nwords && r->words[i].addr < addr; ++i) {}
    for (j = r->nwords; j > i; --j) r->words[j] = r->words[j - 1];
    r->words[i].addr = addr; r->words[i].orig = orig; r->words[i].patched = patched;
    ++r->nwords;
}

/* EXIInit = the function with accesses to the status registers of channels 0, 1, 2 (0x6800, 0x6814, 0x6828) behind ONE `lis` */

static uint32_t func_start(const uint8_t *im, uint32_t base, const struct cbvmc_range *rg, unsigned nr, uint32_t a)
{
    unsigned k;
    for (k = 0; k < 40u; ++k) {
        uint32_t p = a - 4u * k, w;
        if (!in_rng(rg, nr, p - 8u)) return 0;
        w = rd32(im + (p - base));
        if ((w >> 16) == 0x9421u) {                                  /* stwu r1,-n(r1) */
            uint32_t q = p, j;
            for (j = 0; j < 4u; ++j) { if (rd32(im + (q - 4u - base)) == 0x7C0802A6u) return q - 4u; q -= 4u; }   /* mflr r0 up to 4 words before */
            return p;
        }
    }
    return 0;
}

static uint32_t dist(uint32_t a, uint32_t b) { return a > b ? a - b : b - a; }

void cbvmc_scan(const uint8_t *image, uint32_t base, const struct cbvmc_range *rng, unsigned nrng, struct cbvmc_scan_result *r)
{
    cbvmc_scan_ex(image, base, rng, nrng, 0u, r);
}

void cbvmc_scan_ex(const uint8_t *image, uint32_t base, const struct cbvmc_range *rng, unsigned nrng, unsigned flags, struct cbvmc_scan_result *r)
{
    struct site sites[MAXSITES];
    unsigned rk, ns = 0, i, j, f, idem = (flags & CBVMC_SCAN_IDEMPOTENT) ? 1u : 0u;
    uint32_t a, exiinit = 0, e_site = 0, vfirst = 0, dm_addr = 0, dm_orig = 0, dm_new = 0, svec = 0;
    unsigned char core[MAXSITES];

    r->nwords = 0; r->hook_addr = 0; r->hook_target = 0; r->lis_sites = 0; r->mixed_sites = 0; r->ok = 0;
    r->dbg_sites = 0; r->syscall_init = 0; r->refuse = CBVMC_R_NONE;
    for (f = 0; f < CBVMC_F_COUNT; ++f) { r->status[f] = CBVMC_S_NOT_FOUND; r->count[f] = 0; }
    for (rk = 0; rk < nrng; ++rk) {                                   /* the blob (low memory) and the staging area must be free of game code */
        if (rng[rk].lo < 0x80003000u && rng[rk].hi > 0x80000C00u) r->refuse = CBVMC_R_RANGE;
        if (rng[rk].hi > 0x817B0000u) r->refuse = CBVMC_R_RANGE;
    }
    r->serial = 0;
    for (rk = 0; rk < nrng; ++rk)
    for (a = rng[rk].lo; a + 4u <= rng[rk].hi; a += 4u) {
        uint32_t w = rd32(image + (a - base));
        if (!r->serial && (w >> 26) == 15u && (w & 0xFFFFu) == 0x41C6u && a + 20u <= rng[rk].hi) {
            /* the card library's serial check (its LCG constant 0x41C64E6D: lis rX,0x41C6 then ori|addi rY,rX,0x4E6D within four words).  Without it the
               executable has no VerifyID serial compare and no DoMount flash-ID compare to force (EA builds, boot-only executables): their absence is then no refusal */
            uint32_t j;
            for (j = 1; j <= 4u; ++j) {
                uint32_t n = rd32(image + (a + 4u * j - base));
                if ((n & 0xFFFFu) == 0x4E6Du && ((n >> 26) == 24u || (n >> 26) == 14u) && ((n >> 16) & 31u) == ((w >> 21) & 31u)) r->serial = 1;
            }
        }
        if ((w >> 26) == 15u && ((w & 0xFFFFu) == 0xCC00u || (idem && (w & 0xFFFFu) == 0x0C00u))) {   /* lis rD,0xCC00, or addis rD,rS,0xCC00 (rS = an index computed before: chan*0x14); 0x0C00 = patched before (T2) */
            unsigned csr = 0, cls = follow(image, base, rng, nrng, a, (w >> 21) & 31u, ((w >> 16) & 31u) != 0, &csr);
            ++r->lis_sites;
            if (cls & (C_EXI | C_PI)) {
                if (ns < MAXSITES) { sites[ns].addr = a; sites[ns].cls = (unsigned char)cls; sites[ns].csr = (unsigned char)csr; sites[ns].drop = 0; sites[ns].pad = 0; ++ns; }
                else r->ok = -1;                                          /* too many: refuse (overflow), never silently drop one */
            }
        }
    }
    /* EXIInit: the one site that touches the status registers of all three channels and no other register */
    for (i = 0; i < ns; ++i)
        if ((sites[i].cls & C_EXI) && e_site == 0 && (sites[i].csr & K_CSR_ALL) == K_CSR_ALL && !(sites[i].cls & (C_OTHER | C_PI))) e_site = sites[i].addr;
    /* The EXI2 debugger link (MetroTRK / debug interface) also pokes EXI registers directly, with the transfer registers of channel 2, and in some
       builds is linked right beside the EXI library (Mario Kart: 0x3E8 bytes away).  It is not card access: it and its neighbours (within DBG_NEAR)
       stay real and are dropped from every later step.  EXIInit's control-register clear (0x680C, 0x6820 and 0x6834 together) is not the debugger. */
    for (i = 0; i < ns; ++i) {
        unsigned isdbg = (sites[i].csr & K_CH2X) && !((sites[i].csr & K_CR0) && (sites[i].csr & K_CR1));
        if (!isdbg) continue;
        sites[i].drop |= 2u;
    }
    for (i = 0; i < ns; ++i) {
        if (sites[i].drop & 2u) continue;
        if (sites[i].addr == e_site) continue;
        if ((sites[i].cls & C_PI) && (sites[i].cls & C_PIM) && !(sites[i].cls & C_EXI)) continue;     /* the dispatcher is never debugger code */
        for (j = 0; j < ns; ++j)
            if ((sites[j].drop & 2u) && dist(sites[i].addr, sites[j].addr) <= DBG_NEAR) { sites[i].drop |= 1u; break; }
    }
    for (i = 0; i < ns; ++i) if (sites[i].drop) ++r->dbg_sites;
    /* The EXI library = every run of EXI-class sites (consecutive gaps <= CLUSTER_GAP bytes) that is anchored: it holds EXIInit, or a dispatcher site (reads both the PI cause 0x3000 and the PI mask 0x3004) lies within PI_NEAR
       bytes of it (the interrupt dispatcher and the interrupt-mask code beside it).  Unanchored runs (RTC/SRAM code, reset code) also touch EXI but are not
       card access and stay real, as the reference table had them.  PI-cause sites join when they lie within PI_NEAR of a core site. */
    for (i = 0; i < ns; ++i) core[i] = 0;
    {
        unsigned s0 = 0, e0, k2;
        while (s0 < ns) {
            int anch = 0;
            if (sites[s0].drop || !(sites[s0].cls & C_EXI)) { ++s0; continue; }
            e0 = s0;
            for (j = s0 + 1; j < ns; ++j) {
                if (sites[j].drop || !(sites[j].cls & C_EXI)) continue;
                if (sites[j].addr - sites[e0].addr > CLUSTER_GAP) break;
                e0 = j;
            }
            for (j = s0; j <= e0 && !anch; ++j) {
                if (sites[j].drop || !(sites[j].cls & C_EXI)) continue;
                if (sites[j].addr == e_site) anch = 1;
                for (k2 = 0; k2 < ns && !anch; ++k2)
                    if (!sites[k2].drop && (sites[k2].cls & C_PI) && (sites[k2].cls & C_PIM) && !(sites[k2].cls & C_EXI) &&
                        dist(sites[k2].addr, sites[j].addr) <= PI_NEAR) anch = 1;
            }
            if (anch) for (j = s0; j <= e0; ++j) if (!sites[j].drop && (sites[j].cls & C_EXI)) core[j] = 1;
            s0 = e0 + 1;
        }
        for (i = 0; i < ns; ++i) {
            if (sites[i].drop || !((sites[i].cls & C_PI) && !(sites[i].cls & C_EXI))) continue;
            for (j = 0; j < ns; ++j) if (core[j] && dist(sites[i].addr, sites[j].addr) <= PI_NEAR) { core[i] = 1; break; }
        }
    }
    for (i = 0; i < ns; ++i) {
        uint32_t w;
        if (!core[i]) continue;
        w = rd32(image + (sites[i].addr - base));
        add_word(r, sites[i].addr, w, (w & 0xFFFF0000u) | 0x0C00u);
        if (sites[i].cls & C_PI) ++r->count[CBVMC_F_PI_CAUSE]; else ++r->count[CBVMC_F_EXI_LIB];
        if ((sites[i].cls & C_OTHER)) ++r->mixed_sites;
    }
    r->status[CBVMC_F_EXI_LIB] = (r->count[CBVMC_F_EXI_LIB] >= 8u && r->count[CBVMC_F_EXI_LIB] <= 32u) ? CBVMC_S_FOUND : CBVMC_S_NOT_FOUND;
    r->status[CBVMC_F_PI_CAUSE] = r->count[CBVMC_F_PI_CAUSE] >= 1u ? CBVMC_S_FOUND : CBVMC_S_NOT_FOUND;
    /* EXIInit and its single call */
    if (e_site) exiinit = func_start(image, base, rng, nrng, e_site);
    if (exiinit) {
        unsigned hits = 0;
        for (rk = 0; rk < nrng; ++rk)
        for (a = rng[rk].lo; a + 4u <= rng[rk].hi; a += 4u) {
            uint32_t w = rd32(image + (a - base));
            if ((w >> 26) == 18u && (w & 3u) == 1u) {
                uint32_t off = w & 0x03FFFFFCu, t;
                if (off & 0x02000000u) off |= 0xFC000000u;
                t = a + off;
                if (t == exiinit) { ++hits; r->hook_addr = a; }
            }
        }
        r->hook_target = exiinit; r->count[CBVMC_F_EXIINIT] = hits;
        r->status[CBVMC_F_EXIINIT] = hits == 1u ? CBVMC_S_FOUND : (hits ? CBVMC_S_AMBIGUOUS : CBVMC_S_NOT_FOUND);
        if (hits != 1u) r->hook_addr = 0;
    }
    /* the install point must sit in an OSInit that already ran __OSInitSystemCall (= SystemCallVector copy + 0x20) within 0x100 bytes before it:
       then OSExceptionInit has run too (DSI stub in place) and nothing of the card library has been used yet */
    if (r->hook_addr && match_all(image, base, rng, nrng, T_SYSVEC, T_SYSVEC_N, &svec) == 1u) {
        uint32_t isc = svec + 0x20u, c;
        for (c = r->hook_addr - 0x100u; c < r->hook_addr; c += 4u) {
            uint32_t w;
            if (!in_rng(rng, nrng, c)) continue;
            w = rd32(image + (c - base));
            if ((w >> 26) == 18u && (w & 3u) == 1u) {
                uint32_t off = w & 0x03FFFFFCu;
                if (off & 0x02000000u) off |= 0xFC000000u;
                if (c + off == isc) r->syscall_init = 1;
            }
        }
    }
    /* card library words */
    for (rk = 0; rk < nrng; ++rk)
        for (a = rng[rk].lo; a + 60u <= rng[rk].hi; a += 4u)
            if (verifyid_at(image, base, a, idem)) { if (r->count[CBVMC_F_VERIFYID] == 0) vfirst = a; ++r->count[CBVMC_F_VERIFYID]; }
    r->status[CBVMC_F_VERIFYID] = r->count[CBVMC_F_VERIFYID] == 1u ? CBVMC_S_FOUND : (r->count[CBVMC_F_VERIFYID] ? CBVMC_S_AMBIGUOUS : CBVMC_S_NOT_FOUND);
    for (rk = 0; rk < nrng; ++rk)
        for (a = rng[rk].lo + 36u; a + 12u <= rng[rk].hi; a += 4u)
            if (domount_at(image, base, rng, nrng, a, &dm_addr, &dm_orig, &dm_new, idem)) ++r->count[CBVMC_F_DOMOUNT];
    r->status[CBVMC_F_DOMOUNT] = r->count[CBVMC_F_DOMOUNT] == 1u ? CBVMC_S_FOUND : (r->count[CBVMC_F_DOMOUNT] ? CBVMC_S_AMBIGUOUS : CBVMC_S_NOT_FOUND);
    if (r->status[CBVMC_F_VERIFYID] == CBVMC_S_FOUND) {
        add_word(r, vfirst, rd32(image + (vfirst - base)), 0x38600000u);                 /* li r3,0 */
        add_word(r, vfirst + 4u, rd32(image + (vfirst + 4u - base)), 0x4E800020u);       /* blr */
    }
    if (r->status[CBVMC_F_DOMOUNT] == CBVMC_S_FOUND) add_word(r, dm_addr, dm_orig, dm_new);
    /* the verdict: the first failing check names the reason */
    if (r->refuse == CBVMC_R_NONE) {
        if (r->status[CBVMC_F_EXI_LIB] != CBVMC_S_FOUND) r->refuse = CBVMC_R_EXI_LIB;
        else if (r->status[CBVMC_F_PI_CAUSE] != CBVMC_S_FOUND) r->refuse = CBVMC_R_PI_CAUSE;
        else if (!e_site || !exiinit) r->refuse = CBVMC_R_EXIINIT;
        else if (r->status[CBVMC_F_EXIINIT] != CBVMC_S_FOUND) r->refuse = CBVMC_R_EXIINIT_CALLS;
        else if (!r->syscall_init && !(flags & CBVMC_SCAN_LIBOGC)) r->refuse = CBVMC_R_SYSCALL;
        else if (r->status[CBVMC_F_VERIFYID] == CBVMC_S_AMBIGUOUS) r->refuse = CBVMC_R_VERIFYID_AMBIG;
        else if (r->status[CBVMC_F_VERIFYID] != CBVMC_S_FOUND && r->serial && !(flags & CBVMC_SCAN_LIBOGC)) r->refuse = CBVMC_R_VERIFYID;
        else if (r->status[CBVMC_F_DOMOUNT] == CBVMC_S_AMBIGUOUS) r->refuse = CBVMC_R_DOMOUNT_AMBIG;
        else if (r->status[CBVMC_F_DOMOUNT] != CBVMC_S_FOUND && r->serial) r->refuse = CBVMC_R_DOMOUNT;
        else if (r->mixed_sites && !(flags & CBVMC_SCAN_LIBOGC)) r->refuse = CBVMC_R_MIXED;
    }
    if (r->refuse == CBVMC_R_NONE && r->ok < 0) r->refuse = CBVMC_R_OVERFLOW;
    if (r->refuse != CBVMC_R_NONE) r->nwords = 0;                                         /* a refused game gets no table at all */
    r->ok = (r->refuse == CBVMC_R_NONE) ? 1 : 0;
}

/* ---- the report: plain text for /cubeboot/vmc_scan.txt (no libc) ---- */
static unsigned rp_s(char *o, unsigned n, unsigned cap, const char *s) { while (*s && n + 1u < cap) o[n++] = *s++; return n; }
static unsigned rp_d(char *o, unsigned n, unsigned cap, unsigned v)
{
    char t[12]; unsigned k = 0;
    do { t[k++] = (char)('0' + v % 10u); v /= 10u; } while (v);
    while (k && n + 1u < cap) o[n++] = t[--k];
    return n;
}
static unsigned rp_x(char *o, unsigned n, unsigned cap, uint32_t v)
{
    unsigned i;
    for (i = 0; i < 8u && n + 1u < cap; ++i) o[n++] = "0123456789ABCDEF"[(v >> (28u - 4u * i)) & 15u];
    return n;
}

unsigned cbvmc_scan_report(const struct cbvmc_scan_result *r, const struct cbvmc_range *rng, unsigned nrng, const char *game_id, char *out, unsigned cap)
{
    static const char *const fn[CBVMC_F_COUNT] = { "exi_lib", "pi_cause", "exiinit", "verifyid", "domount" };
    static const char *const st[3] = { "not-found", "found", "ambiguous" };
    unsigned n = 0, i;
    if (cap == 0) return 0;
    n = rp_s(out, n, cap, "cbvmc generic scan report\ngame ");
    for (i = 0; i < 4 && game_id && game_id[i]; ++i) if (n + 1u < cap) out[n++] = game_id[i];
    n = rp_s(out, n, cap, "\ntext");
    for (i = 0; i < nrng; ++i) { n = rp_s(out, n, cap, " "); n = rp_x(out, n, cap, rng[i].lo); n = rp_s(out, n, cap, "-"); n = rp_x(out, n, cap, rng[i].hi); }
    n = rp_s(out, n, cap, "\nlis_cc00 "); n = rp_d(out, n, cap, r->lis_sites);
    n = rp_s(out, n, cap, " words "); n = rp_d(out, n, cap, r->nwords);
    n = rp_s(out, n, cap, " mixed "); n = rp_d(out, n, cap, r->mixed_sites); n = rp_s(out, n, cap, "\n");
    for (i = 0; i < CBVMC_F_COUNT; ++i) {
        n = rp_s(out, n, cap, fn[i]); n = rp_s(out, n, cap, " "); n = rp_s(out, n, cap, st[r->status[i] < 3u ? r->status[i] : 0u]);
        n = rp_s(out, n, cap, " hits "); n = rp_d(out, n, cap, r->count[i]); n = rp_s(out, n, cap, "\n");
    }
    n = rp_s(out, n, cap, "hook "); n = rp_x(out, n, cap, r->hook_addr); n = rp_s(out, n, cap, " -> EXIInit "); n = rp_x(out, n, cap, r->hook_target);
    n = rp_s(out, n, cap, "\nsyscall_init "); n = rp_d(out, n, cap, r->syscall_init); n = rp_s(out, n, cap, " debugger_sites_left_real "); n = rp_d(out, n, cap, r->dbg_sites);
    if (r->ok > 0) n = rp_s(out, n, cap, "\nresult OK\n");
    else {
        static const char *const why[CBVMC_R_COUNT] = {
            "", "a code section overlaps the runtime blob or the staging area",
            "no SDK-style EXI library (site count out of range)", "no PI cause read in an interrupt dispatcher",
            "EXIInit not found", "EXIInit is not called from exactly one place",
            "install point is not right after __OSInitSystemCall in OSInit", "card serial check (VerifyID) not found, the card would be reported damaged",
            "card serial check (VerifyID) found more than once", "DoMount flash-ID compare not found", "DoMount flash-ID compare found more than once",
            "a patched site also touches non-EXI registers", "word table overflow" };
        n = rp_s(out, n, cap, "\nresult REFUSED: "); n = rp_s(out, n, cap, r->refuse < CBVMC_R_COUNT ? why[r->refuse] : "unknown");
        n = rp_s(out, n, cap, "\ngame left untouched, boots without the virtual card\n");
    }
    out[n] = 0;
    return n;
}
