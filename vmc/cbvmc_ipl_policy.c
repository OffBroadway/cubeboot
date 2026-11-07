/* Copyright (C) 2026 Nigzel / Bjerreman. SPDX-License-Identifier: GPL-2.0-only */
/* G4 stage 1 install decision, see cbvmc_ipl_policy.h.  Original work.  Portable C99, no libc. */
#include "cbvmc_ipl_policy.h"

#define MEM_LO 0x80000000u
#define MEM_HI 0x81800000u

static uint32_t mrd(const struct cbvmc_ipl_in *in, uint32_t a)
{
    const uint8_t *p;
    if (a < MEM_LO || a > MEM_HI - 4u || (a & 3u)) return 0;
    p = in->mem + (a - MEM_LO);
    return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) | ((uint32_t)p[2] << 8) | p[3];
}

int cbvmc_ipl_bat_covers(uint32_t u, uint32_t l, uint32_t ea)
{
    uint32_t bl = (u >> 2) & 0x7FFu, mask = 0xFFFE0000u & ~(bl << 17);
    (void)l;
    if ((u & 3u) == 0) return 0;                        /* neither Vs nor Vp: the entry is invalid */
    return ((ea ^ u) & mask) == 0;
}

static int any_covers(const uint32_t *bat, unsigned first_pair, uint32_t ea, uint32_t *lower)
{
    unsigned i;
    for (i = 0; i < 4; ++i)
        if (cbvmc_ipl_bat_covers(bat[2u * (first_pair + i)], bat[2u * (first_pair + i) + 1u], ea)) { if (lower) *lower = bat[2u * (first_pair + i) + 1u]; return 1; }
    return 0;
}

static int prologue_ok(uint32_t w) { return w == 0x7C0802A6u || (w >> 16) == 0x9421u; }       /* mflr r0 | stwu r1,x(r1) */

const char *cbvmc_ipl_refuse_text(unsigned r)
{
    static const char *const t[CBVMC_IPLR_COUNT] = {
        "ok", "(unused)", "vmc = 0 in config.ini", "skip button held", "(unused)",
        "address translation is off", "BATs: RAM, I/O window or the 0x0C000000 trap window not as required", "DSI vector is not the SDK stub",
        "scanner refused the BS2 text", "no known symbol set matches the BS2 code", "matching symbol set belongs to another revision",
        "a word to patch lies outside the EXI/CARD block", "patch image plus stage-1 framebuffer reach the region", "region is not all zero",
        "not enough stack for the installer", "both slots hold a physical device", "a card channel is in use"
    };
    return r < CBVMC_IPLR_COUNT ? t[r] : "?";
}

unsigned cbvmc_ipl_decide(const struct cbvmc_ipl_in *in, struct cbvmc_ipl_out *out)
{
    struct cbvmc_range rg;
    uint32_t lower = 0, a, pe, xfb_end, b;
    unsigned i, j, bestn = 0;
    int best = -1;

    out->refuse = CBVMC_IPLR_OK; out->detail = 0; out->best_set = -1; out->best_prologues = 0; out->blk_lo = out->blk_hi = 0;
    out->scan.nwords = 0; out->scan.ok = 0;
#define REFUSE(code, det) do { out->refuse = (code); out->detail = (uint32_t)(det); return out->refuse; } while (0)

    if (!in || !in->mem) REFUSE(CBVMC_IPLR_VECTOR, 0);
    if (!in->vmc_enabled) REFUSE(CBVMC_IPLR_VMC_OFF, 0);
    if (in->z_held) REFUSE(CBVMC_IPLR_BUTTON, 0);

    if (in->ipl_rev == 0) REFUSE(CBVMC_IPLR_REV, 0);
    if ((in->msr & 0x30u) != 0x30u) REFUSE(CBVMC_IPLR_MSR, in->msr);                  /* IR and DR */
    /* RAM cached and executable, the I/O window uncached, and the trap window NOT mapped by any data BAT (the page tables are off: SDR1 is not consulted by the BS2 build) */
    if (!any_covers(in->bat, 0, in->region_base, 0) || !any_covers(in->bat, 0, 0x80000300u, 0)) REFUSE(CBVMC_IPLR_BAT, 1);
    if (!any_covers(in->bat, 4, 0x80000000u, &lower) || !any_covers(in->bat, 4, MEM_HI - 4u, 0) || (lower & 0x20u)) REFUSE(CBVMC_IPLR_BAT, 2);          /* RAM must be cacheable */
    if (!any_covers(in->bat, 4, 0xCC006800u, &lower) || !(lower & 0x20u)) REFUSE(CBVMC_IPLR_BAT, 3);                                                    /* I/O window cache-inhibited */
    if (any_covers(in->bat, 4, 0x0C000000u, 0) || any_covers(in->bat, 4, 0x0C006800u, 0) || any_covers(in->bat, 4, 0x0C00FFFCu, 0)) REFUSE(CBVMC_IPLR_BAT, 4);
    if (mrd(in, 0x80000300u) != 0x7C9043A6u) REFUSE(CBVMC_IPLR_VECTOR, mrd(in, 0x80000300u));

    rg.lo = CBVMC_IPL_BS2_LO; rg.hi = CBVMC_IPL_BS2_HI;
    cbvmc_scan_ex(in->mem + (CBVMC_IPL_BS2_LO - MEM_LO), CBVMC_IPL_BS2_LO, &rg, 1, 0u, &out->scan);
    if (!out->scan.ok) REFUSE(CBVMC_IPLR_SCAN, out->scan.refuse);

    for (i = 0; i < in->nsets; ++i) {
        unsigned n = 0;
        for (j = 0; j < CBVMC_IPL_NFUNC; ++j) if (in->sets[i].fn[j] && prologue_ok(mrd(in, in->sets[i].fn[j]))) ++n;
        if (n > bestn) { bestn = n; best = (int)i; }
    }
    out->best_set = best; out->best_prologues = bestn;
    if (best < 0 || bestn < CBVMC_IPL_MIN_PROLOGUES) REFUSE(CBVMC_IPLR_SYMSET, bestn);
    if (!((in->sets[best].rev_mask >> (in->ipl_rev & 31u)) & 1u)) REFUSE(CBVMC_IPLR_SYMREV, in->ipl_rev);
    b = 0xFFFFFFFFu; a = 0;
    for (j = 0; j < CBVMC_IPL_NFUNC; ++j) { uint32_t f = in->sets[best].fn[j]; if (f && f < b) b = f; if (f > a) a = f; }
    out->blk_lo = b; out->blk_hi = a;
    for (i = 0; i < out->scan.nwords; ++i) {
        uint32_t w = out->scan.words[i].addr;
        if (w + CBVMC_IPL_MARGIN_BELOW < b || w > a + CBVMC_IPL_MARGIN_ABOVE) REFUSE(CBVMC_IPLR_SITES, i);
    }

    /* the patch image (code, data, the embedded images) must end below the region.  The stage-1 framebuffer that follows it may reach into the
       region: it is dead once BS2 runs (BS2 scans out its own XFBs from its arena), and the region is zeroed before the images are copied; so no
       all-zero check any more (2026-11-06, the T2+G4 image needs the full 64 KiB). */
    pe = (in->patches_end + 0x3FFu) & ~0x3FFu;
    xfb_end = pe + CBVMC_IPL_XFB_MAX;
    if (in->patches_end < 0x81600000u || pe > in->region_base || in->region_base + CBVMC_IPL_SIZE > MEM_HI) REFUSE(CBVMC_IPLR_REGION_XFB, xfb_end);
    (void)a;
    if (in->sp < in->stack_end || in->sp - in->stack_end < CBVMC_IPL_STACK_MIN || in->stack_end < MEM_LO || in->sp >= MEM_HI) REFUSE(CBVMC_IPLR_STACK, in->sp - in->stack_end);
    if (!(in->allow & 3u)) REFUSE(CBVMC_IPLR_SLOTS, 0);
    if (in->exi_busy) REFUSE(CBVMC_IPLR_EXI_BUSY, in->exi_busy);
#undef REFUSE
    return CBVMC_IPLR_OK;
}
