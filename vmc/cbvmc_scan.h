/* Copyright (C) 2026 Nigzel / Bjerreman. SPDX-License-Identifier: GPL-2.0-only */
#ifndef CBVMC_SCAN_H
#define CBVMC_SCAN_H

/*
 * Runtime scanner for the virtual card (docs/virtual_memory_card.md).  Finds, in a game image that is already in RAM at its final
 * addresses, the words the trap needs: the `lis rD,0xCC00` of the SDK's EXI library and interrupt dispatcher, the two card-library words
 * (VerifyID, DoMount flash-ID compare) and the install hook (the unique call of EXIInit).  Original work; the same algorithm as
 * tools/vmc_scan/vmc_scan.py, which the host test compares it with.  Freestanding, no libc, strict C89.  Reads only, never writes.
 *
 * The result is a word table (struct cbvmc_word of cbvmc_words.h) plus a per-function report, so that
 * a player's log says what was found and why a game was refused.
 */
#include <stdint.h>

#include "cbvmc_words.h"

#define CBVMC_SCAN_MAX_WORDS 64u
#define CBVMC_SCAN_MAX_RANGES 7u

struct cbvmc_range {
    uint32_t lo, hi;                /* [lo, hi) code section of the game DOL */
};

enum cbvmc_scan_func {
    CBVMC_F_EXI_LIB = 0,        /* trap sites of the EXI library (lis rD,0xCC00 reaching 0xCC006800..) */
    CBVMC_F_PI_CAUSE,           /* interrupt dispatcher read of the PI cause register */
    CBVMC_F_EXIINIT,            /* EXIInit function and its single call site (the install hook) */
    CBVMC_F_VERIFYID,           /* card header serial against the console flash ID (replaced by `li r3,0 ; blr`) */
    CBVMC_F_DOMOUNT,            /* DoMount SRAM flash-ID checksum compare (beq -> b) */
    CBVMC_F_COUNT
};

enum cbvmc_scan_status {
    CBVMC_S_NOT_FOUND = 0,
    CBVMC_S_FOUND = 1,          /* exactly the expected number of hits */
    CBVMC_S_AMBIGUOUS = 2       /* more hits than allowed: nothing is guessed */
};

/* Why a game is refused (first failing check, in this order).  The text of each is in the report (/cubeboot/vmc_scan.txt). */
enum cbvmc_refuse {
    CBVMC_R_NONE = 0,
    CBVMC_R_RANGE,              /* a code section overlaps the runtime blob (0x80000C00..0x80003000) or the staging area (0x817B0000..) */
    CBVMC_R_EXI_LIB,            /* fewer than 8 or more than 32 EXI library sites: no SDK-style EXI library found */
    CBVMC_R_PI_CAUSE,           /* no interrupt dispatcher read of the PI cause register */
    CBVMC_R_EXIINIT,            /* EXIInit not found (no function clears the status registers of all three channels) */
    CBVMC_R_EXIINIT_CALLS,      /* EXIInit is called from no place or from more than one place: no unique install point */
    CBVMC_R_SYSCALL,            /* the install point is not in an OSInit that has just run __OSInitSystemCall (the exception vectors are not set up) */
    CBVMC_R_VERIFYID,           /* card serial check (VerifyID) not found: the virtual card would be reported damaged */
    CBVMC_R_VERIFYID_AMBIG,     /* more than one VerifyID candidate */
    CBVMC_R_DOMOUNT,            /* DoMount flash-ID compare not found */
    CBVMC_R_DOMOUNT_AMBIG,      /* more than one DoMount candidate */
    CBVMC_R_MIXED,              /* a patched site also touches registers other than EXI/PI (would reroute them through the trap) */
    CBVMC_R_OVERFLOW,           /* word table too small */
    CBVMC_R_COUNT
};

#define CBVMC_SCAN_LIBOGC 1u     /* TESTS ONLY (cardtest.dol, libogc): no VerifyID, no __OSInitSystemCall, indexed addis sites may mix registers: those three refusals are skipped */
/* T2 re-scan (docs/virtual_memory_card.md): sites this rule set already patched count as found, so an image the console copies again
   from a buffer we patched earlier (a menu reloaded from RAM) gets the same table: `lis rD,0x0C00` reaching EXI/PI, VerifyID already `li r3,0 ; blr`,
   the DoMount compare already `b`/`nop`.  Their rows are no-ops (orig == patched).  Without the flag the scanner behaves exactly as before. */
#define CBVMC_SCAN_IDEMPOTENT 2u

struct cbvmc_scan_result {
    unsigned nwords;
    struct cbvmc_word words[CBVMC_SCAN_MAX_WORDS];       /* original/patched words (all classes), sorted by address, ready for cbvmc_words_apply */
    uint32_t hook_addr;             /* the `bl EXIInit` in OSInit (0 = none) */
    uint32_t hook_target;           /* EXIInit */
    unsigned serial;            /* 1 = the executable carries the card library's serial check (the LCG constant 0x41C64E6D as lis/ori or lis/addi): then
                                   VerifyID and the DoMount compare must be found and patched; 0 = no serial check anywhere, nothing to patch (EA builds,
                                   boot-only executables), so their absence is not a refusal */
    unsigned status[CBVMC_F_COUNT];
    unsigned count[CBVMC_F_COUNT];
    unsigned lis_sites;             /* all `lis rD,0xCC00` seen (information) */
    unsigned mixed_sites;           /* trap sites that also touch registers outside EXI/PI (information: patched as a whole) */
    unsigned dbg_sites;             /* EXI2 debugger-channel sites and their neighbours left real (information) */
    unsigned syscall_init;          /* 1 when the install point follows a call of __OSInitSystemCall within 0x100 bytes */
    unsigned refuse;                /* enum cbvmc_refuse, CBVMC_R_NONE when ok */
    int ok;                         /* 1 when every class was found and the table is usable */
};

/* image: the game's memory as it is at base; rng/nrng: the DOL's code sections (data sections are never scanned: data words can look like code) */
void cbvmc_scan(const uint8_t *image, uint32_t base, const struct cbvmc_range *rng, unsigned nrng, struct cbvmc_scan_result *r);
void cbvmc_scan_ex(const uint8_t *image, uint32_t base, const struct cbvmc_range *rng, unsigned nrng, unsigned flags, struct cbvmc_scan_result *r);

/* The per-game report as text (one block, ends with a newline, always terminated); returns its length (cap includes the terminator). */
unsigned cbvmc_scan_report(const struct cbvmc_scan_result *r, const struct cbvmc_range *rng, unsigned nrng, const char *game_id, char *out, unsigned cap);

#endif
