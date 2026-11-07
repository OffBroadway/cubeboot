/* Copyright (C) 2026 Nigzel / Bjerreman. SPDX-License-Identifier: GPL-2.0-only */
#ifndef CBVMC_IPL_POLICY_H
#define CBVMC_IPL_POLICY_H

/*
 * G4 stage 1 install decision (docs/virtual_memory_card.md).  Original work.  Portable, no libc, no hardware: the Cubeboot patch
 * (cbvmc_ipl_dev.c) gathers the inputs from the machine, calls cbvmc_ipl_decide(), and only when it returns CBVMC_IPLR_OK copies the images and calls the
 * installer.  The same function runs in the host test against the numbers the console measured (docs/data/ipl_probe_console_2026-10-10.txt).
 * It reads memory through a byte view and never writes anything but its own output.
 *
 * Order of the checks (the first failing one names the refusal; each is cheap, none has a side effect):
 *   switches    vmc = 0 / a held button
 *   machine     MSR translation on, BATs: RAM cached and the I/O window uncached are mapped, 0x0C000000 is NOT
 *               (otherwise the patched accesses would not fault), the DSI stub is the SDK's
 *   code        the strict scanner (cbvmc_scan) accepts the BS2 text window; the known-symbol set of the revision matches (>= 6 of its 8 EXI/CARD functions start
 *               with a prologue), agrees with get_ipl_revision(), and EVERY word to patch lies inside that set's EXI/CARD block (margin below)
 *   memory      the Cubeboot patch image plus the stage-1 framebuffer ends below the region; the 64 KiB region is all zero; the caller's stack has room for the
 *               installer (the card validation holds two 8 KiB blocks on the stack)
 *   devices     at least one slot is free of a physical device; the card channels are quiet
 */
#include <stdint.h>

#include "cbvmc_ipl_plan.h"
#include "cbvmc_scan.h"

#define CBVMC_IPL_NFUNC 8u                      /* EXIImm EXIImmEx EXISync EXISelect EXIDeselect EXILock EXIUnlock CARDProbe */
#define CBVMC_IPL_MAXSETS 8u
#define CBVMC_IPL_MIN_PROLOGUES 6u
#define CBVMC_IPL_MARGIN_BELOW 0x1800u          /* the card library (VerifyID, DoMount) lies below the EXI block */
#define CBVMC_IPL_MARGIN_ABOVE 0x0800u
#define CBVMC_IPL_STACK_MIN 0x7000u             /* bytes between the caller's r1 and the thread's stack end the installer may use (inspect: 16.5 KiB + calls) */
#define CBVMC_IPL_BS2_LO 0x81300000u
#define CBVMC_IPL_BS2_HI 0x81400000u

struct cbvmc_ipl_symset {
    const char *name;                           /* "pal12" */
    uint32_t rev_mask;                          /* ipl_revision_t ids this set belongs to */
    uint32_t fn[CBVMC_IPL_NFUNC];               /* the 8 function addresses (0 = unknown) */
};

struct cbvmc_ipl_in {
    const uint8_t *mem;                         /* byte view of cached MEM1: mem[0] is 0x80000000, 0x01800000 bytes */
    uint32_t vmc_enabled;                       /* config.ini vmc */
    uint32_t z_held;                            /* the skip button is held */
    uint32_t ipl_rev;                           /* get_ipl_revision() */
    uint32_t msr;
    uint32_t bat[16];                           /* IBAT0U IBAT0L .. IBAT3L, DBAT0U DBAT0L .. DBAT3L */
    uint32_t patches_end;                       /* _patches_end of the Cubeboot patch image */
    uint32_t sp, stack_end;                     /* caller's r1; lowest address of the current thread's stack */
    uint32_t allow;                             /* slot mask the physical-device probe allowed (bit 0 = A, bit 1 = B) */
    uint32_t exi_busy;                          /* nonzero: a card channel had a selected device or a running transfer when sampled */
    const struct cbvmc_ipl_symset *sets;
    unsigned nsets;
    uint32_t region_base;                       /* CBVMC_IPL_BASE (a Dolphin test links elsewhere) */
};

enum cbvmc_ipl_refuse {
    CBVMC_IPLR_OK = 0,
    CBVMC_IPLR_MODE_OFF,        /* unused since the vmc_ipl key went (kept for the numbering of the decision file) */
    CBVMC_IPLR_VMC_OFF,         /* vmc = 0 */
    CBVMC_IPLR_BUTTON,          /* the skip button is held */
    CBVMC_IPLR_REV,             /* revision not proven (mode 1) or unknown */
    CBVMC_IPLR_MSR,             /* address translation is off */
    CBVMC_IPLR_BAT,             /* RAM, the I/O window or the trap window are not as the runtime needs */
    CBVMC_IPLR_VECTOR,          /* the DSI vector is not the SDK stub */
    CBVMC_IPLR_SCAN,            /* the scanner refused BS2 (detail = enum cbvmc_refuse) */
    CBVMC_IPLR_SYMSET,          /* no known symbol set matches the code */
    CBVMC_IPLR_SYMREV,          /* the matching set belongs to another revision than get_ipl_revision() says */
    CBVMC_IPLR_SITES,           /* a word to patch lies outside the EXI/CARD block of the matching set (detail = index) */
    CBVMC_IPLR_REGION_XFB,      /* the patch image or the stage-1 framebuffer reaches the region */
    CBVMC_IPLR_REGION_USED,     /* the region is not all zero (detail = first nonzero address) */
    CBVMC_IPLR_STACK,           /* not enough stack for the installer */
    CBVMC_IPLR_SLOTS,           /* both slots hold a physical device */
    CBVMC_IPLR_EXI_BUSY,        /* a card channel was in use when sampled */
    CBVMC_IPLR_COUNT
};

struct cbvmc_ipl_out {
    unsigned refuse;            /* enum cbvmc_ipl_refuse */
    uint32_t detail;
    int best_set;               /* index into sets, -1 none */
    unsigned best_prologues;
    uint32_t blk_lo, blk_hi;    /* EXI/CARD block of the best set */
    struct cbvmc_scan_result scan;      /* valid when the scanner ran; scan.words is the table to patch when refuse == OK */
};

/* Returns CBVMC_IPLR_OK (also in out->refuse) or the first refusal.  out is always filled as far as the checks got. */
unsigned cbvmc_ipl_decide(const struct cbvmc_ipl_in *in, struct cbvmc_ipl_out *out);

const char *cbvmc_ipl_refuse_text(unsigned r);

/* BAT helper (exported for the tests): does the DBAT/IBAT pair-array entry `i` (0..3 of the u/l array starting at bat[0]) cover effective address ea? */
int cbvmc_ipl_bat_covers(uint32_t u, uint32_t l, uint32_t ea);

#endif
