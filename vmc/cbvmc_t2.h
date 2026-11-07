/* Copyright (C) 2026 Nigzel / Bjerreman. SPDX-License-Identifier: GPL-2.0-only */
#ifndef CBVMC_T2_H
#define CBVMC_T2_H

/*
 * T2: re-apply the generic rule set to EVERY executable the console starts in one session (docs/virtual_memory_card.md and 8).  Original work.
 *
 * Trigger.  After the first install (cbvmc_install_split.c, inside the first executable's OSInit) the resident arms the Gekko data breakpoint on the doubleword
 * of the DSI vector (DABR = 0x80000300 | DW | BT).  Any later executable that brings up the SDK OS runs OSExceptionInit, which copies its exception stub
 * over 0x80000300 with a byte memcpy; the first store traps (DSI, DSISR 0x02400000, DAR in 0x80000300..307, console-proven) into our still installed vector.
 * cbvmc_vec.S routes it here (virtual mode, EE off, the resident handler stack).
 *
 * Per trap: the store is decoded (cbvmc_decode) and SWALLOWED: SRR0 advances, an update form gets its base register written (stbu/stwu/sthu and the X forms).
 * Bytes for 0x300..0x303 (our `ba cbvmc_vec`) go into the chain word instead (cbvmc_chain[0]: the replaced first word of the NEW stub, executed for every DSI
 * that is not ours), bytes for 0x304..0x307 and anything outside the doubleword are written for real.  The first trap of a new executable additionally:
 * commits the write-back window, puts the EXI channel model(s) back to the install state, derives the scan window from the new executable's registers
 * ([0x80003100, min(r1, r13)): its stack pointer and small-data base, both set by its crt0 before OSInit; text, rodata and data lie below both in every SDK
 * image of the corpus), runs the resident scanner (CBVMC_SCAN_IDEMPOTENT) and applies the table all-or-nothing.  A refusal patches nothing: the executable
 * runs without the virtual card (keep-alive: vector chained, breakpoint armed for the next one).  After the eighth byte the blob is flagged for repair: the
 * rest of OSExceptionInit rewrites the vectors 0xC00, 0xD00, 0xF00, 0x1300, 0x1400 and 0x1700 that lie inside the blob, and the next entry into cbvmc_vec
 * (which itself lives outside those ranges) copies them back from the resident snapshot before it does anything else.
 *
 * Everything that touches the CPU goes through struct cbvmc_t2_env, so the same code runs on the host against a RAM image with synthetic DSI frames
 * (tests/cbvmc_t2_test.c).
 */
#include <stdint.h>

#include "cbvmc_rt.h"
#include "cbvmc_scan.h"

#define CBVMC_T2_DSISR_DABR   0x00400000u           /* DSISR bit 9: data address breakpoint match (console: 0x02400000 for a store) */
#define CBVMC_T2_DABR_VALUE   0x80000306u           /* doubleword 0x80000300, DW (write) | BT (translated accesses) */
#define CBVMC_T2_VEC          0x80000300u
#define CBVMC_T2_NRANGES      6u
#define CBVMC_T2_RANGE_BYTES  0x100u                /* an SDK exception stub is 0x98 bytes; the whole 256 is kept for slack */
#define CBVMC_T2_SNAP_BYTES   (CBVMC_T2_NRANGES * CBVMC_T2_RANGE_BYTES)
#define CBVMC_T2_SCAN_LO      0x80003100u           /* first text address of every SDK executable (.init) */
/* Build 2 (console result 1, 2026-10-14): only a store whose pc lies in plausible game text starts a new executable (scan, commit, EXI reset).  The SDK
   loads the boot/reboot code at the apploader address 0x81200000 and above (apploader 0x81200000, the reboot program at 0x81300000, our resident and
   Cubeboot's patch area at the top of MEM1); game executables are linked below it (corpus: every text section ends below 0x80700000).  A trap from
   anywhere else is ALLOWED: its bytes are routed exactly like a game's (0x300..0x303 into the chain word so our vector stays, 0x304..0x307 written),
   the vector repair is flagged after its eighth byte, and nothing is scanned, committed or reset. */
#define CBVMC_T2_TEXT_HI      0x81200000u
#define CBVMC_T2_WINDOW_MAX   0x00800000u           /* scan budget: a window larger than 8 MiB is refused (KEEP), never scanned (20 MiB took 1.02 s on the console) */
#define CBVMC_T2_STAGE_SPAN   0x100u                /* traps whose pc is further apart than this from the first one belong to another copy routine */
#define CBVMC_T2_LOGN         8u
#define CBVMC_T2_MAGIC        0x43425432u           /* 'CBT2' */

/* the vectors an SDK OSExceptionInit rewrites that lie inside the runtime blob (0x80000C00..0x80003000) */
extern const uint32_t cbvmc_t2_ranges[CBVMC_T2_NRANGES];

/* a DSI that is the breakpoint on the vector doubleword (any of the 0x0/0x8/0xC segment aliases) and not an EXI trap */
#define CBVMC_T2_MATCH(dar, dsisr) (((dsisr) & CBVMC_T2_DSISR_DABR) != 0u && ((dar) & 0x3FFFFFF8u) == 0x300u)

struct cbvmc_t2_env {
    uint8_t *mem;                       /* game memory as seen at 0x80000000 (console: (uint8_t *)0x80000000) */
    uint32_t mem_size;                  /* 0x01800000 */
    volatile uint32_t *chain;           /* blob cbvmc_chain[0] */
    volatile uint32_t *fix_pending;     /* blob flag read by the real-mode entry */
    void (*flush)(void *c, uint32_t addr, uint32_t len);     /* data cache store + instruction cache invalidate for game addresses */
    void (*set_dabr)(void *c, uint32_t value);
    int (*sync)(void *c);               /* commit every dirty block of the write-back window(s); 0 = ok */
    void (*card_reset)(void *c);        /* EXI channel model(s) back to the state of the first install */
    void (*stage_note)(void *c, const char *text, unsigned len);   /* optional: per-executable report (status file) */
    uint32_t (*now)(void *c);           /* optional: time base */
    void *c;
};

enum cbvmc_t2_mode { CBVMC_T2_NONE = 0, CBVMC_T2_OWN = 1, CBVMC_T2_KEEP = 2, CBVMC_T2_ALLOW = 3 };
enum cbvmc_t2_why {                     /* why a stage runs without the card (KEEP) */
    CBVMC_T2_WHY_OK = 0,
    CBVMC_T2_WHY_WINDOW = 100,          /* r1/r13 give no plausible window */
    CBVMC_T2_WHY_APPLY = 101,           /* the table did not apply (an original word changed between scan and apply: cannot happen) */
    CBVMC_T2_WHY_TEXT = 102,            /* ALLOW: the store's pc is not game text (boot/reboot code at or above CBVMC_T2_TEXT_HI) */
    CBVMC_T2_WHY_PC = 103,              /* the store's pc lies outside the window r1/r13 give: not the executable those registers belong to */
    CBVMC_T2_WHY_BIG = 104              /* the window is larger than CBVMC_T2_WINDOW_MAX (scan budget) */
    /* 1..12: enum cbvmc_refuse of the scanner */
};

struct cbvmc_t2_stage {
    uint32_t index, srr0, r1, r13, lo, hi, words, hook, why, ticks, sync_rc, mode;
};

struct cbvmc_t2 {
    uint32_t magic;
    uint32_t armed;                     /* the breakpoint is ours and set */
    uint32_t active;                    /* a new executable's copy over 0x300..0x307 is in progress */
    uint32_t mask;                      /* bytes 0..7 of the doubleword stored in this stage */
    uint32_t shadow;                    /* the new stub's first word as collected (goes to the chain word) */
    uint32_t chain_done;
    uint32_t stage_srr0;
    uint32_t mode;                      /* enum cbvmc_t2_mode of the stage in progress / the last one */
    /* counters (production status) */
    uint32_t traps, stages, stages_own, stages_keep, unsupported, chain_bad, sync_fail, outside, stages_allow;
    struct cbvmc_t2_stage log[CBVMC_T2_LOGN];
    struct cbvmc_scan_result sr;        /* scratch: too big for the handler stack */
    struct cbvmc_t2_env env;
};

/* Fresh state; does not arm (cbvmc_t2_arm does). */
void cbvmc_t2_init(struct cbvmc_t2 *t, const struct cbvmc_t2_env *env);

/* Takes the snapshot of the six blob ranges into `snap` (CBVMC_T2_SNAP_BYTES, the copy the real-mode entry restores from) and sets the breakpoint. */
void cbvmc_t2_arm(struct cbvmc_t2 *t, uint8_t *snap);

/* One breakpoint trap (ctx as saved by cbvmc_vec.S).  Returns 0 when the store was swallowed (ctx->srr0 advanced), 1 when it could not be handled: the
   breakpoint is then cleared and ctx->srr0 left alone, so the store re-executes and completes (the executable runs stock from there on). */
int cbvmc_t2_trap(struct cbvmc_t2 *t, struct cbvmc_ctx *ctx);

/* The scan window the trap would use for these register values: 0 and [*lo, *hi) when plausible. */
int cbvmc_t2_window(uint32_t r1, uint32_t r13, uint32_t mem_top, uint32_t *lo, uint32_t *hi);

/* One EXI channel model back to the state of the first install (nothing selected, registers cleared, no latched interrupt).  The card behind it (image,
   unlock, status) is left as it is, like a real card that stays in its slot.  No-op for a channel without a port. */
void cbvmc_t2_exi_reset(struct cubeboot_vmc_exi *e);

/* Readable text for a `why` code (scanner refusals 1..12 and the CBVMC_T2_WHY_* codes). */
const char *cbvmc_t2_reason(uint32_t why);

/* One status line for stage log entry `s` (no libc); returns the length.  `cap` includes the terminator.
   Format: "stage N OWN|KEEP|ALLOW pc X r1 X r13 X window X-X words N hook X sync N ticks N: reason" */
unsigned cbvmc_t2_line(const struct cbvmc_t2_stage *s, char *out, unsigned cap);

#endif
