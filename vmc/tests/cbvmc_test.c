/* Copyright (C) 2026 Nigzel / Bjerreman. SPDX-License-Identifier: GPL-2.0-only */
/*
 * Host tests of the cubeboot_vmc runtime (offline/cubeboot_vmc): a fake CPU hands the handler real PowerPC load/store words,
 * fake hardware answers the forwarded accesses, and a small "SDK" drives the card the way the SDK's card library does
 * (probe, ID, status, DMA read, sector erase, 128-byte page programs, interrupts through PI INTSR).  No emulator, no console.
 *
 *   cbvmc_test [dump.raw]     with a path, the card image after the SDK run is written there (checked by a Python reader)
 */
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "cbvmc_rt.h"
#include "cbvmc_ramstore.h"
#include "cbvmc_words.h"

#define CHECK(c) do { if (!(c)) { fprintf(stderr, "%s:%d: CHECK failed: %s\n", __FILE__, __LINE__, #c); abort(); } } while (0)

/* ---- fake hardware ------------------------------------------------------------------------------------------- */

#define FRAM_SIZE 0x40000u
#define HW_LOG_MAX 4096

struct hwlog { uint32_t off; uint32_t value; unsigned size; int write; };

struct fake {
    uint8_t ram[FRAM_SIZE];                 /* the game's RAM, physical addresses */
    uint32_t reg[0x4000];                   /* registers of the 64 KiB page, by word */
    struct hwlog log[HW_LOG_MAX];
    unsigned nlog;
    uint32_t insn;                          /* the instruction at the fault */
    uint32_t insn_pc;
    unsigned kicks;
    uint32_t pi_intsr_real;                 /* what the real PI INTSR holds */
    uint8_t real_ipl[16];                   /* the real device on CS1 */
    uint32_t real_cs1_data;
    unsigned real_transfers;
    unsigned dma_done_calls, dma_done_bytes;
};

static struct fake F;

static uint32_t f_read(void *c, uint32_t off, unsigned size)
{
    struct fake *f = c;
    uint32_t v;
    if (f->nlog < HW_LOG_MAX) { f->log[f->nlog].off = off; f->log[f->nlog].size = size; f->log[f->nlog].write = 0; }
    if (off == CBVMC_PI_INTSR) v = f->pi_intsr_real;
    else v = f->reg[off >> 2];
    if (f->nlog < HW_LOG_MAX) f->log[f->nlog++].value = v;
    return v;
}

static void f_write(void *c, uint32_t off, unsigned size, uint32_t value)
{
    struct fake *f = c;
    if (f->nlog < HW_LOG_MAX) { f->log[f->nlog].off = off; f->log[f->nlog].size = size; f->log[f->nlog].write = 1; f->log[f->nlog++].value = value; }
    if (off >= CBVMC_EXI_BASE && off < CBVMC_EXI_BASE + 0x14 && ((off - CBVMC_EXI_BASE) == CUBEBOOT_VMC_EXI_CSR)) {
        /* write-1-to-clear TCINT / EXTINT, keep the rest */
        uint32_t keep = f->reg[off >> 2] & (CUBEBOOT_VMC_CSR_TCINT | CUBEBOOT_VMC_CSR_EXTINT | CUBEBOOT_VMC_CSR_EXT);   /* EXT: read-only, the slot's state */
        keep &= ~(value & (CUBEBOOT_VMC_CSR_TCINT | CUBEBOOT_VMC_CSR_EXTINT));
        f->reg[off >> 2] = (value & ~(CUBEBOOT_VMC_CSR_TCINT | CUBEBOOT_VMC_CSR_EXTINT | CUBEBOOT_VMC_CSR_EXT)) | keep;
        return;
    }
    if (off == CBVMC_EXI_BASE + CUBEBOOT_VMC_EXI_CR && (value & 1u)) {
        /* a real transfer: only CS1 (IPL-like) has a device here */
        uint32_t csr = f->reg[(CBVMC_EXI_BASE + CUBEBOOT_VMC_EXI_CSR) >> 2];
        unsigned cs = (csr >> 7) & 7u;
        f->real_transfers++;
        if (cs == 2u) {
            if (value & 2u) {                       /* DMA: copy the pattern into RAM */
                uint32_t mar = f->reg[(CBVMC_EXI_BASE + CUBEBOOT_VMC_EXI_MAR) >> 2];
                uint32_t len = f->reg[(CBVMC_EXI_BASE + CUBEBOOT_VMC_EXI_LENGTH) >> 2];
                uint32_t i;
                for (i = 0; i < len; ++i) f->ram[mar + i] = (uint8_t)(0xA0u + i);
            } else {
                f->reg[(CBVMC_EXI_BASE + CUBEBOOT_VMC_EXI_DATA) >> 2] = 0xC0DEF00Du;
            }
        }
        f->reg[(CBVMC_EXI_BASE + CUBEBOOT_VMC_EXI_CSR) >> 2] |= CUBEBOOT_VMC_CSR_TCINT;
        f->reg[(CBVMC_EXI_BASE + CUBEBOOT_VMC_EXI_CR) >> 2] = value & ~1u;
        return;
    }
    f->reg[off >> 2] = value;
}

static uint32_t f_fetch(void *c, uint32_t address)
{
    struct fake *f = c;
    CHECK(address == f->insn_pc);
    return f->insn;
}

static void f_kick(void *c) { ((struct fake *)c)->kicks++; }

static void f_dma_done(void *c, uint32_t phys, uint32_t length)
{
    struct fake *f = c;
    f->dma_done_calls++;
    f->dma_done_bytes += length;
    (void)phys;
}

static void *f_translate(void *c, uint32_t address, uint32_t length)
{
    struct fake *f = c;
    if (address > FRAM_SIZE || length > FRAM_SIZE - address) return NULL;
    return f->ram + address;
}

/* ---- the unit under test, wired up like the blob does it ------------------------------------------------------ */

static struct cbvmc_rt RT;
static struct cubeboot_vmc_store STORE;
static struct cbvmc_ramstore RS;
static struct cubeboot_vmc_port PORT;
static void f_tick(void *c) { (void)c; cbvmc_rt_observe_physical(&RT); }
static uint8_t IMAGE[0x80000];
#define CARD_PATH "/swiss/saves/MemoryCardA.USA.raw"

static void setup(unsigned mbits, int format)
{
    struct cubeboot_vmc_identity id = { 0x1122334455667788ull, 0x99aabbccddeeff00ull };
    struct cubeboot_vmc_geometry g = { (uint16_t)mbits, CUBEBOOT_VMC_ENC_WINDOWS_1252 };
    memset(&F, 0, sizeof(F));
    memset(IMAGE, 0x5a, sizeof(IMAGE));
    memset(&RT, 0, sizeof(RT));
    CHECK(cbvmc_ramstore_init(&RS, &STORE, IMAGE, sizeof(IMAGE), CARD_PATH) == 0);
    CHECK(cubeboot_vmc_port_init(&PORT, &STORE, CUBEBOOT_VMC_SLOT_A, CARD_PATH, &id, &g) == 0);
    if (format) {
        CHECK(cubeboot_vmc_port_service(&PORT) == CUBEBOOT_VMC_EVENT_VIRTUAL_INSERTED);
        CHECK(PORT.source == CUBEBOOT_VMC_SOURCE_VIRTUAL);
    }
    cubeboot_vmc_exi_init(&RT.exi, &PORT, f_translate, &F);
    RT.hw.context = &F;
    RT.hw.read = f_read;
    RT.hw.write = f_write;
    RT.hw.fetch = f_fetch;
    RT.hw.kick = f_kick;
    RT.hw.tick = f_tick;
    RT.hw.dma_done = f_dma_done;
}

/* ---- the fake CPU: one load or store through the DSI path ----------------------------------------------------- */

static uint32_t D(unsigned op, unsigned rt, unsigned ra, int d) { return (op << 26) | (rt << 21) | (ra << 16) | ((unsigned)d & 0xffffu); }
static uint32_t X(unsigned xo, unsigned rt, unsigned ra, unsigned rb) { return (31u << 26) | (rt << 21) | (ra << 16) | (rb << 11) | (xo << 1); }

static void fault(uint32_t insn, uint32_t dar, struct cbvmc_ctx *c)
{
    F.insn = insn;
    F.insn_pc = 0x80123450u;
    c->srr0 = F.insn_pc;
    c->dar = dar;
    CHECK(cbvmc_dsi(&RT, c) == 0);
    CHECK(c->srr0 == F.insn_pc + 4);
}

static uint32_t ld32(uint32_t reg_off)
{
    struct cbvmc_ctx c;
    memset(&c, 0, sizeof(c));
    c.gpr[5] = 0xdeadbeefu;
    fault(D(32, 5, 4, 0), CBVMC_WIN_BASE + reg_off, &c);        /* lwz r5,0(r4) */
    return c.gpr[5];
}

static void st32(uint32_t reg_off, uint32_t v)
{
    struct cbvmc_ctx c;
    memset(&c, 0, sizeof(c));
    c.gpr[5] = v;
    fault(D(36, 5, 4, 0), CBVMC_WIN_BASE + reg_off, &c);        /* stw r5,0(r4) */
}

/* ---- the SDK: EXI helpers the card library uses ---------------------------------------------------------------- */

#define CH0 CBVMC_EXI_BASE
#define CSR (CH0 + CUBEBOOT_VMC_EXI_CSR)
#define MAR (CH0 + CUBEBOOT_VMC_EXI_MAR)
#define LEN (CH0 + CUBEBOOT_VMC_EXI_LENGTH)
#define CRR (CH0 + CUBEBOOT_VMC_EXI_CR)
#define DAT (CH0 + CUBEBOOT_VMC_EXI_DATA)

static void sel(unsigned dev, unsigned freq)
{
    /* EXISelect: keep the interrupt masks, set clock and chip select */
    uint32_t v = ld32(CSR);
    v = (v & (CUBEBOOT_VMC_CSR_EXIINTMASK | CUBEBOOT_VMC_CSR_TCINTMASK | CUBEBOOT_VMC_CSR_EXTINTMASK)) |
        (freq << CUBEBOOT_VMC_CSR_CLK_SHIFT) | ((1u << dev) << CUBEBOOT_VMC_CSR_CS_SHIFT);
    st32(CSR, v);
}

static void desel(void)
{
    uint32_t v = ld32(CSR);
    v &= (CUBEBOOT_VMC_CSR_EXIINTMASK | CUBEBOOT_VMC_CSR_TCINTMASK | CUBEBOOT_VMC_CSR_EXTINTMASK);
    st32(CSR, v);
}

/* EXIImm write/read of 1..4 bytes: data left aligned, returns the data register after the transfer */
static uint32_t imm(int write, uint32_t data, unsigned bytes)
{
    uint32_t cr;
    st32(DAT, data);
    cr = 1u | (((unsigned)(write ? 1 : 0)) << CUBEBOOT_VMC_CR_RW_SHIFT) | ((bytes - 1u) << CUBEBOOT_VMC_CR_TLEN_SHIFT);
    if (!write) cr = 1u | (((bytes - 1u)) << CUBEBOOT_VMC_CR_TLEN_SHIFT);
    st32(CRR, cr);
    CHECK((ld32(CRR) & 1u) == 0);                               /* EXISync: TSTART clear */
    return ld32(DAT);
}

static void dma(uint32_t addr, uint32_t len, int write)
{
    st32(MAR, addr);
    st32(LEN, len);
    st32(CRR, 1u | 2u | ((write ? 1u : 0u) << CUBEBOOT_VMC_CR_RW_SHIFT));
    CHECK((ld32(CRR) & 1u) == 0);
}

static void clear_tc(void)
{
    uint32_t v = ld32(CSR);
    st32(CSR, v | CUBEBOOT_VMC_CSR_TCINT);
}

/* card command helpers (frequency 4 = 16 MHz as the SDK uses for the card) */
static uint32_t card_status(void)
{
    uint32_t r;
    sel(0, 4);
    imm(1, 0x83000000u, 2);
    r = imm(0, 0, 1);
    desel();
    return r >> 24;
}

static void card_clear_status(void)
{
    sel(0, 4);
    imm(1, 0x89000000u, 1);
    desel();
}

static void card_read_sector(uint32_t card_off, uint32_t ram, uint32_t len)
{
    uint32_t a = 0x52000000u | ((card_off >> 9) & 0xFFu) << 16;
    /* 0x52 + 4 address bytes as the SDK sends them: A31..A25 (7 bits) etc. */
    uint32_t addr = 0;
    addr |= ((card_off >> 17) & 0xffu) << 24;
    addr |= ((card_off >> 9) & 0xffu) << 16;
    addr |= ((card_off >> 7) & 0x3u) << 8;
    addr |= (card_off & 0x7fu);
    (void)a;
    sel(0, 4);
    imm(1, 0x52000000u | (addr >> 8), 4);       /* cmd + first 3 address bytes */
    imm(1, (addr & 0xffu) << 24, 1);            /* last address byte */
    imm(1, 0, 4);                               /* 4 dummy bytes */
    dma(ram, len, 0);
    desel();
    clear_tc();
}

static void card_sector_erase(uint32_t card_off)
{
    uint32_t addr = ((card_off >> 17) & 0xffu) << 16 | ((card_off >> 9) & 0xffu) << 8;
    sel(0, 4);
    imm(1, 0xf1000000u | addr, 3);
    desel();
    clear_tc();
}

static void card_page_program(uint32_t card_off, uint32_t ram, uint32_t len)
{
    uint32_t addr = 0;
    addr |= ((card_off >> 17) & 0xffu) << 24;
    addr |= ((card_off >> 9) & 0xffu) << 16;
    addr |= ((card_off >> 7) & 0x3u) << 8;
    addr |= (card_off & 0x7fu);
    sel(0, 4);
    imm(1, 0xf2000000u | (addr >> 8), 4);
    imm(1, (addr & 0xffu) << 24, 1);
    dma(ram, len, 1);
    desel();
    clear_tc();
}

/* ---- tests ----------------------------------------------------------------------------------------------------- */

static void test_decode(void)
{
    struct cbvmc_access a;
    CHECK(cbvmc_decode(D(32, 3, 4, 0x10), &a) && !a.store && a.size == 4 && a.rt == 3 && a.ra == 4 && a.disp == 0x10);
    CHECK(cbvmc_decode(D(36, 3, 4, -8), &a) && a.store && a.size == 4 && a.disp == -8);
    CHECK(cbvmc_decode(D(37, 3, 4, 8), &a) && a.store && a.ra_update);                 /* stwu */
    CHECK(!cbvmc_decode(D(37, 3, 0, 8), &a));                                          /* stwu with rA = 0 is invalid */
    CHECK(!cbvmc_decode(D(33, 4, 4, 8), &a));                                          /* lwzu with rA = rD is invalid */
    CHECK(cbvmc_decode(D(34, 3, 4, 1), &a) && a.size == 1 && !a.store);
    CHECK(cbvmc_decode(D(42, 3, 4, 2), &a) && a.size == 2 && a.algebraic);
    CHECK(cbvmc_decode(D(44, 3, 4, 2), &a) && a.size == 2 && a.store);
    CHECK(cbvmc_decode(X(23, 3, 4, 5), &a) && a.indexed && a.rb == 5 && a.size == 4);
    CHECK(cbvmc_decode(X(151, 3, 4, 5), &a) && a.store && a.indexed);
    CHECK(cbvmc_decode(X(407, 3, 4, 5), &a) && a.store && a.size == 2);               /* sthx (Swiss' VI case) */
    CHECK(cbvmc_decode(X(534, 3, 4, 5), &a) && a.byterev);
    CHECK(!cbvmc_decode(X(86, 0, 4, 5), &a));                                          /* dcbf: not a data access we model */
    CHECK(!cbvmc_decode(D(46, 3, 4, 0), &a));                                          /* lmw */
    CHECK(!cbvmc_decode(0x60000000u, &a));                                             /* nop */
}

static void test_access_shapes(void)
{
    struct cbvmc_ctx c;
    setup(4, 1);
    /* every supported shape against a plain forwarded register (SI poll at 0x6400: not EXI, not PI) */
    F.reg[0x6400 >> 2] = 0x11223344u;
    memset(&c, 0, sizeof(c));
    fault(D(32, 5, 4, 0), CBVMC_WIN_BASE + 0x6400, &c);                                 /* lwz */
    CHECK(c.gpr[5] == 0x11223344u);
    memset(&c, 0, sizeof(c)); c.gpr[3] = 0xffffffffu;
    fault(D(33, 5, 3, 0), CBVMC_WIN_BASE + 0x6400, &c);                                 /* lwzu: rA gets the EA */
    CHECK(c.gpr[5] == 0x11223344u && c.gpr[3] == CBVMC_WIN_BASE + 0x6400);
    memset(&c, 0, sizeof(c));
    fault(D(34, 5, 4, 0), CBVMC_WIN_BASE + 0x6400, &c);                                 /* lbz reads the register as a byte */
    CHECK(c.gpr[5] == 0x44u);
    memset(&c, 0, sizeof(c)); c.gpr[5] = 0xAABBCCDDu;
    fault(X(534, 5, 4, 6), CBVMC_WIN_BASE + 0x6400, &c);                                /* lwbrx */
    CHECK(c.gpr[5] == 0x44332211u);
    memset(&c, 0, sizeof(c)); c.gpr[5] = 0x01020304u;
    fault(X(151, 5, 4, 6), CBVMC_WIN_BASE + 0x6404, &c);                                /* stwx */
    CHECK(F.reg[0x6404 >> 2] == 0x01020304u);
    memset(&c, 0, sizeof(c)); c.gpr[5] = 0x01020304u; c.gpr[7] = 0;
    fault(D(37, 5, 7, 8), CBVMC_WIN_BASE + 0x6408, &c);                                 /* stwu */
    CHECK(F.reg[0x6408 >> 2] == 0x01020304u && c.gpr[7] == CBVMC_WIN_BASE + 0x6408);
    memset(&c, 0, sizeof(c)); c.gpr[5] = 0x12345678u;
    fault(X(407, 5, 4, 6), CBVMC_WIN_BASE + 0x2002, &c);                                /* sthx: VI style 16-bit store */
    CHECK(F.nlog > 0 && F.log[F.nlog - 1].size == 2 && F.log[F.nlog - 1].value == 0x5678u);
    memset(&c, 0, sizeof(c));
    F.reg[0x6410 >> 2] = 0x00008123u;
    fault(D(42, 5, 4, 0), CBVMC_WIN_BASE + 0x6410, &c);                                 /* lha sign-extends */
    CHECK(c.gpr[5] == 0xffff8123u);

    /* a store of an unsupported shape is dropped, a load floats; the game continues */
    memset(&c, 0, sizeof(c)); c.srr0 = 0x80123450u;
    F.insn = D(46, 5, 4, 0); F.insn_pc = c.srr0; c.dar = CBVMC_WIN_BASE + 0x6400;
    CHECK(cbvmc_dsi(&RT, &c) == 0 && c.srr0 == 0x80123454u);
    CHECK(RT.stats.bad_insn == 1 && RT.stats.last_bad_insn == D(46, 5, 4, 0));

    /* a DAR outside the window is not ours */
    memset(&c, 0, sizeof(c)); c.dar = 0x0D000000u; c.srr0 = 0x80123450u;
    CHECK(cbvmc_dsi(&RT, &c) == 1 && c.srr0 == 0x80123450u);
    c.dar = 0xCC006800u;
    CHECK(cbvmc_dsi(&RT, &c) == 1);
}

static void test_card_basic(void)
{
    uint32_t v;
    setup(4, 1);
    /* the card is "inserted": EXT set, nothing else */
    v = ld32(CSR);
    CHECK(v & CUBEBOOT_VMC_CSR_EXT);
    CHECK(!(v & (CUBEBOOT_VMC_CSR_EXTINT | CUBEBOOT_VMC_CSR_TCINT)));

    /* ID command 0x00: the reply is 0x80 then the Mbit count (4) as 4-byte repeats... as the model defines it */
    sel(0, 4);
    imm(1, 0x00000000u, 2);                      /* command 0x00 + 1 dummy byte */
    v = imm(0, 0, 4);
    desel();
    CHECK(v == 0x00000004u);                     /* 4 Mbit: 00 00 00 04 */
    CHECK(card_status() == (CUBEBOOT_VMC_ST_UNLOCKED | CUBEBOOT_VMC_ST_READY));
    /* a CS1 transfer goes to the real device and does not disturb the card */
    {
        unsigned before = F.real_transfers;
        sel(1, 3);
        imm(1, 0x01020304u, 4);                  /* real immediate transfer */
        CHECK(F.real_transfers == before + 1);
        CHECK(ld32(DAT) == 0xC0DEF00Du);         /* read from the real DATA register */
        desel();
        clear_tc();
        CHECK(card_status() == (CUBEBOOT_VMC_ST_UNLOCKED | CUBEBOOT_VMC_ST_READY));
    }
    /* real DMA on CS1 reaches the game RAM through the real controller, not the card */
    sel(1, 3);
    dma(0x1000, 32, 0);
    CHECK(F.ram[0x1000] == 0xA0 && F.ram[0x101f] == 0xBF);
    desel();
    clear_tc();
    /* channel 1 and channel 2 are plain forwards */
    st32(CBVMC_EXI_BASE + 0x14, 0x12345678u);
    CHECK(F.reg[(CBVMC_EXI_BASE + 0x14) >> 2] == 0x12345678u);
    CHECK(ld32(CBVMC_EXI_BASE + 0x14) == 0x12345678u);
    st32(CBVMC_EXI_BASE + 0x28, 0x7);
    CHECK(F.reg[(CBVMC_EXI_BASE + 0x28) >> 2] == 0x7u);
}

static void test_interrupts(void)
{
    uint32_t v;
    setup(4, 1);
    /* enable the card interrupt (0x81 01), the SDK's __CARDEnableInterrupt; EXI interrupt mask on in the CSR */
    st32(CSR, CUBEBOOT_VMC_CSR_EXIINTMASK);
    sel(0, 4);
    imm(1, 0x81010000u, 2);
    desel();
    CHECK(!(ld32(CBVMC_PI_INTSR) & CBVMC_PI_INTSR_EXI));
    /* a sector erase completes and raises the card interrupt: the line goes up (kick) and shows in PI INTSR */
    F.kicks = 0;
    card_sector_erase(5u * 8192u);
    v = ld32(CSR);
    CHECK(v & CUBEBOOT_VMC_CSR_EXIINT);
    CHECK(ld32(CBVMC_PI_INTSR) & CBVMC_PI_INTSR_EXI);
    CHECK(RT.stats.pi_irq_merged >= 1);
    CHECK(F.kicks >= 1);
    {   /* the real INTSR bits are kept */
        F.pi_intsr_real = 0x0120u;
        CHECK((ld32(CBVMC_PI_INTSR) & 0x0120u) == 0x0120u);
        F.pi_intsr_real = 0;
    }
    /* 0x89 clears the status and with it the card interrupt */
    card_clear_status();
    CHECK(!(ld32(CSR) & CUBEBOOT_VMC_CSR_EXIINT));
    CHECK(!(ld32(CBVMC_PI_INTSR) & CBVMC_PI_INTSR_EXI));
    /* TCINT with its mask is a line too, cleared by the write-1-to-clear */
    st32(CSR, CUBEBOOT_VMC_CSR_TCINTMASK);
    sel(0, 4);
    imm(1, 0x83000000u, 2);
    CHECK(ld32(CSR) & CUBEBOOT_VMC_CSR_TCINT);
    CHECK(ld32(CBVMC_PI_INTSR) & CBVMC_PI_INTSR_EXI);
    clear_tc();
    CHECK(!(ld32(CSR) & CUBEBOOT_VMC_CSR_TCINT));
    CHECK(!(ld32(CBVMC_PI_INTSR) & CBVMC_PI_INTSR_EXI));
    desel();
    /* masks off: no line */
    st32(CSR, 0);
    sel(0, 4);
    imm(1, 0x83000000u, 2);
    CHECK(!(ld32(CBVMC_PI_INTSR) & CBVMC_PI_INTSR_EXI));
    desel();
}

/* a physical card appears in the slot while the virtual card is live: at the next PI INTSR read (the idle clock) the virtual card is parked and the
   channel is forwarded untouched; when it goes away the virtual card comes back with the hot-plug EXTINT latched (owner rule: physical wins per slot) */
static void test_hot_swap(void)
{
    uint32_t v;
    setup(4, 1);
    CHECK(PORT.source == CUBEBOOT_VMC_SOURCE_VIRTUAL);
    CHECK(ld32(CSR) & CUBEBOOT_VMC_CSR_EXT);                                  /* virtual card: EXT from the model */
    F.reg[CSR >> 2] = CUBEBOOT_VMC_CSR_EXT | 0x12345000u;                     /* a real card answers on the real CSR */
    (void)ld32(CBVMC_PI_INTSR);
    CHECK(PORT.source == CUBEBOOT_VMC_SOURCE_PHYSICAL);
    v = ld32(CSR);
    CHECK(v == (CUBEBOOT_VMC_CSR_EXT | 0x12345000u));                         /* the real register now: forwarded, not the model */
    F.reg[CSR >> 2] = 0;                                                      /* the real card is pulled */
    F.kicks = 0;
    (void)ld32(CBVMC_PI_INTSR);
    CHECK(PORT.source == CUBEBOOT_VMC_SOURCE_VIRTUAL);
    v = ld32(CSR);
    CHECK((v & CUBEBOOT_VMC_CSR_EXT) && (v & CUBEBOOT_VMC_CSR_EXTINT));       /* back, announced like a real insertion */
    /* never mid-request: with the virtual card selected a real card is noticed only after the deselect */
    sel(0, 4);
    F.reg[CSR >> 2] = CUBEBOOT_VMC_CSR_EXT;
    (void)ld32(CBVMC_PI_INTSR);
    CHECK(PORT.source == CUBEBOOT_VMC_SOURCE_VIRTUAL);
    desel();
    (void)ld32(CBVMC_PI_INTSR);
    CHECK(PORT.source == CUBEBOOT_VMC_SOURCE_PHYSICAL);
    F.reg[CSR >> 2] = 0;
    (void)ld32(CBVMC_PI_INTSR);
    CHECK(PORT.source == CUBEBOOT_VMC_SOURCE_VIRTUAL);
}


static void test_card_save_cycle(const char *dump)
{
    unsigned i, p;
    uint32_t block = 7u * 8192u;
    uint8_t pat[8192];
    setup(4, 1);
    for (i = 0; i < sizeof(pat); ++i) pat[i] = (uint8_t)(i * 7u + 3u);

    /* mount-style reads: the header and the directory blocks into game RAM */
    card_read_sector(0, 0x2000, 512);
    CHECK(memcmp(F.ram + 0x2000, IMAGE, 512) == 0);
    card_read_sector(1u * 8192u, 0x3000, 512);
    CHECK(memcmp(F.ram + 0x3000, IMAGE + 8192, 512) == 0);

    /* a save: erase the block, then 64 page programs of 128 bytes each (the SDK's __CARDWritePage) */
    card_sector_erase(block);
    for (i = 0; i < 8192; i += 512) {            /* erase is complete: reads give 0xFF */
        card_read_sector(block + i, 0x4000, 512);
        CHECK(F.ram[0x4000] == 0xff && F.ram[0x41ff] == 0xff);
    }
    memcpy(F.ram + 0x8000, pat, sizeof(pat));
    for (p = 0; p < 8192; p += 128) card_page_program(block + p, 0x8000 + p, 128);
    CHECK(memcmp(IMAGE + block, pat, sizeof(pat)) == 0);
    CHECK((card_status() & (CUBEBOOT_VMC_ST_ERASE_ERR | CUBEBOOT_VMC_ST_PROGRAM_ERR)) == 0);
    /* read it back through the register path, 512 bytes at a time */
    memset(F.ram + 0x10000, 0, sizeof(pat));
    for (i = 0; i < 8192; i += 512) card_read_sector(block + i, 0x10000 + i, 512);
    CHECK(memcmp(F.ram + 0x10000, pat, sizeof(pat)) == 0);
    CHECK(RS.writes > 64);
    CHECK(F.dma_done_calls >= 64 + 16 + 8 && RT.stats.dma == F.dma_done_calls);     /* every virtual DMA reported once */
    if (dump) {
        FILE *f = fopen(dump, "wb");
        CHECK(f != NULL);
        CHECK(fwrite(IMAGE, 1, 0x80000, f) == 0x80000);
        fclose(f);
    }
}

static void test_ramstore(void)
{
    struct cubeboot_vmc_store s;
    struct cbvmc_ramstore rs;
    uint8_t buf[16];
    uint32_t n;
    char tmp[CUBEBOOT_VMC_PATH_MAX];
    memset(IMAGE, 0, sizeof(IMAGE));
    CHECK(cbvmc_ramstore_init(&rs, &s, IMAGE, 0x1000, "/a") == 0);
    CHECK(s.size(&rs, "/a", &n) == CUBEBOOT_VMC_ERR_MISSING);
    CHECK(s.size(&rs, "/other", &n) == CUBEBOOT_VMC_ERR_MISSING);
    strcpy(tmp, "/a.tmp");
    CHECK(s.create(&rs, tmp) == 0);
    CHECK(s.create(&rs, tmp) == CUBEBOOT_VMC_ERR_EXISTS);
    CHECK(s.create(&rs, "/a") == CUBEBOOT_VMC_ERR_EXISTS);          /* one buffer */
    CHECK(s.write(&rs, tmp, 4, "xx", 2) != 0);                      /* a hole is refused */
    CHECK(s.write(&rs, tmp, 0, "0123456789abcdef", 16) == 0);
    CHECK(s.write(&rs, tmp, 16, "!", 1) == 0);
    CHECK(s.size(&rs, tmp, &n) == 0 && n == 17);
    CHECK(s.read(&rs, tmp, 10, buf, 8) != 0);                       /* past the end */
    CHECK(s.read(&rs, tmp, 0, buf, 16) == 0 && memcmp(buf, "0123456789abcdef", 16) == 0);
    CHECK(s.rename(&rs, "/a", tmp) != 0);                           /* only tmp -> final */
    CHECK(s.rename(&rs, tmp, "/a") == 0);
    CHECK(s.size(&rs, "/a", &n) == 0 && n == 17);
    CHECK(s.size(&rs, tmp, &n) == CUBEBOOT_VMC_ERR_MISSING);
    CHECK(s.write(&rs, "/a", 0x1000, "x", 1) != 0);                 /* capacity */
    CHECK(s.remove(&rs, "/a") == 0);
    CHECK(s.size(&rs, "/a", &n) == CUBEBOOT_VMC_ERR_MISSING);
    CHECK(cbvmc_ramstore_init(&rs, &s, IMAGE, 0x1000, "/this/path/is/much/too/long/to/fit/in/the/model/path/buffer/at/all/really/yes/it/is/long/and/then/some/more/characters") != 0);

    /* formatting through the model: 4 Mbit in the 512 KiB buffer, valid, with the right free-block count */
    setup(4, 1);
    CHECK(cubeboot_vmc_validate(&STORE, CARD_PATH) == 0);
    CHECK(cubeboot_vmc_free_blocks(&STORE, CARD_PATH) == 59);
    CHECK(PORT.card.image_bytes == 0x80000u && PORT.card.geometry.mbits == 4);
    CHECK(PORT.card.geometry.encoding == CUBEBOOT_VMC_ENC_WINDOWS_1252);
}


/* ---- slot B: EXI channel 1 routed to a second virtual card ------------------------------------------------------ */
static uint8_t IMAGE_B[0x100000];
static struct cbvmc_ramstore RS_B;
static struct cubeboot_vmc_store STORE_B;
static struct cubeboot_vmc_port PORT_B;
#define CH1 (CBVMC_EXI_BASE + CBVMC_EXI_CH_STRIDE)
static uint32_t r1(uint32_t reg) { return cbvmc_mmio_read(&RT, CH1 + reg, 4); }
static void w1(uint32_t reg, uint32_t v) { cbvmc_mmio_write(&RT, CH1 + reg, 4, v); }
static uint32_t id_ch1(void)
{
    uint32_t v;
    w1(CUBEBOOT_VMC_EXI_CSR, (r1(CUBEBOOT_VMC_EXI_CSR) & (CUBEBOOT_VMC_CSR_EXIINTMASK | CUBEBOOT_VMC_CSR_TCINTMASK | CUBEBOOT_VMC_CSR_EXTINTMASK)) | (4u << CUBEBOOT_VMC_CSR_CLK_SHIFT) | (1u << CUBEBOOT_VMC_CSR_CS_SHIFT));
    w1(CUBEBOOT_VMC_EXI_DATA, 0);
    w1(CUBEBOOT_VMC_EXI_CR, 1u | (1u << CUBEBOOT_VMC_CR_RW_SHIFT) | (1u << CUBEBOOT_VMC_CR_TLEN_SHIFT));
    CHECK((r1(CUBEBOOT_VMC_EXI_CR) & 1u) == 0);
    w1(CUBEBOOT_VMC_EXI_CR, 1u | (3u << CUBEBOOT_VMC_CR_TLEN_SHIFT));
    CHECK((r1(CUBEBOOT_VMC_EXI_CR) & 1u) == 0);
    v = r1(CUBEBOOT_VMC_EXI_DATA);
    w1(CUBEBOOT_VMC_EXI_CSR, r1(CUBEBOOT_VMC_EXI_CSR) & (CUBEBOOT_VMC_CSR_EXIINTMASK | CUBEBOOT_VMC_CSR_TCINTMASK | CUBEBOOT_VMC_CSR_EXTINTMASK));
    return v;
}

static void test_slot_b(void)
{
    struct cubeboot_vmc_identity id = { 0xAABBCCDDEEFF0011ull, 0x2233445566778899ull };
    struct cubeboot_vmc_geometry g = { 8, CUBEBOOT_VMC_ENC_WINDOWS_1252 };
    uint32_t fwd;
    setup(4, 1);                                                      /* slot A: 4 Mbit */
    /* slot B not served yet: channel 1 goes to the real registers (an empty slot there) */
    F.reg[(CH1 + CUBEBOOT_VMC_EXI_CSR) >> 2] = 0x1234u;
    fwd = RT.stats.exi_forwarded;
    CHECK(r1(CUBEBOOT_VMC_EXI_CSR) == 0x1234u);
    CHECK(RT.stats.exi_forwarded == fwd + 1);
    /* B served: its own card model, its own registers */
    memset(IMAGE_B, 0x5a, sizeof(IMAGE_B));
    CHECK(cbvmc_ramstore_init(&RS_B, &STORE_B, IMAGE_B, sizeof(IMAGE_B), "/swiss/saves/MemoryCardB.USA.raw") == 0);
    CHECK(cubeboot_vmc_port_init(&PORT_B, &STORE_B, CUBEBOOT_VMC_SLOT_B, "/swiss/saves/MemoryCardB.USA.raw", &id, &g) == 0);
    CHECK(cubeboot_vmc_port_service(&PORT_B) == CUBEBOOT_VMC_EVENT_VIRTUAL_INSERTED);
    cubeboot_vmc_exi_init(&RT.exi1, &PORT_B, f_translate, &F);
    CHECK(r1(CUBEBOOT_VMC_EXI_CSR) & CUBEBOOT_VMC_CSR_EXT);          /* a card is in slot B */
    CHECK(id_ch1() == 0x00000008u);                                   /* 8 Mbit */
    sel(0, 4); imm(1, 0x00000000u, 2); CHECK(imm(0, 0, 4) == 0x00000004u); desel();      /* slot A still answers as its own 4 Mbit card */
    CHECK(id_ch1() == 0x00000008u);
    /* B's card selection does not leak into A and the hardware never sees B's traffic */
    {
        unsigned seen = 0, k;
        for (k = 0; k < F.nlog && k < HW_LOG_MAX; ++k)
            if (F.log[k].write && F.log[k].off == CH1 + CUBEBOOT_VMC_EXI_CR) seen++;
        CHECK(seen == 0);
    }
    /* the PI cause register merges the EXI bit if either virtual line is high: raise B's by an unmasked EXT interrupt */
    w1(CUBEBOOT_VMC_EXI_CSR, CUBEBOOT_VMC_CSR_EXTINTMASK);
    F.pi_intsr_real = 0;
    CHECK(!(cbvmc_mmio_read(&RT, CBVMC_PI_INTSR, 4) & CBVMC_PI_INTSR_EXI));
    memset(&RT.exi1, 0, sizeof(RT.exi1));                              /* back to not served */
}

int main(int argc, char **argv)
{
    const char *dump = NULL;
    int i;
    for (i = 1; i < argc; ++i) {
        if (strcmp(argv[i], "--dol") == 0 && i + 1 < argc) ++i;      /* accepted for compatibility, nothing here needs a game image */
        else dump = argv[i];
    }
    test_decode();            puts("decode ok");
    test_access_shapes();     puts("access shapes ok");
    test_ramstore();          puts("ram store ok");
    test_card_basic();        puts("card basic ok");
    test_interrupts();
    test_hot_swap();        puts("interrupts ok");
    test_card_save_cycle(dump); puts("card save cycle ok");
    test_slot_b();            puts("slot B ok");
    puts("cbvmc host tests passed");
    return 0;
}
