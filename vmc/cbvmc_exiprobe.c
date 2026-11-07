/* Copyright (C) 2026 Nigzel / Bjerreman. SPDX-License-Identifier: GPL-2.0-only */
/* Slot probe, see cbvmc_exiprobe.h.  Original work (register layout from the public EXI documentation and the SDK's behaviour as seen in Dolphin). */
#include "cbvmc_exiprobe.h"

#define CSR_EXIINTMASK   0x0001u
#define CSR_TCINTMASK    0x0004u
#define CSR_EXTINTMASK   0x0400u
#define CSR_MASKS        (CSR_EXIINTMASK | CSR_TCINTMASK | CSR_EXTINTMASK)
#define CSR_EXT          0x1000u
#define CSR_CS0          (1u << 7)
#define CR_TSTART        0x01u
#define CR_WRITE         (1u << 2)

static int wait_idle(const struct cbvmc_exi_io *io)
{
    unsigned n = io->max_polls ? io->max_polls : 2000000u;
    while (n--) if (!(io->rd(io->ctx, 3) & CR_TSTART)) return 1;
    return 0;
}

enum cbvmc_slot_state cbvmc_exi_probe_slot(const struct cbvmc_exi_io *io, uint32_t *id)
{
    uint32_t csr = io->rd(io->ctx, 0), keep = csr & CSR_MASKS, v;
    enum cbvmc_slot_state st;
    *id = 0;
    if (!(csr & CSR_EXT)) return CBVMC_SLOT_EMPTY;
    if (!wait_idle(io)) return CBVMC_SLOT_OTHER;                         /* a transfer from earlier code never ends: do not touch the slot */
    io->wr(io->ctx, 0, keep | CSR_CS0);                                  /* device 0, clock 1 MHz (field 0) */
    io->wr(io->ctx, 4, 0);
    io->wr(io->ctx, 3, CR_TSTART | CR_WRITE | (1u << 4));               /* immediate write, 2 bytes: the ID command 0x0000 */
    if (!wait_idle(io)) { io->wr(io->ctx, 0, keep); return CBVMC_SLOT_OTHER; }
    io->wr(io->ctx, 3, CR_TSTART | (3u << 4));                           /* immediate read, 4 bytes */
    if (!wait_idle(io)) { io->wr(io->ctx, 0, keep); return CBVMC_SLOT_OTHER; }
    v = io->rd(io->ctx, 4);
    io->wr(io->ctx, 0, keep);                                            /* deselect, masks as found */
    *id = v;
    if (v == 0u || v == 0xFFFFFFFFu) return CBVMC_SLOT_EMPTY;            /* a slot-detect pin without an answer is no usable device */
    st = (((v & 0xFFFF0000u) == 0u && (v & 3u) == 0u) || v == 0x80000004u) ? CBVMC_SLOT_CARD : CBVMC_SLOT_OTHER;
    return st;
}
