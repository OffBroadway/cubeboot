/* Copyright (C) 2026 Nigzel / Bjerreman. SPDX-License-Identifier: GPL-2.0-only */
#ifndef CBVMC_FLASH_HOOK_H
#define CBVMC_FLASH_HOOK_H

/*
 * The two calls the Cubeboot patch (cubeboot_vmc_hook.patch) makes around cbvmc_loader_install.  Part of the patched cubeboot.dol, so GPL-2.0-only
 * with the rest of that file (docs/LICENSING_AND_DISTRIBUTION.md).  Original work.
 */
#include <stdint.h>

/* Probes EXI slots A and B (raw register access, the SDK's own ID read).  Returns the mask of slots the virtual card may serve: bit 0 = slot A is empty,
   bit 1 = slot B is empty.  A physical memory card, a MemCard PRO or any other device in a slot clears its bit (physical devices always win). 0 = serve nothing. */
uint32_t cbvmc_flash_probe(void);

/* Settings from /config.ini [cubeboot], copied into the patch's variables by Cubeboot's stage 1 by symbol name (settings.c / main.c): cbvmc_enable
   (default 1 = on, also without a config.ini), cbvmc_dir (the card folder, "" = /cubeboot/saves), cbvmc_blocks, cbvmc_ipl_diag. */
int cbvmc_flash_enabled(void);


/* Writes the mask and the card paths from config.ini (if any) into the copy of the resident image that cbvmc_loader_install just placed (the install image reads them at the game's OSInit). */
void cbvmc_flash_set_allow(uint32_t allow, int loader_rc, unsigned layer);     /* layer: CBVMC_LAYER_* of the game (cbvmc_layer_of_game) */
const char *cbvmc_flash_dir(void);
unsigned cbvmc_layer_of_game(const char *code);
unsigned cbvmc_flash_regions(void);
void cbvmc_layer_path(const char *dir, unsigned layer, unsigned slot, char *out, unsigned cap);
/* One line for the loader menu: "Slot A: Memory Card   Slot B: Virtual Card" (re-probes the slots every 120 calls). Never NULL. */
const char *cbvmc_flash_menu_line(void);      /* loader_rc: the result of cbvmc_loader_install, kept in the diagnostics block */

#endif
