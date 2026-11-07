/* Copyright (C) 2026 Nigzel / Bjerreman. SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef CBVMC_WBSTORE_H
#define CBVMC_WBSTORE_H

/*
 * Write-coalescing store for the virtual card (docs/virtual_memory_card.md).  Original work.
 *
 * Why: the card model writes an immediate-mode page program ONE BYTE per store call (cubeboot_vmc.c select(): card_write(..., 1) in a
 * loop), a DMA page program as one 128-byte call, an erase as sixteen 512-byte calls, and asks for a flush after every command.  On the Flippy file API (reopen flush policy)
 * one 8 KiB block save is 338 DI commands (measured on the simulated drive, tests/cbvmc_wb_test.c).  This wrapper sits between the model and any lower struct cubeboot_vmc_store and keeps
 * ONE erase block (8 KiB, aligned) of the card file in a caller-owned buffer:
 *
 *   - writes and reads inside the window are served from it (a partial write to a sector not yet loaded loads that 512-byte sector);
 *   - the window is committed to the lower store in ascending order, as at most one write per run of dirty sectors, followed by ONE
 *     lower flush, when (a) every 128-byte page of the block has been programmed since the last erase and the model flushes
 *     (the normal end of a block save), (b) another window is needed, (c) any non-card file operation (create/rename/remove/format)
 *     comes, (d) cbvmc_wb_sync() is called (reset, shutdown, error UI), (e) cbvmc_wb_tick() has seen `idle_ticks` calls without a
 *     write (a partial block left behind).
 *   - the model's flush() is answered "ok" without I/O unless (a) holds.
 *
 * Power loss: data is exposed only between the first write into a window and its commit, i.e. during a block erase+program, which
 * is also not atomic on a real card.  A commit that fails keeps the window dirty (sticky error, retried by the next trigger).
 * The card must already be a complete file (the model formats into "<path>.tmp" and renames; those names pass straight through).
 * Files whose size is not a multiple of 512 bytes, or writes that append, pass through unbuffered.
 */
#include <stdint.h>

#include "cubeboot_vmc.h"

#define CBVMC_WB_WINDOW  8192u
#define CBVMC_WB_SECTOR  512u
#define CBVMC_WB_PAGE    128u
#define CBVMC_WB_SCRATCH 2048u

/*
 * Write-safe commit (owner decision 2026-10-09: safety over speed).  A block commit is
 *   1. journal: the new 8 KiB block + a header {magic, card offset, CRC32 of the data} into "<path>.wal" (data first, header last),
 *      lower flush;  the header is only valid if its CRC matches the data, so a torn journal is simply ignored;
 *   2. the card file: ascending dirty runs inside the window only (no other block is ever written), lower flush;
 *   3. read-back: the whole window is read from the lower store and compared with the RAM copy (on the Flippy store the flush of 2
 *      reopened the file, so this reads what the drive holds);  a mismatch is a failed commit;
 *   4. the journal header is zeroed, lower flush.
 * After a power cut cbvmc_wb_recover() (call it once after init, before the card model opens the file) replays a valid journal into the
 * card file, verifies, and clears it.  So a block is either entirely old or entirely new after recovery, unless the drive itself
 * lies about flush (not measurable here).  A failed step keeps the window dirty (sticky error); the next trigger retries from step 1,
 * at most CBVMC_WB_RETRIES times per trigger.  Without a journal path (NULL) steps 1 and 4 are skipped: verify still runs, but a power
 * cut in step 2 can tear the block (never any other block).
 * Build options: -DCBVMC_WB_NO_VERIFY (drop step 3), -DCBVMC_WB_PAGE_COMMIT (every model flush commits, i.e. per-page mode).
 */
#define CBVMC_WB_RETRIES 2u
#define CBVMC_WB_JHDR    16u

struct cbvmc_wb_stats {
    uint32_t lower_reads, lower_writes, lower_flushes;
    uint32_t commits;               /* window commits that wrote something */
    uint32_t commits_complete;      /* ... because the block was complete */
    uint32_t commits_switch;        /* ... because another window / file operation came */
    uint32_t commits_sync;          /* ... explicit sync */
    uint32_t commits_idle;          /* ... idle ticks */
    uint32_t commit_failures;
    uint32_t verify_reads, verify_mismatches, journal_writes, recovered;
    uint32_t bytes_to_lower;
    uint32_t sector_loads;
    uint32_t model_writes, model_reads, model_flushes;
    uint32_t read_retries;          /* lower reads that failed and were repeated */
};

struct cbvmc_wb {
    struct cubeboot_vmc_store lower;
    char path[CUBEBOOT_VMC_PATH_MAX];
    char jpath[CUBEBOOT_VMC_PATH_MAX];  /* "" = no journal */
    uint8_t jready;                     /* journal file exists with its header and a data area */
    uint8_t scratch[CBVMC_WB_SCRATCH];  /* read-back chunk */
    uint8_t *win;                   /* CBVMC_WB_WINDOW bytes, caller owned */
    uint32_t base;
    uint32_t file_size;             /* of the card file; 0 = not known yet */
    uint32_t idle_ticks;            /* ticks without a write before a partial window is committed */
    uint32_t quiet;
    uint16_t valid, dirty;          /* one bit per 512-byte sector of the window */
    uint64_t programmed;            /* one bit per 128-byte page programmed since the window was last erased */
    uint8_t active;
    uint8_t err;                    /* last commit failed */
    struct cbvmc_wb_stats stats;
    /* diagnostics (NULL = off): called once per commit attempt sequence with the window base, the dirty sector mask, the result (0 = ok) and the trigger (0 complete, 1 switch, 2 sync, 3 idle) */
    void (*on_commit)(const struct cbvmc_wb *wb, uint32_t base, uint16_t dirty, int result, int why);
};

/* Wraps `lower` (copied) for the card file `path`; `out` is the store the model uses.  `journal` is "<path>.wal" or NULL.  Returns 0. */
int cbvmc_wb_init(struct cbvmc_wb *wb, struct cubeboot_vmc_store *out, const struct cubeboot_vmc_store *lower,
                  const char *path, const char *journal, uint8_t *window, uint32_t idle_ticks);

/* Points the window at another card file (and its journal): commits what the old file left dirty first (<0 = that failed, nothing changed),
   then forgets the window.  This is how two virtual cards share ONE window (cbvmc_wbmux.h). */
int cbvmc_wb_rebind(struct cbvmc_wb *wb, const char *path, const char *journal);

/* Replays a valid journal (a commit that a power cut interrupted).  0 = nothing to do or replayed and verified, <0 = failed (card untouched
   beyond the journalled block; the journal stays for the next attempt). */
int cbvmc_wb_recover(struct cbvmc_wb *wb);

/* Makes the journal file complete (header + 8 KiB data area, zero-filled) NOW, so that no commit ever has to allocate space on the SD (a full FAT volume
   is found at install, not in the middle of a save).  0 on success or when there is no journal, <0 when the file cannot be created or grown. */
int cbvmc_wb_prepare_journal(struct cbvmc_wb *wb);

/* Commits everything dirty (0 on success or nothing to do).  For reset / shutdown / error paths. */
int cbvmc_wb_sync(struct cbvmc_wb *wb);

/* Called from a periodic event (the PI interrupt-cause read): commits a partial window after `idle_ticks` quiet ticks. */
void cbvmc_wb_tick(struct cbvmc_wb *wb);

int cbvmc_wb_dirty(const struct cbvmc_wb *wb);
/* forget the window (after a sync): the next access reloads from the lower store.  For a caller that wrote the card file directly. */
void cbvmc_wb_discard(struct cbvmc_wb *wb);

#endif
