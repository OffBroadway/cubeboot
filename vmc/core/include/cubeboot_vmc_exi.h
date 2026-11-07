/* Copyright (C) 2026 Nigzel / Bjerreman. SPDX-License-Identifier: GPL-2.0-only */
#ifndef CUBEBOOT_VMC_VMC_EXI_H
#define CUBEBOOT_VMC_VMC_EXI_H

/*
 * EXI channel register model in front of a virtual card.
 *
 * This is the logic the resident hook runs when the game touches the EXI
 * registers of the channel that has a virtual card (channel 0 for slot A,
 * channel 1 for slot B): CSR / MAR / LENGTH / CR / DATA at 0x00/04/08/0C/10.
 * It has no hardware access, so it can be driven by a simulated game on the
 * host. The glue that intercepts the real registers and raises the real
 * exception is deliberately not here.
 *
 * Routing: while the port is not VIRTUAL, every access returns PASS and the
 * glue forwards it to the real hardware untouched. While VIRTUAL, accesses
 * are HANDLED here, except a transfer with a chip select other than CS0
 * (IPL/RTC/SRAM on channel 0, serial on channel 0/2), which also returns PASS.
 */

#include "cubeboot_vmc.h"

#define CUBEBOOT_VMC_EXI_CSR     0x00u
#define CUBEBOOT_VMC_EXI_MAR     0x04u
#define CUBEBOOT_VMC_EXI_LENGTH  0x08u
#define CUBEBOOT_VMC_EXI_CR      0x0cu
#define CUBEBOOT_VMC_EXI_DATA    0x10u

/* CSR bits */
#define CUBEBOOT_VMC_CSR_EXIINTMASK  (1u << 0)
#define CUBEBOOT_VMC_CSR_EXIINT      (1u << 1)
#define CUBEBOOT_VMC_CSR_TCINTMASK   (1u << 2)
#define CUBEBOOT_VMC_CSR_TCINT       (1u << 3)
#define CUBEBOOT_VMC_CSR_CLK_SHIFT   4
#define CUBEBOOT_VMC_CSR_CS_SHIFT    7          /* 3 bits: 1=CS0 2=CS1 4=CS2 */
#define CUBEBOOT_VMC_CSR_EXTINTMASK  (1u << 10)
#define CUBEBOOT_VMC_CSR_EXTINT      (1u << 11)
#define CUBEBOOT_VMC_CSR_EXT         (1u << 12)
#define CUBEBOOT_VMC_CSR_ROMDIS      (1u << 13)

/* CR bits */
#define CUBEBOOT_VMC_CR_TSTART       (1u << 0)
#define CUBEBOOT_VMC_CR_DMA          (1u << 1)
#define CUBEBOOT_VMC_CR_RW_SHIFT     2          /* 0 read, 1 write, 2 r/w */
#define CUBEBOOT_VMC_CR_TLEN_SHIFT   4          /* bytes - 1 */

enum cubeboot_vmc_exi_route {
    CUBEBOOT_VMC_EXI_PASS = 0,     /* forward this access to the hardware */
    CUBEBOOT_VMC_EXI_HANDLED = 1,  /* fully emulated here */
};

/* Maps a game DMA address to a host/console pointer for `length` bytes, or
 * NULL if the range is not valid RAM. On the console this is the uncached
 * mirror plus cache maintenance; the simulator returns a pointer into a
 * fake RAM array. */
typedef void *(*cubeboot_vmc_exi_translate)(void *context, uint32_t address,
                                            uint32_t length);

struct cubeboot_vmc_exi {
    struct cubeboot_vmc_port *port;
    cubeboot_vmc_exi_translate translate;
    void *translate_context;
    uint32_t csr;      /* stored bits only; EXT/EXIINT are derived on read */
    uint32_t mar;
    uint32_t length;
    uint32_t cr;
    uint32_t data;
    uint8_t cs0_selected;   /* virtual card currently has chip select */
};

void cubeboot_vmc_exi_init(struct cubeboot_vmc_exi *exi,
                           struct cubeboot_vmc_port *port,
                           cubeboot_vmc_exi_translate translate, void *context);

enum cubeboot_vmc_exi_route cubeboot_vmc_exi_read(struct cubeboot_vmc_exi *exi,
                                                  uint32_t offset,
                                                  uint32_t *value);
enum cubeboot_vmc_exi_route cubeboot_vmc_exi_write(struct cubeboot_vmc_exi *exi,
                                                   uint32_t offset,
                                                   uint32_t value);

/*
 * Run at an EXI request boundary (after chip select drops, and from a
 * periodic tick). Applies pending physical/virtual switches and, for a
 * virtual card appearing, latches EXTINT so the game sees an insertion.
 * Returns the event so the glue can raise the interrupt line. A removal
 * produces no EXTINT here: the hardware raises its own when a physical card
 * is inserted, and a removed virtual card has no register state to report.
 */
enum cubeboot_vmc_event cubeboot_vmc_exi_service(struct cubeboot_vmc_exi *exi);

/* Level of the interrupt line this channel asks for (masks applied). */
int cubeboot_vmc_exi_irq(const struct cubeboot_vmc_exi *exi);

#endif
