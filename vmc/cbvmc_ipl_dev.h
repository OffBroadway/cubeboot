/* Copyright (C) 2026 Nigzel / Bjerreman. SPDX-License-Identifier: GPL-2.0-only */
#ifndef CBVMC_IPL_DEV_H
#define CBVMC_IPL_DEV_H

/*
 * G4 stage 1, Cubeboot side (compiled into the patch image only with CBVMC_IPL, see cubeboot_ipl_g4.patch).  Original work, GPL-2.0-only with the rest of cubeboot.dol.
 *   cbvmc_ipl_install_device()  from pre_thread_init, menu boot only, after gm_init_thread and BEFORE gm_start_thread: decide, copy the images into the top of the
 *                               patch window, run the installer.  Never blocks the boot: every refusal returns, the Cubeboot menu starts exactly as before.
 *   cbvmc_ipl_finish_device()   from bs2start, interrupts off, BS2 about to be discarded: take the DSI vector back, close the card files, write the counters file.
 */
void cbvmc_ipl_install_device(void);
void cbvmc_ipl_finish_device(void);
void cbvmc_ipl_watch(void);         // from bs2tick: vector/word watch, re-assert, re-announce (diag6)
void cbvmc_ipl_patch_hb(void);      /* from bs2tick: the patch's own heartbeat (vmc_ipl_diag bit 15) */

#endif
