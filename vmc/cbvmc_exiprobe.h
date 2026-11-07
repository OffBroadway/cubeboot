/* Copyright (C) 2026 Nigzel / Bjerreman. SPDX-License-Identifier: GPL-2.0-only */
#ifndef CBVMC_EXIPROBE_H
#define CBVMC_EXIPROBE_H

/*
 * Is something in a memory card slot?  (Owner rule 2026-10-09: a physical card, a MemCard PRO or any other EXI device that answers wins;
 * the virtual card only serves a slot that is empty.)  Raw EXI register access through two callbacks (index = word index in the channel's
 * register block: 0 CSR, 1 MAR, 2 LENGTH, 3 CR, 4 DATA), so the same code runs on the console (loader, before the OS exists or next to libogc),
 * in the Dolphin harness and on the host against a model.  The sequence is the SDK's own ID read: slot detect (CSR bit 12), select device 0 at
 * 1 MHz, write 2 zero bytes (the ID command), read 4 bytes, deselect.  Interrupt masks and flags in CSR are left as found.
 */
#include <stdint.h>

enum cbvmc_slot_state {
    CBVMC_SLOT_EMPTY = 0,       /* nothing in the slot (no slot-detect, or no answer: 0 / 0xFFFFFFFF) */
    CBVMC_SLOT_CARD = 1,        /* answers with an ID the SDK would mount as a memory card */
    CBVMC_SLOT_OTHER = 2        /* something else answers (SD adapter, a card the SDK would refuse, ...) : counts as physical too */
};

struct cbvmc_exi_io {
    void *ctx;
    uint32_t (*rd)(void *ctx, unsigned idx);
    void (*wr)(void *ctx, unsigned idx, uint32_t v);
    unsigned max_polls;         /* bound of every wait (default 2,000,000) */
};

/* Returns the slot state; *id gets the ID word read (0 if none). On a timeout the slot counts as CBVMC_SLOT_OTHER (something is there and stuck). */
enum cbvmc_slot_state cbvmc_exi_probe_slot(const struct cbvmc_exi_io *io, uint32_t *id);

#endif
