/* Copyright (C) 2026 Nigzel / Bjerreman. SPDX-License-Identifier: GPL-2.0-only */
#ifndef CBVMC_CARDGEN_H
#define CBVMC_CARDGEN_H

/*
 * Generate a missing virtual card file (docs/virtual_memory_card.md).  Original work.  Used by the loader DOL (before the game, with a progress line)
 * and by the install image (flash cubeboot.dol, inside the game's OSInit).
 *
 * Rules: an existing file is never touched (valid or not: a bad one is refused later by the card open, never overwritten); nothing is ever created under
 * /swiss/ (the owner's Swiss cards are off limits); the folders are created by the store (make_parents of the Flippy store); the card is built by the
 * model's cubeboot_vmc_format into "<path>.tmp", validated, and only then renamed to <path>, so a power cut or a full SD leaves at most a stale .tmp (removed
 * by the next attempt) and a final file that is either absent or complete.  The result is the same 128 Mbit (16 MiB) ANSI card the model formats everywhere
 * else: no game file inside; the game creates its own save on first use.
 */
#include <stdint.h>

#include "cubeboot_vmc.h"

#define CBVMC_GEN_EXISTS    1       /* the file is there: untouched */
#define CBVMC_GEN_CREATED   0
#define CBVMC_GEN_ERR_SWISS (-20)   /* (no longer returned: a missing card is generated in whatever folder vmc_dir names, Swiss's too) */
#define CBVMC_GEN_ERR_PATH  (-21)   /* path too long / not absolute */
/* anything else negative: the model's error (CUBEBOOT_VMC_ERR_*) from create / write (full or read-only SD) / flush / validate / rename */

struct cbvmc_gen_stats {
    uint32_t writes;                /* store writes made */
    uint32_t bytes;                 /* ... bytes written */
};

/* Card geometry as one word, the form in which the Cubeboot patch hands config.ini's vmc_blocks / vmc_region to the resident image (cbvmc_resident_card_geom):
   bits 0..15 Mbit count (4, 8, 16, 32, 64, 128), bits 16..23 encoding (0 Windows-1252, 1 Shift-JIS), bits 24..31 the marker 0x47.  0 = not set.
   cbvmc_geom_unpack writes the default (128 Mbit, Windows-1252) and returns -1 when the word is 0 or not a valid geometry, else 0. */
#define CBVMC_GEOM_MARK 0x47000000u
uint32_t cbvmc_geom_pack(unsigned mbits, unsigned encoding);
int cbvmc_geom_unpack(uint32_t packed, struct cubeboot_vmc_geometry *out);

/* `progress` (may be NULL) is called after every MiB written with (bytes done, bytes total). `geom` NULL = 128 Mbit ANSI. Returns the codes above. */
int cbvmc_cardgen(const struct cubeboot_vmc_store *store, const char *path, unsigned slot, const struct cubeboot_vmc_identity *id,
                  const struct cubeboot_vmc_geometry *geom, void (*progress)(void *ctx, uint32_t done, uint32_t total), void *ctx, struct cbvmc_gen_stats *stats);

#endif
