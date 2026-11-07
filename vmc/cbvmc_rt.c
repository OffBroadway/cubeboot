/* Copyright (C) 2026 Nigzel / Bjerreman. SPDX-License-Identifier: GPL-2.0-only */
/*
 * cbvmc_rt: see cbvmc_rt.h.  Original work.  The instruction decoding follows the PowerPC User Instruction Set Architecture
 * (load/store opcode tables) only; the register semantics of the virtual channel are those of cubeboot_vmc_exi.c.
 *
 * Routing rules for an EXI channel (0 = slot A, 1 = slot B, identical rules) while its port is VIRTUAL (written from the register model, not from Swiss):
 *   CSR   write  model sees it (chip-select edges drive the card); the real CSR gets the same value with CS0 cleared, so a
 *                real device (IPL/RTC CS1, serial CS2) selected later has the clock and interrupt masks the game chose and
 *                the real CS0 never asserts.
 *   CSR   read   model value (EXT, EXIINT, EXTINT and the virtual TCINT come from the model) OR the real TCINT, which is how
 *                the completion of a transfer to a real device reaches the game.
 *   MAR/LENGTH/DATA write   model and real (mirrored, so a real transfer started afterwards is configured).
 *   CR    write  CS0 selected: the model runs the transfer synchronously (TCINT latched, TSTART cleared).  Otherwise the real
 *                CR gets the write (real transfer).
 *   MAR/LENGTH/CR/DATA read   model while CS0 is selected or nothing is selected; real while CS1 or CS2 is selected.
 * Channels 1 and 2 and every other register in the window are forwarded untouched.
 * PI INTSR: the real value, with the EXI summary bit set while the virtual channel's interrupt line (masks applied) is high.
 */
#include "cbvmc_rt.h"

/* Channel 0 = slot A, channel 1 = slot B (second virtual card, optional: exi1.port stays NULL / physical when slot B is not served). */
static struct cubeboot_vmc_exi *chan(struct cbvmc_rt *rt, unsigned c)
{
    return c ? &rt->exi1 : &rt->exi;
}

static int vact(const struct cubeboot_vmc_exi *e)
{
    return e->port != 0 && e->port->source == CUBEBOOT_VMC_SOURCE_VIRTUAL;
}

static uint32_t hw_rd(struct cbvmc_rt *rt, uint32_t off, unsigned size)
{
    return rt->hw.read(rt->hw.context, off, size);
}

static void hw_wr(struct cbvmc_rt *rt, uint32_t off, unsigned size, uint32_t v)
{
    rt->hw.write(rt->hw.context, off, size, v);
}

/* Raise the CPU's external interrupt on a rising edge of a virtual line (either channel). */
static void line_update(struct cbvmc_rt *rt)
{
    unsigned c, now = 0;
    for (c = 0; c < 2; ++c) {
        struct cubeboot_vmc_exi *e = chan(rt, c);
        if (vact(e) && cubeboot_vmc_exi_irq(e)) now |= 1u << c;
    }
    if ((now & ~(unsigned)rt->exi_line_prev) && rt->hw.kick) rt->hw.kick(rt->hw.context);
    rt->exi_line_prev = (uint8_t)now;
}

static uint32_t exi_read(struct cbvmc_rt *rt, unsigned c, uint32_t reg)
{
    struct cubeboot_vmc_exi *e = chan(rt, c);
    uint32_t v = 0, base = CBVMC_EXI_BASE + c * CBVMC_EXI_CH_STRIDE;
    unsigned cs = (e->csr >> CUBEBOOT_VMC_CSR_CS_SHIFT) & 7u;

    if (c) rt->stats.exi_virtual_b++;
    if (reg == CUBEBOOT_VMC_EXI_CSR) {
        cubeboot_vmc_exi_read(e, reg, &v);
        v |= hw_rd(rt, base + reg, 4) & CUBEBOOT_VMC_CSR_TCINT;
        rt->stats.exi_virtual++;
        return v;
    }
    if (cs & 6u) {
        rt->stats.exi_forwarded++;
        return hw_rd(rt, base + reg, 4);
    }
    cubeboot_vmc_exi_read(e, reg, &v);
    rt->stats.exi_virtual++;
    return v;
}

static void exi_write(struct cbvmc_rt *rt, unsigned c, uint32_t reg, uint32_t value)
{
    struct cubeboot_vmc_exi *e = chan(rt, c);
    uint32_t base = CBVMC_EXI_BASE + c * CBVMC_EXI_CH_STRIDE;
    if (c) rt->stats.exi_virtual_b++;
    switch (reg) {
    case CUBEBOOT_VMC_EXI_CSR:
        cubeboot_vmc_exi_write(e, reg, value);
        hw_wr(rt, base + reg, 4, value & ~((1u << CUBEBOOT_VMC_CSR_CS_SHIFT) | CUBEBOOT_VMC_CSR_EXT));   /* EXT is read-only on hardware: the model's "card present" never reaches the real register */
        rt->stats.exi_virtual++;
        break;
    case CUBEBOOT_VMC_EXI_MAR:
    case CUBEBOOT_VMC_EXI_LENGTH:
    case CUBEBOOT_VMC_EXI_DATA:
        cubeboot_vmc_exi_write(e, reg, value);
        hw_wr(rt, base + reg, 4, value);
        rt->stats.exi_virtual++;
        break;
    case CUBEBOOT_VMC_EXI_CR:
        if (cubeboot_vmc_exi_write(e, reg, value) == CUBEBOOT_VMC_EXI_PASS) {
            hw_wr(rt, base + reg, 4, value);
            rt->stats.exi_forwarded++;
        } else {
            rt->stats.exi_virtual++;
            if ((value & (CUBEBOOT_VMC_CR_TSTART | CUBEBOOT_VMC_CR_DMA)) == (CUBEBOOT_VMC_CR_TSTART | CUBEBOOT_VMC_CR_DMA)) {
                rt->stats.dma++;
                if (rt->hw.dma_done) rt->hw.dma_done(rt->hw.context, e->mar, e->length);
            }
        }
        break;
    default:
        hw_wr(rt, base + reg, 4, value);
        rt->stats.exi_forwarded++;
        break;
    }
    line_update(rt);
}

/* The virtual channel an aligned 32-bit access at `off` belongs to, or -1 */
static int vchan(struct cbvmc_rt *rt, uint32_t off, unsigned size)
{
    unsigned c;
    if (size != 4 || (off & 3u) != 0 || off < CBVMC_EXI_BASE) return -1;
    c = (off - CBVMC_EXI_BASE) / CBVMC_EXI_CH_STRIDE;
    if (c > 1u || !vact(chan(rt, c))) return -1;
    return (int)c;
}

uint32_t cbvmc_mmio_read(struct cbvmc_rt *rt, uint32_t off, unsigned size)
{
    int c;
    if (size == 4 && off == CBVMC_PI_INTSR) {
        uint32_t v = hw_rd(rt, off, 4);
        rt->stats.pi_reads++;
        if (rt->hw.tick) rt->hw.tick(rt->hw.context);
        if ((vact(&rt->exi) && cubeboot_vmc_exi_irq(&rt->exi)) || (vact(&rt->exi1) && cubeboot_vmc_exi_irq(&rt->exi1))) {
            v |= CBVMC_PI_INTSR_EXI;
            rt->stats.pi_irq_merged++;
        }
        return v;
    }
    c = vchan(rt, off, size);
    if (c >= 0) return exi_read(rt, (unsigned)c, (off - CBVMC_EXI_BASE) % CBVMC_EXI_CH_STRIDE);
    if (off >= CBVMC_EXI_BASE && off < CBVMC_EXI_END) rt->stats.exi_forwarded++;
    else rt->stats.other_forwarded++;
    return hw_rd(rt, off, size);
}

void cbvmc_mmio_write(struct cbvmc_rt *rt, uint32_t off, unsigned size, uint32_t value)
{
    int c = vchan(rt, off, size);
    if (c >= 0) {
        exi_write(rt, (unsigned)c, (off - CBVMC_EXI_BASE) % CBVMC_EXI_CH_STRIDE, value);
        return;
    }
    if (off >= CBVMC_EXI_BASE && off < CBVMC_EXI_END) rt->stats.exi_forwarded++;
    else rt->stats.other_forwarded++;
    hw_wr(rt, off, size, value);
}

/* ---- load/store decoder --------------------------------------------------------------------------------------- */

int cbvmc_decode(uint32_t insn, struct cbvmc_access *a)
{
    unsigned op = insn >> 26;
    unsigned rs = (insn >> 21) & 31u;
    unsigned ra = (insn >> 16) & 31u;

    a->store = 0; a->size = 0; a->rt = (uint8_t)rs; a->ra_update = 0; a->ra = (uint8_t)ra;
    a->byterev = 0; a->algebraic = 0; a->indexed = 0; a->rb = 0; a->disp = 0;

    if (op >= 32 && op <= 45) {
        static const uint8_t size_of[14] = { 4, 4, 1, 1, 4, 4, 1, 1, 2, 2, 2, 2, 2, 2 };
        unsigned k = op - 32u;
        /* 32 lwz 33 lwzu 34 lbz 35 lbzu 36 stw 37 stwu 38 stb 39 stbu 40 lhz 41 lhzu 42 lha 43 lhau 44 sth 45 sthu */
        a->size = size_of[k];
        a->store = (uint8_t)(k == 4 || k == 5 || k == 6 || k == 7 || k == 12 || k == 13);
        a->ra_update = (uint8_t)(k & 1u);
        a->algebraic = (uint8_t)(k == 10 || k == 11);
        a->disp = (int16_t)(insn & 0xffffu);
        if (a->ra_update && (ra == 0 || (!a->store && ra == rs))) return 0;
        return 1;
    }
    if (op == 31) {
        unsigned xo = (insn >> 1) & 0x3ffu;
        a->indexed = 1;
        a->rb = (uint8_t)((insn >> 11) & 31u);
        switch (xo) {
        case 23:  a->size = 4; break;                                   /* lwzx */
        case 55:  a->size = 4; a->ra_update = 1; break;                 /* lwzux */
        case 87:  a->size = 1; break;                                   /* lbzx */
        case 119: a->size = 1; a->ra_update = 1; break;                 /* lbzux */
        case 279: a->size = 2; break;                                   /* lhzx */
        case 311: a->size = 2; a->ra_update = 1; break;                 /* lhzux */
        case 343: a->size = 2; a->algebraic = 1; break;                 /* lhax */
        case 375: a->size = 2; a->algebraic = 1; a->ra_update = 1; break; /* lhaux */
        case 534: a->size = 4; a->byterev = 1; break;                   /* lwbrx */
        case 790: a->size = 2; a->byterev = 1; break;                   /* lhbrx */
        case 151: a->size = 4; a->store = 1; break;                     /* stwx */
        case 183: a->size = 4; a->store = 1; a->ra_update = 1; break;   /* stwux */
        case 215: a->size = 1; a->store = 1; break;                     /* stbx */
        case 247: a->size = 1; a->store = 1; a->ra_update = 1; break;   /* stbux */
        case 407: a->size = 2; a->store = 1; break;                     /* sthx */
        case 439: a->size = 2; a->store = 1; a->ra_update = 1; break;   /* sthux */
        case 662: a->size = 4; a->store = 1; a->byterev = 1; break;     /* stwbrx */
        case 918: a->size = 2; a->store = 1; a->byterev = 1; break;     /* sthbrx */
        default: return 0;
        }
        if (a->ra_update && (ra == 0 || (!a->store && ra == rs))) return 0;
        return 1;
    }
    return 0;
}

static uint32_t swap(uint32_t v, unsigned size)
{
    if (size == 4)
        return (v << 24) | ((v & 0xff00u) << 8) | ((v >> 8) & 0xff00u) | (v >> 24);
    return ((v & 0xffu) << 8) | ((v >> 8) & 0xffu);
}

int cbvmc_dsi(struct cbvmc_rt *rt, struct cbvmc_ctx *ctx)
{
    struct cbvmc_access a;
    uint32_t off, insn;
#ifdef CBVMC_RING
    struct cbvmc_ring_entry *re;
#endif

    if (ctx->dar - CBVMC_WIN_BASE >= CBVMC_WIN_SIZE) return 1;
    off = ctx->dar - CBVMC_WIN_BASE;
    insn = rt->hw.fetch(rt->hw.context, ctx->srr0);

    if (!cbvmc_decode(insn, &a)) {
        /* A load of an unknown shape reads as a floating bus, a store is dropped: the game goes on, the count tells. */
        rt->stats.bad_insn++;
        rt->stats.last_bad_insn = insn;
        rt->stats.last_bad_pc = ctx->srr0;
        ctx->srr0 += 4;
        return 0;
    }
#ifdef CBVMC_RING
    re = 0;
    if (rt->ring_on && rt->ring_n < CBVMC_RING && ((rt->exi.csr >> 7) & 1u || (a.store && off == CBVMC_EXI_BASE && ((ctx->gpr[a.rt] >> 7) & 1u)))) {
        struct cbvmc_ring_entry *e = re = &rt->ring[rt->ring_n++];
        e->pc = ctx->srr0; e->off = off | (a.store ? 0x80000000u : 0); e->val = a.store ? ctx->gpr[a.rt] : 0; e->insn = insn; e->r3 = ctx->gpr[3]; e->r6 = ctx->gpr[6];
    }
#endif
    if (a.store) {
        uint32_t v = ctx->gpr[a.rt];
        if (a.byterev) v = swap(v, a.size);
        if (a.size == 1) v &= 0xffu;
        else if (a.size == 2) v &= 0xffffu;
        cbvmc_mmio_write(rt, off, a.size, v);
    } else {
        uint32_t v = cbvmc_mmio_read(rt, off, a.size);
        if (a.size == 1) v &= 0xffu;
        else if (a.size == 2) {
            v &= 0xffffu;
            if (a.byterev) v = swap(v, 2);
            if (a.algebraic && (v & 0x8000u)) v |= 0xffff0000u;
        } else if (a.byterev) v = swap(v, 4);
        ctx->gpr[a.rt] = v;
#ifdef CBVMC_RING
        if (re) { re->val = v; re->r6 = ctx->gpr[6]; }
#endif
    }
    if (a.ra_update) ctx->gpr[a.ra] = ctx->dar;
#ifndef CBVMC_FULL_BLOB
    {
        uint32_t k = rt->last_i++ % CBVMC_LAST_N;
        rt->last[k].pc = ctx->srr0; rt->last[k].off = off | (a.store ? 0x80000000u : 0u);
        rt->last[k].val = ctx->gpr[a.rt];         /* the value stored, or the value the load returned */
    }
#endif
    if (rt->stats.traps == 0) { rt->stats.first_pc = ctx->srr0; rt->stats.first_off = off; }
    rt->stats.last_pc = ctx->srr0;
    rt->stats.traps++;
    ctx->srr0 += 4;
    return 0;
}
