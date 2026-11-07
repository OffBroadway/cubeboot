/* Copyright (C) 2026 Nigzel / Bjerreman. SPDX-License-Identifier: GPL-2.0-only */
#ifndef CBVMC_IPL_PROBE_H
#define CBVMC_IPL_PROBE_H

/*
 * G3: read-only IPL (BS2) probe, docs/virtual_memory_card.md.  Nothing here ever writes to memory, a register or a device other
 * than through the `emit` callback; the callback of the Cubeboot build writes /cubeboot/ipl_probe.txt.  Original work, GPL-2.0-only.
 *
 * Split in two so that the logic can be tested on the host: cbvmc_ipl_probe.c (portable: formats the report, runs the scanner over the BS2 image
 * through a byte view) and cbvmc_ipl_probe_dev.c (Cubeboot only: reads the SPRs and registers, links the symbol sets of the 7 known IPL revisions,
 * writes the file).  Both exist only when CBVMC_IPL_PROBE (Cubeboot build) or CBVMC_IPL_PROBE_HOST (host test) is defined: without either
 * flag no code of this probe is in the image.
 */
#include <stdint.h>

#define CBVMC_IPL_PROBE_STAGES 6u       /* 1 started, 2 CPU/low memory, 3 top of MEM1, 4 DI/EXI/threads, 5 BS2 scan, 6 done */

struct cbvmc_ipl_sym {                  /* a symbol of patches/linker/link_*.ld: set = "ntsc12_101", name = "EXILock" */
    const char *set, *name;
    uint32_t addr;
};

struct cbvmc_ipl_env {
    const uint8_t *mem;                 /* byte view of the cached MEM1: mem[0] is address 0x80000000, 0x1800000 bytes; all reads are bounds-checked */
    uint32_t bs2_lo, bs2_hi;            /* scan window of the BS2 text: the device build uses 0x81300000..0x81400000 (BS2 text is below this in every revision, r2/r13 lie at 0x8146.. to 0x814B..) */
    uint32_t ipl_rev;                   /* get_ipl_revision() id of Cubeboot (0 = unknown) */
    uint32_t r2, r13, msr, pvr, hid0, sdr1;
    uint32_t bat[16];                   /* IBAT0U, IBAT0L .. IBAT3L, DBAT0U .. DBAT3L */
    uint32_t di[10];                    /* 0xCC006000.. */
    uint32_t exi[3][5];                 /* CSR, MAR, LENGTH, CR, DATA of channel 0..2 */
    uint32_t pi_cause, pi_mask;         /* 0xCC003000, 0xCC003004 */
    uint32_t patches_end, lowmem_end;   /* end of Cubeboot's patch image and of its low-memory data */
    uint32_t vmc_enabled;
    const struct cbvmc_ipl_sym *syms;   /* the known function addresses of all revisions */
    unsigned nsyms;
    uint64_t (*now)(void);              /* time base ticks, NULL = no timing */
    uint32_t ticks_per_ms;
    /* stage 1..6: text of that stage (NUL-terminated, len bytes); returns 0 when the sink is still good, nonzero = stop (no later stage is produced) */
    int (*emit)(void *ctx, unsigned stage, const char *text, unsigned len);
    void *ctx;
};

/* Runs the stages in order; returns the number of stages emitted.  Never writes memory outside its own buffers; every loop is bounded by a constant. */
unsigned cbvmc_ipl_probe_run(const struct cbvmc_ipl_env *e);

#ifdef CBVMC_IPL_PROBE
/* Cubeboot build: opens /cubeboot/ipl_probe.txt, writes the start marker, snapshots the registers, runs the probe, closes.  Every failure returns silently. */
void cbvmc_ipl_probe_device(void);
#endif

#endif
