/* Copyright (C) 2026 Nigzel / Bjerreman. SPDX-License-Identifier: GPL-2.0-only */
#ifndef CBVMC_IPL_PLAN_H
#define CBVMC_IPL_PLAN_H

/*
 * G4 stage 1 (docs/virtual_memory_card.md): the virtual cards inside the IPL's own Memory Card menu.  Original work.
 *
 * Memory plan of the IPL install.  Measured on the owner's console (docs/data/ipl_probe_console_2026-10-10.txt, PAL 1.2, ipl_rev_id 11):
 *   - 0x80000C00..0x80003000 is NOT free in BS2 (1860 of 2304 words nonzero): the game-path blob (cbvmc_vec.S, absolute 16-bit addressing) cannot live there.
 *   - BS2's OS arena ends at 0x81600000 (Cubeboot lowers it, patches/source/patch.s _reduce_arena_size); the Cubeboot patch image starts there
 *     (patches_end 0x816477E0 on the probe build) and the stage-1 framebuffer follows it (xfb = ROUND_UP_1K(_patches_end), 640 x 528 x 2 = 0xA5000 bytes
 *     on PAL, so the observed zero run starts at 0x816ED000).  Nothing of BS2 or of the Cubeboot menu is placed in 0x816F0000..0x81700000: BS2 OSInit
 *     clears only up to its arena top, the menu's FST buffer starts at 0x81700000 (dolphin_dvd.c), the menu heap is a 2 MiB buffer in the 0x80100000 window.
 *   - the real-mode DSI entry needs no low-memory scratch: it uses SPRG0 (the SDK stub that follows overwrites it with r4 as its first instruction) and the
 *     indexed store `stwx rS,0,rB`, which takes the whole address from a register (cbvmc_vec_ipl.S), so everything lives in this region.
 * The region is 64 KiB at CBVMC_IPL_BASE.  The install code checks, before it touches anything, that the region is all zero and that the stage-1
 * framebuffer (patches_end + CBVMC_IPL_XFB_MAX) ends below it.  Dolphin tests link the images at another base (CBVMC_IPL_BASE is overridable).
 */
#include <stdint.h>

#include "cbvmc_words.h"

#ifndef CBVMC_IPL_BASE
#define CBVMC_IPL_BASE        0x817C0000u   /* 128 KiB at the top of MEM1 (2026-11-06 dump of the live menu, docs/virtual_memory_card.md): BS2's framebuffer is 0x81650000..0x816EFFFF, above 0x81700000 only Cubeboot's disc-FST scratch grows upward (768 KiB left for it); 0x817FE000.. is the OS's */
#endif
#define CBVMC_IPL_SIZE        0x20000u
#define CBVMC_IPL_BLOB_BASE   (CBVMC_IPL_BASE)                      /* trap blob: real-mode entry, DSI handler, card model state; ends before CBVMC_IPL_RES_BASE */
#define CBVMC_IPL_BLOB_SIZE   0x2000u
#define CBVMC_IPL_RES_BASE    (CBVMC_IPL_BASE + CBVMC_IPL_BLOB_SIZE) /* resident + installer image: Flippy read-only store, model copy, installer, handler stack */
#define CBVMC_IPL_RES_SIZE    0x1D000u
#define CBVMC_IPL_ARGS        (CBVMC_IPL_BASE + 0x1F000u)            /* struct cbvmc_ipl_args, written by the Cubeboot patch, results read back from it */
#define CBVMC_IPL_END         (CBVMC_IPL_BASE + CBVMC_IPL_SIZE)
#define CBVMC_IPL_XFB_MAX     0xA5000u                              /* 640 x 528 x 2: the stage-1 framebuffer of the PAL mode Cubeboot selects (TVPal528IntDf); NTSC and EURGB60 use 480 lines = 0x96000 */

/* The image starts with two branches (cbvmc_ipl_tbl.S): +0 install, +4 finish. */
#define CBVMC_IPL_ENTRY_INSTALL (CBVMC_IPL_RES_BASE + 0u)
#define CBVMC_IPL_ENTRY_FINISH  (CBVMC_IPL_RES_BASE + 4u)
#define CBVMC_IPL_ENTRY_REPLUG  (CBVMC_IPL_RES_BASE + 8u)   /* latch the hot-plug EXTINT of the live slots again (diag6) */

#define CBVMC_IPL_ARGS_MAGIC  0x49504C41u                           /* 'IPLA' */
#define CBVMC_IPL_PATH_MAX    88u

/* vmc_ipl_diag bits (a bisect aid for the console, 2026-10-11 after the first G4 run: install OK but the loader grid ignored every input).  0 = the full install. */
#define CBVMC_IPLD_STATUS      0x01u    /* vmc_diag = 1: the four status files (vmc_scan.txt, cbvmc_ipl_decision.txt, cbvmc_ipl_status.txt, cbvmc_status.txt), else nothing is written */
#define CBVMC_IPLD_NO_Z        0x40000000u /* do not read the SI controller buffer (Z skip) */
#define CBVMC_IPLD_NO_ATOMIC   0x02u    /* gm thread's open+status pair NOT made atomic (flippy_sync.c) */
#define CBVMC_IPLD_NO_EXTINT   0x04u    /* do not latch the hot-plug EXTINT after the vector write */
#define CBVMC_IPLD_KEEP_MCP    0x08u    /* keep Cubeboot's MemCard PRO game-id traffic (it is switched off while a virtual card is live) */
#define CBVMC_IPLD_NO_A        0x10u    /* never serve slot A */
#define CBVMC_IPLD_NO_B        0x20u    /* never serve slot B */
#define CBVMC_IPLD_DRY         0x40u    /* everything except the vector word and the BS2 words: opens the cards, validates, copies, but applies nothing */
#define CBVMC_IPLD_NO_HB       0x80u    /* no heartbeat file */
/* second set, 2026-10-11: the dry run (0x40) still froze the grid, so the cause is in what happens BEFORE any BS2 word is patched.  These bits take the pre-patch steps apart: */
#define CBVMC_IPLD_SKIP_ALL    0x100u   /* decide and write the decision, then nothing: no memset, no copy, no installer call */
#define CBVMC_IPLD_BLOB_ONLY   0x200u   /* memset + copy the blob image only; no resident image, no installer call */
#define CBVMC_IPLD_NO_CALL     0x400u   /* memset + copy both images, args block; do not call the installer (no Flippy file API use by the installer) */
#define CBVMC_IPLD_NO_PROBE    0x800u   /* no EXI channel sampling and no slot probe (allow = 3 assumed), no interrupt-off section */
#define CBVMC_IPLD_NO_DECFILE  0x1000u  /* do not write /cubeboot/cbvmc_ipl_decision.txt */
#define CBVMC_IPLD_NO_MEMSET   0x2000u  /* copy without zeroing the region first */
#define CBVMC_IPLD_NO_SCAN     0x4000u  /* skip the scan/decision altogether (no read of BS2 text, no SPR reads): with 0x100 the patch only writes the decision stub */
#define CBVMC_IPLD_PATCH_HB    0x8000u  /* heartbeat written by the PATCH from bs2tick every 3 s (/cubeboot/cbvmc_ipl_phb.txt): works without the trap, e.g. in the dry run */
#define CBVMC_IPLD_ENABLE_Z    0x20000u /* opt in to the Z-skip read of the SI controller buffer (OFF by default: it killed the menu's pad input on the console) */
#define CBVMC_IPLD_TRAP_HB     0x40000u /* opt in to the trap-side heartbeat file (cbvmc_ipl_hb.txt); OFF by default since diag4 (bit 7 is now redundant) */
/* STOP LEVEL (diag4, bits 24..27 = L << 24): cbvmc_ipl_install_device returns right after step L, L = 0 runs everything.
   1 at the very top (nothing runs)   2 after memset(&in)+simple stores   3 after get_ipl_revision   4 after the SPR/BAT/MSR/stack reads and the pad-pointer store
   5 after custom_OSReport("start")   6 after the EXI sampling + slot probe   7 after cbvmc_ipl_decide   8 after memset/copy of the images and the args block (installer not called)
   9 after the installer call and the MemCard PRO switch-off   (the decision file and the closing OSReport are after 9) */
#define CBVMC_IPLD_STOP_SHIFT  24u
#define CBVMC_IPLD_REASSERT    0x400000u /* diag6: bs2tick writes the DSI vector word again whenever it finds it changed (counted) */
#define CBVMC_IPLD_REPLUG      0x800000u /* diag6: latch the hot-plug EXTINT again at frames 120, 600 and 1800 (from bs2tick) */
#define CBVMC_IPLD_DUMP_BS2    0x200000u  /* one-off (bit 21; bits 24-27 are the stop level): write BS2 (0x81300000, 1 MiB) and low memory to /cubeboot/bs2.bin after the install (menu reverse engineering) */
#define CBVMC_IPLD_DUMP_HI     0x80000u  /* one-off: with PATCH_HB, the 4th heartbeat writes 0x81600000..0x81800000 to /cubeboot/hi.bin (memory map above the region) */
#define CBVMC_IPLD_NO_MENU     0x100000u /* no Memory Card page popup extension (the page stays stock) */
#define CBVMC_IPLD_CLOSE_DRY   0x10000u
#define CBVMC_IPLD_NOTES       0x10000000u /* the per-action and periodic notes (popup note, IPL counters, stage lines of the game path); off by default since build 57 */ /* with DRY (0x40): call the hand-over right after the install: file handles closed again, counters file written */

enum cbvmc_ipl_status {                                             /* result of cbvmc_ipl_install: nothing was changed unless OK */
    CBVMC_IPLS_OK = 0,
    CBVMC_IPLS_ARGS = 1,        /* bad argument block */
    CBVMC_IPLS_PHYSICAL = 2,    /* both slots hold a physical device: nothing to serve */
    CBVMC_IPLS_VECTOR = 3,      /* the DSI vector's first word is not the SDK stub's */
    CBVMC_IPLS_WORDS = 4,       /* a word of the table does not hold the expected original (detail = index) */
    CBVMC_IPLS_NOCARD = 5,      /* no slot could be served (card file missing, journal pending, invalid, drive not a FlippyDrive); detail = first slot error */
    CBVMC_IPLS_BIND = 6,
    CBVMC_IPLS_EXIBUSY = 7      /* the EXI channels were never quiet (a transfer or a selected device) within the bound */
};

struct cbvmc_ipl_args {
    uint32_t magic;
    uint32_t version;
    uint32_t allow;             /* slot mask the physical-device probe allowed: bit 0 = slot A, bit 1 = slot B */
    uint32_t flags;             /* diagnostic switches (config.ini vmc_ipl_diag): CBVMC_IPLD_* */
    uint32_t nwords;
    char path_a[CBVMC_IPL_PATH_MAX];    /* card files ("" = the built-in default of the resident image) */
    char path_b[CBVMC_IPL_PATH_MAX];
    struct cbvmc_word words[64];        /* CBVMC_SCAN_MAX_WORDS */
    /* results, written by the installer */
    uint32_t res_status;        /* enum cbvmc_ipl_status */
    uint32_t res_detail;
    uint32_t res_live;          /* slot mask actually served */
    uint32_t res_slot_status[2];/* per slot: 0 served, else the open/validation error */
    uint32_t res_slot_detail[2];
    uint32_t res_exi_tries;     /* attempts until the EXI channels were quiet */
    uint32_t res_ticks;         /* time base ticks of the whole install */
    uint32_t res_vec_before;    /* DSI vector first word before / after */
    uint32_t res_vec_after;
    uint32_t hb_addr;           /* address of the patch's counter array (uint32_t[8]) for the heartbeat file, 0 = none */
    uint32_t physical;          /* slots with a physical device at the probe (bit 0 = A, bit 1 = B): served PARKED, the virtual card comes back when the device is pulled */
    uint32_t geom;              /* version 3: packed geometry of a card file generated this boot (cbvmc_geom_pack of config.ini vmc_blocks), 0 = the default; the encoding follows the layer */
    char dir[CBVMC_IPL_PATH_MAX];   /* version 4: the card folder (config.ini vmc_dir, "" = /cubeboot/saves) */
    uint32_t layer;             /* version 4: the console's default layer (CBVMC_LAYER_*), overridden by <dir>/vmc_region.txt */
    uint32_t regions;           /* version 5: CBVMC_REGIONS_STOCK (US and PAL share the USA pair, as a real card does) or CBVMC_REGIONS_SWISS (three pairs by game region) */
};
#define CBVMC_REGIONS_STOCK 0u
#define CBVMC_REGIONS_SWISS 1u
/* Card layers (one pair of card files each, Swiss's names): the game path picks by the game's region, the IPL page by the console's region or the
   layer file, and the popup's Layer item cycles them. */
#define CBVMC_LAYER_USA 0u          /* MemoryCardA.USA.raw, Windows-1252 */
#define CBVMC_LAYER_EUR 1u          /* MemoryCardA.EUR.raw, Windows-1252 */
#define CBVMC_LAYER_JAP 2u          /* MemoryCardA.JAP.raw, Shift-JIS */
#define CBVMC_LAYER_FILE "vmc_region.txt"
#define CBVMC_DIR_DEFAULT "/cubeboot/saves"

#endif
