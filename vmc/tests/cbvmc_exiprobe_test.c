/* Copyright (C) 2026 Nigzel / Bjerreman. SPDX-License-Identifier: GPL-2.0-only */
/* Host test of the slot probe against a small EXI channel model: empty slot, memory card, MemCard PRO style card, other device, floating bus. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "cbvmc_exiprobe.h"

#define CHECK(c) do { if (!(c)) { fprintf(stderr, "%s:%d: CHECK failed: %s\n", __FILE__, __LINE__, #c); abort(); } } while (0)

struct exi {
    uint32_t r[5];
    int ext;                /* slot-detect pin */
    uint32_t id;            /* what the device answers to the ID command (0 and 0xFFFFFFFF: nothing) */
    int stuck;              /* a transfer never completes */
    int step;               /* 0 idle, 1 after the 2-byte ID command */
    unsigned writes, selects;
    uint32_t last_csr_write;
};

static uint32_t rd(void *c, unsigned i)
{
    struct exi *e = c;
    uint32_t v = e->r[i];
    if (i == 0 && e->ext) v |= 0x1000u;
    return v;
}
static void wr(void *c, unsigned i, uint32_t v)
{
    struct exi *e = c;
    e->writes++;
    if (i == 0) {
        e->last_csr_write = v;
        if (v & (7u << 7)) e->selects++;
        e->r[0] = v & ~0x1000u;
        if (!(v & (7u << 7))) e->step = 0;
        return;
    }
    if (i == 3 && (v & 1u)) {
        if (e->stuck) { e->r[3] = v; return; }
        if (v & 4u) { if (((v >> 4) & 3u) == 1u && (e->r[4] >> 16) == 0) e->step = 1; e->r[3] = v & ~1u; }
        else { e->r[4] = e->step == 1 ? e->id : 0xFFFFFFFFu; e->r[3] = v & ~1u; }
        return;
    }
    e->r[i] = v;
}

int main(void)
{
    struct exi e;
    struct cbvmc_exi_io io = { &e, rd, wr, 1000 };
    uint32_t id;

    memset(&e, 0, sizeof(e));                                  /* nothing in the slot: no pin, no access at all */
    CHECK(cbvmc_exi_probe_slot(&io, &id) == CBVMC_SLOT_EMPTY && e.writes == 0 && e.selects == 0);

    memset(&e, 0, sizeof(e)); e.ext = 1; e.id = 0x00000004u;   /* a 4 Mbit card */
    e.r[0] = 0x0405;                                           /* the OS's interrupt masks are on: must come back unchanged */
    CHECK(cbvmc_exi_probe_slot(&io, &id) == CBVMC_SLOT_CARD && id == 4 && e.selects == 1);
    CHECK((e.r[0] & ~0x1000u) == 0x0405 && (e.last_csr_write & (7u << 7)) == 0);   /* deselected, masks as found */

    memset(&e, 0, sizeof(e)); e.ext = 1; e.id = 0x00000080u;   /* a 128 Mbit card / SD based card device answering like a card */
    CHECK(cbvmc_exi_probe_slot(&io, &id) == CBVMC_SLOT_CARD);
    memset(&e, 0, sizeof(e)); e.ext = 1; e.id = 0x80000004u;   /* the vendor-ID family the SDK accepts */
    CHECK(cbvmc_exi_probe_slot(&io, &id) == CBVMC_SLOT_CARD);
    memset(&e, 0, sizeof(e)); e.ext = 1; e.id = 0x04020010u;   /* an SD adapter style ID: not a card, but physical */
    CHECK(cbvmc_exi_probe_slot(&io, &id) == CBVMC_SLOT_OTHER && id == 0x04020010u);
    memset(&e, 0, sizeof(e)); e.ext = 1; e.id = 0xFFFFFFFFu;   /* pin set but nothing answers */
    CHECK(cbvmc_exi_probe_slot(&io, &id) == CBVMC_SLOT_EMPTY);
    memset(&e, 0, sizeof(e)); e.ext = 1; e.id = 0;
    CHECK(cbvmc_exi_probe_slot(&io, &id) == CBVMC_SLOT_EMPTY);
    memset(&e, 0, sizeof(e)); e.ext = 1; e.id = 4; e.stuck = 1; /* never completes: counts as physical, bounded */
    CHECK(cbvmc_exi_probe_slot(&io, &id) == CBVMC_SLOT_OTHER);
    memset(&e, 0, sizeof(e)); e.ext = 1; e.id = 4; e.r[3] = 1;  /* an earlier transfer still running: not touched */
    CHECK(cbvmc_exi_probe_slot(&io, &id) == CBVMC_SLOT_OTHER && e.selects == 0);
    puts("cbvmc_exiprobe_test: all passed");
    return 0;
}
