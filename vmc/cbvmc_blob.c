/* Copyright (C) 2026 Nigzel / Bjerreman. SPDX-License-Identifier: GPL-2.0-only */
/*
 * cbvmc_blob: platform glue of the runtime (PowerPC, freestanding).  Original work.
 * The C core is cbvmc_rt.c; this file gives it the real registers, the instruction fetch, the DMA mapping and the state block.
 * Hardware is reached through the game's uncached I/O mapping 0xCC000000 (BAT, not patched, so it never traps).
 */
#include "cbvmc_blob.h"

struct cbvmc_state cbvmc_state __attribute__((aligned(32)));
struct cbvmc_ctx cbvmc_ctx __attribute__((aligned(32)));
/* Handler stack: 2 KB at the top of a block in the installer's reserve (the blob itself has no room for it below 0x80003000).
 * cbvmc_sp is the initial r1 (16-byte aligned, one frame below the top). */
uint32_t cbvmc_sp;

static uint32_t hw_read(void *context, uint32_t off, unsigned size)
{
    volatile uint8_t *p = (volatile uint8_t *)(CBVMC_HW_BASE + off);
    (void)context;
    if (size == 4) return *(volatile uint32_t *)p;
    if (size == 2) return *(volatile uint16_t *)p;
    return *p;
}

static void hw_write(void *context, uint32_t off, unsigned size, uint32_t value)
{
    volatile uint8_t *p = (volatile uint8_t *)(CBVMC_HW_BASE + off);
    (void)context;
    if (size == 4) *(volatile uint32_t *)p = value;
    else if (size == 2) *(volatile uint16_t *)p = (uint16_t)value;
    else *p = (uint8_t)value;
}

static uint32_t hw_fetch(void *context, uint32_t address)
{
    (void)context;
    return *(volatile uint32_t *)address;
}

/* The virtual interrupt is seen by the game at its next real external interrupt (video, audio, pad polling all raise one every
 * few milliseconds).  A dedicated trigger (the console's PI-error read) is a later step; see the design doc. */
static void hw_kick(void *context)
{
    (void)context;
}

/* DMA: the game's physical address through the CACHED mirror (0x80000000), flushed to RAM by dma_done below.  The uncached mirror
 * (0xC0000000) would be the console-natural choice, but Dolphin 2606a with MMU emulation turns a byte store through it into a
 * zero-extended word store (measured, docs/virtual_memory_card.md), which corrupts every sector the model copies
 * byte by byte. */
static void *translate(void *context, uint32_t address, uint32_t length)
{
    (void)context;
    if (address >= 0x01800000u || length > 0x01800000u - address) return (void *)0;
    return (void *)(0x80000000u | address);
}

static void dma_done(void *context, uint32_t phys, uint32_t length)
{
    uint32_t p;
    (void)context;
    for (p = phys & ~31u; p < phys + length; p += 32u)
        __asm__ volatile("dcbf 0,%0" : : "r"(0x80000000u | p) : "memory");
    __asm__ volatile("sync" : : : "memory");
}

int cbvmc_blob_bind(uint8_t *image, uint32_t capacity, const char *path, uint32_t stack_top)
{
    int r;
    if (stack_top & 15u) return -1;
    cbvmc_sp = stack_top - 16u;
    r = cbvmc_ramstore_init(&cbvmc_state.ram, &cbvmc_state.store, image, capacity, path);
    if (r != 0) return r;
    cbvmc_state.magic = CBVMC_STATE_MAGIC;
    cbvmc_state.version = 1;
    cubeboot_vmc_exi_init(&cbvmc_state.rt.exi, &cbvmc_state.port, translate, (void *)0);
    cbvmc_state.rt.hw.context = (void *)0;
    cbvmc_state.rt.hw.read = hw_read;
    cbvmc_state.rt.hw.write = hw_write;
    cbvmc_state.rt.hw.fetch = hw_fetch;
    cbvmc_state.rt.hw.kick = hw_kick;
    cbvmc_state.rt.hw.dma_done = dma_done;
#ifdef CBVMC_RING
    cbvmc_state.rt.ring_on = 1;
#endif
    return 0;
}

int cbvmc_blob_bind_ext(const struct cubeboot_vmc_store *store, void (*tick)(void *), void *tick_ctx, uint32_t stack_top)
{
    if (stack_top & 15u) return -1;
    cbvmc_sp = stack_top - 16u;
    cbvmc_state.store = *store;
    cbvmc_state.magic = CBVMC_STATE_MAGIC;
    cbvmc_state.version = 2;
    cubeboot_vmc_exi_init(&cbvmc_state.rt.exi, &cbvmc_state.port, translate, (void *)0);
    cbvmc_state.rt.hw.context = tick_ctx;
    cbvmc_state.rt.hw.read = hw_read;
    cbvmc_state.rt.hw.write = hw_write;
    cbvmc_state.rt.hw.fetch = hw_fetch;
    cbvmc_state.rt.hw.kick = hw_kick;
    cbvmc_state.rt.hw.dma_done = dma_done;
    cbvmc_state.rt.hw.tick = tick;
    return 0;
}

/* Slot B: the second card's port record lives in the resident image (it is too big for the blob); this routes EXI channel 1 to it. */
int cbvmc_blob_bind_b(struct cubeboot_vmc_port *port)
{
    cubeboot_vmc_exi_init(&cbvmc_state.rt.exi1, port, translate, (void *)0);
    return 0;
}

#ifdef CBVMC_T2
/* T2 (cbvmc_t2.h): the resident's breakpoint handler (set by cbvmc_resident_t2_arm), the repair flag and the physical address of the snapshot the
   real-mode entry restores from.  All three sit in the blob's data, which cbvmc_blob.ld keeps above 0x80001800, outside the rewritten vector ranges. */
void (*cbvmc_t2_fn)(struct cbvmc_ctx *ctx);
uint32_t cbvmc_fix_pending;
uint32_t cbvmc_fix_src;
/* freeze diagnostics (build 3): handler nesting (the handler runs with EE off and the breakpoint cleared, so anything above 1 is a self-trap), and a
   resident hook called every 4096 handled faults, so the counters file keeps updating even if the game spins on EXI without reading PI INTSR */
uint32_t cbvmc_depth, cbvmc_depth_max, cbvmc_faults;
void (*cbvmc_diag_fn)(void);
#endif

/* Called from cbvmc_vec.S in virtual mode with interrupts off.  The result is ignored there: the real-mode entry already
 * checked the window, so 1 cannot happen. */
int cbvmc_dsi_c(struct cbvmc_ctx *ctx)
{
#ifdef CBVMC_T2
    int r = 0;
    if (++cbvmc_depth > cbvmc_depth_max) cbvmc_depth_max = cbvmc_depth;
    if ((ctx->dsisr & 0x00400000u) && (ctx->dar & 0x3FFFFFF8u) == 0x300u && ctx->dar - CBVMC_WIN_BASE >= CBVMC_WIN_SIZE) {
        if (cbvmc_t2_fn) cbvmc_t2_fn(ctx);
        else __asm__ volatile("mtspr 1013,%0" : : "r"(0u));      /* a breakpoint we did not set: clear it, the store re-executes and completes */
    } else {
        r = cbvmc_dsi(&cbvmc_state.rt, ctx);
        if ((++cbvmc_faults & 4095u) == 0 && cbvmc_diag_fn) cbvmc_diag_fn();
    }
    --cbvmc_depth;
    return r;
#else
    return cbvmc_dsi(&cbvmc_state.rt, ctx);
#endif
}
