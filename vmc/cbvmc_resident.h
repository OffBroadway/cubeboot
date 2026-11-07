/* Copyright (C) 2026 Nigzel / Bjerreman. SPDX-License-Identifier: GPL-2.0-only */
#ifndef CBVMC_RESIDENT_H
#define CBVMC_RESIDENT_H

/*
 * Interface between the install image and the resident image (cbvmc_plan.h).  The resident image owns every byte that must survive the
 * game's boot: the card store chain (DI transport + Flippy file API + write-back window with journal, or a RAM lower store in the Dolphin
 * test build), its state, and the DSI handler stack.  The install image only calls these functions.
 */
#include <stdint.h>

#include "cubeboot_vmc.h"

#define CBVMC_CARD_PATH_MAX 88u          /* with ".wal" it must stay under CUBEBOOT_VMC_PATH_MAX (96) */
const char *cbvmc_resident_card_path(unsigned slot);      /* slot 0 = A, 1 = B */
/* IPL path: the card folder and the active layer (CBVMC_LAYER_*), the layer file in the folder, and the runtime switch (popup Layer item) */
const char *cbvmc_resident_dir(void);
unsigned cbvmc_resident_layer(void);
unsigned cbvmc_resident_regions(void);
void cbvmc_resident_set_regions(unsigned mode);
unsigned cbvmc_resident_next_layer(unsigned layer);
void cbvmc_resident_layer_paths(const char *dir, unsigned layer);
int cbvmc_resident_layer_file_read(unsigned *layer);
int cbvmc_resident_layer_file_write(unsigned layer);
int cbvmc_resident_set_layer(unsigned layer);
uint32_t cbvmc_resident_slot_allow(void);
/* Geometry of a card file the install image has to generate (cbvmc_geom_unpack of cbvmc_cardgen.h): written by the loader from config.ini; 0 = the default. */
uint32_t cbvmc_resident_card_geom(void);
/* Port record of slot B (the card model state of the second card lives here, not in the blob). */
struct cubeboot_vmc_port *cbvmc_resident_port_b(void);

enum {
    CBVMC_OPEN_OK = 0,
    CBVMC_OPEN_ERR_DRIVE = 3,       /* not a normal-mode FlippyDrive */
    CBVMC_OPEN_ERR_NOFILE = 4,      /* the card file does not exist (never created here) */
    CBVMC_OPEN_ERR_JOURNAL = 5,     /* a pending journal could not be replayed */
    CBVMC_OPEN_ERR_BIND = 7
};

/* Brings the store up for the card file of `slot` (journal "<path>.wal"; the first call also brings the drive up). Both cards share ONE write-back
   window (cbvmc_wbmux.h). On CBVMC_OPEN_OK *top is the store the model of that slot must use. */
int cbvmc_resident_open(unsigned slot, const struct cubeboot_vmc_store **top, int *detail);

/* Replays a pending journal of `slot` and returns 0, or <0. Called once after open, before the card is validated. */
int cbvmc_resident_recover(unsigned slot);

/* Makes the journal of `slot` complete now (so a full SD volume is found at install). 0 or <0. Call after recover. */
int cbvmc_resident_prepare(unsigned slot);

/* Diagnostics hook for the card model (cubeboot_vmc_card.trace): records every card command / DMA / commit into a ring written to /cubeboot/cbvmc_events.bin; NULL in the RAM build. */
void (*cbvmc_resident_trace_hook(void))(const struct cubeboot_vmc_card *, char, uint32_t, uint32_t, int, const void *);

/* Best-effort status note for the owner (/cubeboot/cbvmc_status.txt on the SD; replaced each boot): what happened to each slot. Never fails the install. */
void cbvmc_resident_note(const char *text);

/* Idle clock (PI INTSR reads) and the commit-everything call for reset / shutdown paths. */
void cbvmc_resident_tick(void *context);
void *cbvmc_resident_tick_context(void);
int cbvmc_resident_sync(void);

/* Top of the DSI handler stack (16-byte aligned).  3 KiB: the T2 handler runs the scanner on it (about 1.4 KiB of frames, build_blob.sh prints the -fstack-usage maximum). */
#define CBVMC_HSTACK_BYTES 3072u
uint32_t cbvmc_resident_stack_top(void);

/* T2 multi-executable trigger (cbvmc_t2.h, cbvmc_resident_t2.c).  Called by the install image after the vector and the word table (`words` = how many,
   0 = keep-alive install of a refused first executable): snapshot of the blob's vector ranges, breakpoint handler, DABR.  0 = armed, 1 = off (vmc_t2 = 0). */
extern uint32_t cbvmc_t2_enable;
int cbvmc_resident_t2_arm(unsigned words);
/* Per-executable report line (Flippy build: /cubeboot/cbvmc_stages.txt, rewritten whole; RAM build: kept in memory).  `restart` = first line of this boot. */
void cbvmc_resident_stage_note(const char *text, unsigned len, int restart);
/* Freeze diagnostics: refresh the counters file now if its period is due (called from the blob every 4096 handled faults). */
void cbvmc_resident_diag(void);

#endif
