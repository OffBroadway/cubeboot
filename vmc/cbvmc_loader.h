/* Copyright (C) 2026 Nigzel / Bjerreman. SPDX-License-Identifier: GPL-2.0-only */
#ifndef CBVMC_LOADER_H
#define CBVMC_LOADER_H

/*
 * Loader-side half of the cubeboot virtual card (docs/virtual_memory_card.md).  Original work, no Cubeboot code here.
 * Called by the Cubeboot patch (cubeboot_vmc_hook.patch, GPL-2.0) after the game DOL has been loaded to its final addresses and before the jump
 * to its entry point.  It does not talk to the drive and does not format anything: it checks, copies the resident image into the memory the
 * loader took out of the arena (cbvmc_plan.h), stages the install image inside the arena below bi2, and replaces one word of the game's OSInit
 * (the `bl OSInitAlarm` after `bl __OSInitSystemCall`) with a `bl` to the install image.  Everything else happens inside the game's OSInit.
 */
#include <stdint.h>

#include "cbvmc_plan.h"
#include "cbvmc_words.h"

enum {
    CBVMC_LOADER_OK = 0,
    CBVMC_LOADER_ERR_GAME = -1,         /* (unused since the per-game path went) */
    CBVMC_LOADER_ERR_WORDS = -2,        /* the word table does not match this DOL (version, already patched) */
    CBVMC_LOADER_ERR_HOOK = -3,         /* the OSInit hook word is not `bl OSInitAlarm` */
    CBVMC_LOADER_ERR_IMAGE = -4,        /* an image does not fit its region, or the regions overlap the game DOL / bi2 */
    CBVMC_LOADER_ERR_ARGS = -5
};

struct cbvmc_loader_cfg {
    uint8_t *ram;                       /* game memory: pointer that corresponds to ram_base (the loader uses the cached mirror 0x80000000) */
    uint32_t ram_base;                  /* 0x80000000 */
    uint32_t ram_size;                  /* 0x01800000 */
    const char *game_code;              /* 4 characters of the disc header (reports only) */
    uint32_t dol_max;                   /* highest address used by the game DOL (Cubeboot's DOLMax) */
    uint32_t bi2_addr;                  /* lowest address the loader placed below the arena top (bi2); the install image must end below it */
    const uint8_t *res_image;           /* resident image linked at CBVMC_R_BASE */
    uint32_t res_len;
    const uint8_t *ins_image;           /* install image linked at CBVMC_B_BASE (entry at its first byte) */
    uint32_t ins_len;
};

/* Returns CBVMC_LOADER_OK or a negative reason; on any error nothing was written. The function flushes what it wrote.
   Any game, with the word table and the hook the scanner (cbvmc_scan.c) found. Same checks (every original word must
   match, the hook word must be `bl hook_target`, the images must fit); no game-code check. The table is also written to the hook block so that the
   install image applies the same words.  nwords = 0 is the KEEP-ALIVE install of an executable the scanner refused but whose install point is proven
   (unique `bl EXIInit` right after __OSInitSystemCall): card files, blob, vector and the T2 breakpoint, no word, so the next executable is still caught
   (docs/virtual_memory_card.md and 8). */
int cbvmc_loader_install_generic(const struct cbvmc_loader_cfg *cfg, const struct cbvmc_word *words, unsigned nwords, uint32_t hook_addr, uint32_t hook_target);

#endif
