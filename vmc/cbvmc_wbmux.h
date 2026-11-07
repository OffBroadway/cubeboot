/* Copyright (C) 2026 Nigzel / Bjerreman. SPDX-License-Identifier: GPL-2.0-only */
#ifndef CBVMC_WBMUX_H
#define CBVMC_WBMUX_H

/*
 * Two virtual cards (slot A, slot B) on ONE write-back window (docs/virtual_memory_card.md).  Original work.
 * Each card gets its own struct cubeboot_vmc_store that points at the shared cbvmc_wb; every call first makes sure the window is bound
 * to that card's file (cbvmc_wb_rebind: the previous card's dirty block is committed with journal and read-back before the window moves).
 * A game that only uses slot A never rebinds.  Cost of the second card: one slot record (about 290 bytes), no second window, no second
 * scratch buffer, no second journal buffer.  The price is a commit and a reload whenever the game alternates between the two cards.
 */
#include "cbvmc_wbstore.h"

struct cbvmc_wbmux;

struct cbvmc_wbmux_slot {
    struct cbvmc_wbmux *mux;
    char path[CUBEBOOT_VMC_PATH_MAX];
    char jpath[CUBEBOOT_VMC_PATH_MAX];
};

struct cbvmc_wbmux {
    struct cbvmc_wb *wb;
    struct cubeboot_vmc_store inner;        /* the window's own store (cbvmc_wb_init) */
    const struct cbvmc_wbmux_slot *cur;     /* whose file the window is bound to now */
};

/* Sets the shared window up over `lower` (no file bound yet).  0 on success. */
int cbvmc_wbmux_init(struct cbvmc_wbmux *mux, struct cbvmc_wb *wb, const struct cubeboot_vmc_store *lower, uint8_t *window, uint32_t idle_ticks);

/* A card: `out` is the store its card model uses; the journal is "<path>.wal".  0 on success. */
int cbvmc_wbmux_add(struct cbvmc_wbmux *mux, struct cbvmc_wbmux_slot *slot, struct cubeboot_vmc_store *out, const char *path);

/* Replays a pending journal of this card (binds the window to it first). */
int cbvmc_wbmux_recover(struct cbvmc_wbmux *mux, const struct cbvmc_wbmux_slot *slot);

/* Creates this card's journal file in full size now (see cbvmc_wb_prepare_journal): a full SD volume refuses here, at install. */
int cbvmc_wbmux_prepare(struct cbvmc_wbmux *mux, const struct cbvmc_wbmux_slot *slot);

/* Commits whatever is dirty, whichever card owns the window. */
int cbvmc_wbmux_sync(struct cbvmc_wbmux *mux);

#endif
