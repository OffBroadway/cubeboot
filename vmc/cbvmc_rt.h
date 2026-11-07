/* Copyright (C) 2026 Nigzel / Bjerreman. SPDX-License-Identifier: GPL-2.0-only */
#ifndef CBVMC_RT_H
#define CBVMC_RT_H

/*
 * cubeboot_vmc runtime (docs/virtual_memory_card.md, step 1).  Original work.
 *
 * The game's SDK code is patched (cbvmc_words.h) so that its EXI register block and the PI interrupt-cause register are
 * addressed at 0x0C00xxxx instead of 0xCC00xxxx.  Under the game's BATs that address is unmapped, so every access raises
 * a DSI.  The DSI vector (cbvmc_vec.S) saves the CPU state in a struct cbvmc_ctx and calls cbvmc_dsi(); this file decodes
 * the faulting load or store, emulates it against the virtual EXI channel 0 (the card model in
 * proposals/cubeboot-resident-services) and forwards every other access to the real register at 0xCC00xxxx.
 *
 * Nothing here touches PowerPC state directly: the hardware is reached through struct cbvmc_hw, so the same code is
 * driven on the host by a fake CPU (tests/cbvmc_test.c) and on the console / in Dolphin by cbvmc_blob.c.
 * Only the memory-card channel (EXI channel 0, chip select 0, slot A) is virtual.  Everything else on EXI (IPL / RTC / SRAM
 * on channel 0 CS1, the serial port, channels 1 and 2) goes to the real registers.
 */

#include <stdint.h>

#include "cubeboot_vmc_exi.h"

/* Registers behind the trap window. */
#define CBVMC_WIN_BASE   0x0C000000u        /* trapped base the patched SDK code uses */
#define CBVMC_WIN_SIZE   0x00010000u        /* the whole 64 KiB hardware page; anything else is not ours */
#define CBVMC_HW_BASE    0xCC000000u        /* the real registers (uncached, game BAT) */
#define CBVMC_PI_INTSR   0x3000u
#define CBVMC_PI_INTSR_EXI  (1u << 4)       /* EXI summary bit of PI INTSR */
#define CBVMC_EXI_BASE   0x6800u
#define CBVMC_EXI_CH_STRIDE 0x14u
#define CBVMC_EXI_END    (CBVMC_EXI_BASE + 3u * CBVMC_EXI_CH_STRIDE)

/* The CPU state at the fault.  Layout is shared with cbvmc_vec.S (offsets below). */
struct cbvmc_ctx {
    uint32_t gpr[32];       /* 0x00 */
    uint32_t srr0;          /* 0x80 */
    uint32_t srr1;          /* 0x84 */
    uint32_t dar;           /* 0x88 */
    uint32_t dsisr;         /* 0x8C */
    uint32_t lr;            /* 0x90 */
    uint32_t ctr;           /* 0x94 */
    uint32_t cr;            /* 0x98 */
    uint32_t xer;           /* 0x9C */
};
#define CBVMC_CTX_SRR0  0x80
#define CBVMC_CTX_SRR1  0x84
#define CBVMC_CTX_DAR   0x88
#define CBVMC_CTX_DSISR 0x8C
#define CBVMC_CTX_LR    0x90
#define CBVMC_CTX_CTR   0x94
#define CBVMC_CTX_CR    0x98
#define CBVMC_CTX_XER   0x9C

/* Real hardware, as seen by the handler.  `off` is the offset inside the 64 KiB page (e.g. 0x6800), `size` 1, 2 or 4. */
struct cbvmc_hw {
    void *context;
    uint32_t (*read)(void *context, uint32_t off, unsigned size);
    void (*write)(void *context, uint32_t off, unsigned size, uint32_t value);
    /* Fetch the faulting instruction (an address in the game's cached view). */
    uint32_t (*fetch)(void *context, uint32_t address);
    /* Ask the CPU for an external interrupt (called when the virtual line goes high). Optional. */
    void (*kick)(void *context);
    /* A virtual DMA finished: `phys`/`length` is the game memory the card model wrote or read through the translate hook. The console
     * flushes those cache lines here (the model writes through the cached view). Optional. */
    void (*dma_done)(void *context, uint32_t phys, uint32_t length);
    /* Called on every PI INTSR read (about once per video interrupt): the idle clock of the write-back store (cbvmc_wbstore.h). Optional. */
    void (*tick)(void *context);
};

struct cbvmc_stats {
    uint32_t traps;             /* handled faults */
    uint32_t exi_virtual;       /* accesses answered by the virtual card channel */
    uint32_t exi_forwarded;     /* EXI accesses forwarded to the real registers */
    uint32_t pi_reads;          /* PI INTSR reads (virtual EXI bit merged in) */
    uint32_t pi_irq_merged;     /* ... of which the virtual card raised the EXI bit */
    uint32_t other_forwarded;   /* other registers in the window */
    uint32_t bad_insn;          /* faulting instruction not decodable: load reads 0xFFFFFFFF, store dropped */
    uint32_t last_bad_insn;
    uint32_t last_bad_pc;
    uint32_t dma_bad;           /* DMA address the translate hook refused */
    uint32_t dma;               /* virtual DMA transfers */
    uint32_t exi_virtual_b;     /* ... of which on channel 1 (slot B) */
    uint32_t first_pc;          /* diagnostics: the first handled fault (pc, register offset) */
    uint32_t first_off;
    uint32_t last_pc;           /* ... and the latest */
};

#ifdef CBVMC_RING
/* debug build only: the first CBVMC_RING accesses after ring_on is set (pc, register offset | 0x80000000 for a store, value) */
struct cbvmc_ring_entry { uint32_t pc, off, val, insn, r3, r6; };
#endif

struct cbvmc_rt {
#ifdef CBVMC_RING
    struct cbvmc_ring_entry ring[CBVMC_RING];
    uint32_t ring_n, ring_on;
#endif
    struct cbvmc_hw hw;
    struct cubeboot_vmc_exi exi;        /* virtual channel 0 (slot A) */
    struct cubeboot_vmc_exi exi1;       /* virtual channel 1 (slot B); port NULL = not served, registers go to the real channel */
    uint8_t exi_line_prev;              /* last levels of the virtual interrupt lines, bit per channel (for the rising-edge kick) */
    struct cbvmc_stats stats;
#ifndef CBVMC_FULL_BLOB
    /* freeze diagnostics (T2 build 4): the last CBVMC_LAST_N handled accesses, oldest overwritten: pc, register offset (bit 31 = store), value read or
       written.  The old single-image Dolphin blob (CBVMC_FULL_BLOB) has no room for it; every unit that shares this struct is built with the same setting. */
#define CBVMC_LAST_N 16u
    struct { uint32_t pc, off, val; } last[CBVMC_LAST_N];
    uint32_t last_i;
#endif
};

/*
 * Handles one fault.  Returns 0 and advances ctx->srr0 (and the update register of an update-form access).
 * Returns 1 only if the DAR is outside the window: the caller must give the exception to the OS unchanged.
 * (cbvmc_vec.S also checks the window in real mode before it saves anything, so 1 is a safety net.)
 */
int cbvmc_dsi(struct cbvmc_rt *rt, struct cbvmc_ctx *ctx);

/* Physical card arbitration at runtime (owner rule: a physical card always wins per slot, hot-swap included).  Called from the
   resident's tick (one per PI INTSR read = per real external interrupt; not from the blob, which has no room).  The real EXT line
   of each served channel is sampled from the real CSR.  A card appearing parks the virtual card (source PHYSICAL: the channel is
   forwarded untouched, so the real card's own EXTINT and traffic reach the software; the virtual card's image stays loaded and is
   never written).  A card going away brings the virtual card back (source VIRTUAL) and latches EXTINT like a real insertion.
   Never switches while the virtual card is selected.  Self-contained on purpose: the model's full port_service (which re-validates
   an image through the store) does not fit the resident's handler stack. */
static inline void cbvmc_rt_observe_physical(struct cbvmc_rt *rt)
{
    unsigned c;
    for (c = 0; c < 2; ++c) {
        struct cubeboot_vmc_exi *e = c ? &rt->exi1 : &rt->exi;
        struct cubeboot_vmc_port *p = e->port;
        int present;
        if (!p) continue;
        present = (rt->hw.read(rt->hw.context, CBVMC_EXI_BASE + c * CBVMC_EXI_CH_STRIDE + CUBEBOOT_VMC_EXI_CSR, 4) & CUBEBOOT_VMC_CSR_EXT) != 0;
        if (p->physical_present == present) continue;
        if (p->source == CUBEBOOT_VMC_SOURCE_VIRTUAL && (e->cs0_selected || p->card.selected)) continue;   /* not mid-request: next tick */
        p->physical_present = (uint8_t)present;
        if (present) {
            if (p->source == CUBEBOOT_VMC_SOURCE_VIRTUAL) { p->source = CUBEBOOT_VMC_SOURCE_PHYSICAL; e->cs0_selected = 0; }
        } else if (p->source == CUBEBOOT_VMC_SOURCE_PHYSICAL) {
            p->source = CUBEBOOT_VMC_SOURCE_VIRTUAL;
            e->cs0_selected = 0;
            e->csr = (e->csr & ~(7u << CUBEBOOT_VMC_CSR_CS_SHIFT)) | CUBEBOOT_VMC_CSR_EXTINT;
        }
    }
}

/* Register-level entry points (used by cbvmc_dsi and by tests). */
uint32_t cbvmc_mmio_read(struct cbvmc_rt *rt, uint32_t off, unsigned size);
void cbvmc_mmio_write(struct cbvmc_rt *rt, uint32_t off, unsigned size, uint32_t value);

/* Decoder of one load/store.  Returns 0 if the word is not a supported load/store. */
struct cbvmc_access {
    uint8_t store;          /* 1 = store */
    uint8_t size;           /* 1, 2, 4 */
    uint8_t rt;             /* source or target register */
    uint8_t ra_update;      /* 1: write the effective address back to rA */
    uint8_t ra;
    uint8_t byterev;        /* lwbrx style */
    uint8_t algebraic;      /* lha */
    uint8_t indexed;        /* X form: rB used */
    uint8_t rb;
    int32_t disp;
};
int cbvmc_decode(uint32_t insn, struct cbvmc_access *out);

#endif
