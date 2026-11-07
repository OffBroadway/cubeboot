/* Copyright (C) 2026 Nigzel / Bjerreman. SPDX-License-Identifier: GPL-2.0-only */
/*
 * EXI channel register model for a virtual card. See include/cubeboot_vmc_exi.h.
 * Register semantics follow the retail EXI block as modelled by Dolphin's
 * EXI_Channel (behavioural reference only).
 */
#include <string.h>

#include "cubeboot_vmc_exi.h"

#define CSR_CS(csr)        (((csr) >> CUBEBOOT_VMC_CSR_CS_SHIFT) & 7u)
#define CSR_STORED_MASK    (CUBEBOOT_VMC_CSR_EXIINTMASK | CUBEBOOT_VMC_CSR_TCINTMASK | \
                            (7u << CUBEBOOT_VMC_CSR_CLK_SHIFT) | \
                            (7u << CUBEBOOT_VMC_CSR_CS_SHIFT) | \
                            CUBEBOOT_VMC_CSR_EXTINTMASK | CUBEBOOT_VMC_CSR_ROMDIS)
#define CSR_LATCHED_MASK   (CUBEBOOT_VMC_CSR_TCINT | CUBEBOOT_VMC_CSR_EXTINT)

void cubeboot_vmc_exi_init(struct cubeboot_vmc_exi *exi,
                           struct cubeboot_vmc_port *port,
                           cubeboot_vmc_exi_translate translate, void *context)
{
    memset(exi, 0, sizeof(*exi));
    exi->port = port;
    exi->translate = translate;
    exi->translate_context = context;
}

static int virtual_active(const struct cubeboot_vmc_exi *exi)
{
    return exi->port->source == CUBEBOOT_VMC_SOURCE_VIRTUAL;
}

static int card_irq_line(const struct cubeboot_vmc_exi *exi)
{
    return virtual_active(exi) && cubeboot_vmc_card_interrupt(&exi->port->card);
}

static uint32_t csr_value(const struct cubeboot_vmc_exi *exi)
{
    uint32_t v = exi->csr;
    if (virtual_active(exi)) v |= CUBEBOOT_VMC_CSR_EXT;
    if (card_irq_line(exi)) v |= CUBEBOOT_VMC_CSR_EXIINT;
    return v;
}

int cubeboot_vmc_exi_irq(const struct cubeboot_vmc_exi *exi)
{
    uint32_t v = csr_value(exi);
    return ((v & CUBEBOOT_VMC_CSR_EXIINT) && (v & CUBEBOOT_VMC_CSR_EXIINTMASK)) ||
           ((v & CUBEBOOT_VMC_CSR_TCINT) && (v & CUBEBOOT_VMC_CSR_TCINTMASK)) ||
           ((v & CUBEBOOT_VMC_CSR_EXTINT) && (v & CUBEBOOT_VMC_CSR_EXTINTMASK));
}

static void write_csr(struct cubeboot_vmc_exi *exi, uint32_t v)
{
    uint32_t keep = exi->csr & CSR_LATCHED_MASK;
    uint32_t old_cs = CSR_CS(exi->csr), new_cs = CSR_CS(v);
    struct cubeboot_vmc_card *card = &exi->port->card;

    /* TCINT and EXTINT are write-1-to-clear; EXIINT is the card's own line
     * and clears via the card's 0x89 command, not here. */
    if (v & CUBEBOOT_VMC_CSR_TCINT) keep &= ~CUBEBOOT_VMC_CSR_TCINT;
    if (v & CUBEBOOT_VMC_CSR_EXTINT) keep &= ~CUBEBOOT_VMC_CSR_EXTINT;
    exi->csr = (v & CSR_STORED_MASK) | keep;

    if (new_cs != old_cs) {
        if (exi->cs0_selected) {
            cubeboot_vmc_card_select(card, 0);
            exi->cs0_selected = 0;
        }
        if (new_cs == 1) {
            cubeboot_vmc_card_select(card, 1);
            exi->cs0_selected = 1;
        }
    }
}

static void do_transfer(struct cubeboot_vmc_exi *exi)
{
    struct cubeboot_vmc_card *card = &exi->port->card;
    uint32_t cr = exi->cr;
    unsigned rw = (cr >> CUBEBOOT_VMC_CR_RW_SHIFT) & 3u;

    if (cr & CUBEBOOT_VMC_CR_DMA) {
        void *mem = exi->translate
            ? exi->translate(exi->translate_context, exi->mar, exi->length)
            : NULL;
        if (mem) {
            if (rw == 0) cubeboot_vmc_card_dma_read(card, mem, exi->length);
            else if (rw == 1) cubeboot_vmc_card_dma_write(card, mem, exi->length);
        }
    } else {
        unsigned len = ((cr >> CUBEBOOT_VMC_CR_TLEN_SHIFT) & 3u) + 1u;
        uint32_t in = exi->data, out = 0;
        unsigned i;
        for (i = 0; i < len; ++i) {
            uint8_t tx = rw == 0 ? 0 : (uint8_t)(in >> 24);
            uint8_t rx = cubeboot_vmc_card_transfer_byte(card, tx);
            in <<= 8;
            out |= (uint32_t)rx << (24 - 8 * i);
        }
        if (rw != 1) exi->data = out;       /* read and read/write return data */
    }
    exi->cr = cr & ~CUBEBOOT_VMC_CR_TSTART; /* transfer completes synchronously */
    exi->csr |= CUBEBOOT_VMC_CSR_TCINT;
}

enum cubeboot_vmc_exi_route cubeboot_vmc_exi_read(struct cubeboot_vmc_exi *exi,
                                                  uint32_t offset,
                                                  uint32_t *value)
{
    if (!virtual_active(exi)) return CUBEBOOT_VMC_EXI_PASS;
    switch (offset) {
    case CUBEBOOT_VMC_EXI_CSR: *value = csr_value(exi); break;
    case CUBEBOOT_VMC_EXI_MAR: *value = exi->mar; break;
    case CUBEBOOT_VMC_EXI_LENGTH: *value = exi->length; break;
    case CUBEBOOT_VMC_EXI_CR: *value = exi->cr; break;
    case CUBEBOOT_VMC_EXI_DATA: *value = exi->data; break;
    default: return CUBEBOOT_VMC_EXI_PASS;
    }
    return CUBEBOOT_VMC_EXI_HANDLED;
}

enum cubeboot_vmc_exi_route cubeboot_vmc_exi_write(struct cubeboot_vmc_exi *exi,
                                                   uint32_t offset,
                                                   uint32_t value)
{
    if (!virtual_active(exi)) return CUBEBOOT_VMC_EXI_PASS;
    switch (offset) {
    case CUBEBOOT_VMC_EXI_CSR: write_csr(exi, value); break;
    case CUBEBOOT_VMC_EXI_MAR: exi->mar = value & 0x03ffffe0u; break;
    case CUBEBOOT_VMC_EXI_LENGTH: exi->length = value & 0x03ffffe0u; break;
    case CUBEBOOT_VMC_EXI_DATA: exi->data = value; break;
    case CUBEBOOT_VMC_EXI_CR:
        exi->cr = value & 0x3fu;
        if (exi->cr & CUBEBOOT_VMC_CR_TSTART) {
            if (!exi->cs0_selected) return CUBEBOOT_VMC_EXI_PASS;  /* other device */
            do_transfer(exi);
        }
        break;
    default: return CUBEBOOT_VMC_EXI_PASS;
    }
    return CUBEBOOT_VMC_EXI_HANDLED;
}

enum cubeboot_vmc_event cubeboot_vmc_exi_service(struct cubeboot_vmc_exi *exi)
{
    int was_virtual = virtual_active(exi);
    enum cubeboot_vmc_event e = cubeboot_vmc_port_service(exi->port);

    if (e == CUBEBOOT_VMC_EVENT_VIRTUAL_INSERTED) {
        /* New card: fresh EXI state, then announce it like a real insertion. */
        exi->cs0_selected = 0;
        exi->csr = (exi->csr & ~(7u << CUBEBOOT_VMC_CSR_CS_SHIFT)) |
                   CUBEBOOT_VMC_CSR_EXTINT;
    } else if (e == CUBEBOOT_VMC_EVENT_VIRTUAL_REMOVED || (was_virtual && !virtual_active(exi))) {
        exi->cs0_selected = 0;
    }
    return e;
}
