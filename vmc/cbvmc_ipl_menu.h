/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * cbvmc_ipl_menu: the Memory Card page popup, extended (docs/virtual_memory_card.md).  Original work.
 *
 * The IPL's own popup (Move / Copy / Erase) is reused: it also opens on an EMPTY slot, and it always shows our items below the stock ones
 * (dim when the action is unavailable): Undo (one action back on this card: the five system blocks are snapshotted to <card>.undo before
 * Erase / Format / Import, and before Move / Copy on every virtual card they touch, and written back), Import (every .gci and every card
 * image .raw / .gcp in /cubeboot/saves/import, moved to /cubeboot/saves/imported afterwards), Swap Slot (the two virtual cards change place:
 * their files are renamed and both cards re-plugged), Format (the IPL's own format worker behind its own Yes/No; the text encoding follows the card's region;
 * the result message closes by itself).  The confirmation is the IPL's own Yes/No column.
 *
 * The page code is found by masked signatures (cbvmc_ipl_menu_sigs.h) in all known builds (USA, JAP, PAL, PAL 1.2); about two dozen single
 * instructions become `bl` into stubs of this image (cbvmc_ipl_menu_stubs.S), each stub re-doing the instruction it replaced.  When any signature does not
 * match exactly once, nothing is patched and the page is the stock one.
 *
 * Portable part (host test): the card-image operations behind a store + a small file-system interface.
 */
#ifndef CBVMC_IPL_MENU_H
#define CBVMC_IPL_MENU_H

#include <stdint.h>
#include "cubeboot_vmc.h"

#define CBVMC_MENU_IMPORT_DIR   "/cubeboot/saves/import"
#define CBVMC_MENU_IMPORTED_DIR "/cubeboot/saves/imported"
#define CBVMC_MENU_UNDO_SUFFIX  ".undo"
#define CBVMC_MENU_SYSBLOCKS    5u                  /* header, directory x2, block map x2: all Erase and Format ever rewrite */
#define CBVMC_MENU_BLOCK        8192u
#define CBVMC_MENU_CHUNK        8192u               /* one Flippy command moves 8 KiB; every access costs an open + transfer + close, so whole blocks */

/* SD-side file access of the extension: the plain files of a directory, a directory to create, and the card store for the files themselves */
struct cbvmc_menu_fs {
    void *ctx;
    int (*list)(void *ctx, const char *dir, int (*cb)(void *cbctx, const char *name), void *cbctx);   /* >= 0 files listed, < 0 error; cb returns 0 to go on */
    int (*mkdir)(void *ctx, const char *path);                                                          /* EXISTS is fine (0) */
    const struct cubeboot_vmc_store *files;
};

/* One slot's card as the extension sees it */
struct cbvmc_menu_card {
    const struct cubeboot_vmc_store *store;     /* the card image (the window store of the resident) */
    const char *path;
    struct cubeboot_vmc_card *model;            /* its model (read cache dropped after a direct write); may be NULL */
    uint8_t is_virtual;                         /* 0: a physical card is in the slot (no image: Import / Undo not offered, no snapshot) */
};

void cbvmc_menu_bind_fs(const struct cbvmc_menu_fs *fs);
void cbvmc_menu_set_dir(const char *dir);                 /* the card folder: import / imported live inside it (defaults: the built-in folder) */
const char *cbvmc_menu_import_dir(void);
const char *cbvmc_menu_imported_dir(void);
void cbvmc_menu_bind_card(unsigned slot, const struct cbvmc_menu_card *card);

/* results */
enum {
    CBVMC_MENU_OK = 0,
    CBVMC_MENU_ERR_SLOT = -1, CBVMC_MENU_ERR_IO = -2, CBVMC_MENU_ERR_NOFS = -3, CBVMC_MENU_ERR_PHYSICAL = -4, CBVMC_MENU_ERR_NOUNDO = -5,
    CBVMC_MENU_ERR_CORRUPT = -6, CBVMC_MENU_ERR_FULL = -7, CBVMC_MENU_ERR_BADGCI = -8, CBVMC_MENU_ERR_DUP = -9, CBVMC_MENU_ERR_NOENTRY = -10
};

int cbvmc_menu_snapshot(unsigned slot);                 /* blocks 0..4 -> <card>.undo (virtual cards) */
int cbvmc_menu_undo_available(unsigned slot);           /* 1 when <card>.undo exists with the right size */
int cbvmc_menu_undo(unsigned slot);                     /* <card>.undo -> blocks 0..4, the file removed */
int cbvmc_menu_swap_available(void);                    /* 1 when both slots are served by a virtual card */
int cbvmc_menu_swap(void);                              /* Swap Slot: the two card files (and their .undo snapshots) change place */
/* every .gci of the import directory into the card: *done / *skipped counted (skipped: duplicate, bad file, no room); the imported files move */
int cbvmc_menu_import(unsigned slot, unsigned *done, unsigned *skipped);
int cbvmc_menu_import_one(unsigned slot, const char *path);     /* one .gci by path (not moved) */
int cbvmc_menu_import_image(unsigned slot, const char *path, unsigned *done, unsigned *skipped);   /* every save of a card image by path (not moved) */

/* console: install the patches into BS2 (text scanned in [lo, hi)); returns the number of words patched (>0) or a negative reason; `why` gets a short text */
int cbvmc_menu_install(uint32_t lo, uint32_t hi, const char **why);
/* what the install found (for the status file) */
struct cbvmc_menu_found { uint32_t browse, measure, draw, popup, confirm, message, pdraw, format_async, req_format, close_popup, slot_table, sound, queue, send_message; int32_t sda_delta; unsigned enc_variant; };
const struct cbvmc_menu_found *cbvmc_menu_found(void);

#endif
