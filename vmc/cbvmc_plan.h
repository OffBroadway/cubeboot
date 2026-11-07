/* Copyright (C) 2026 Nigzel / Bjerreman. SPDX-License-Identifier: GPL-2.0-only */
#ifndef CBVMC_PLAN_H
#define CBVMC_PLAN_H

/*
 * Memory plan of the loader build (docs/virtual_memory_card.md).  Two images, two lifetimes:
 *
 *   RESIDENT  (cbvmc_resident*.c + the store layer): lives for the whole game. Sits in the top of MEM1 that Cubeboot takes out of the
 *             arena for it (patch space grows by CBVMC_R_SIZE), above the FST, so ArenaHi, the FST and the heaps move down by exactly
 *             CBVMC_R_SIZE bytes and by nothing else.
 *   INSTALL   (cbvmc_install_split.c, the model copy used for validation, the word table, the blob image, the 40 KB installer stack):
 *             runs once inside the game's OSInit and is then dead.  It is staged INSIDE the arena (below bi2), so it costs the game
 *             nothing: OSInit's ClearArena zeroes it right after and the heap allocators hand it out like any other arena byte.
 *
 * The addresses are fixed at link time (both images are position dependent); the loader refuses when the game's bi2/FST would reach them.
 * CBVMC_R_SIZE must cover the resident image including its bss and the handler stack; build_blob.sh checks it and prints the slack.
 */
/* Two variants of the resident image: production (counters only) and the diagnosis build (-DCBVMC_DIAG: event ring, trace file with the reference game's state; about 11 KB more heap).
   Every unit that includes this file must be compiled with the same CBVMC_DIAG setting (build_blob.sh and the build scripts do it). */
/* T2 (cbvmc_t2.h, 2026-10-14): the scanner, the word applier, the breakpoint handler, the 1.5 KiB snapshot of the blob's vector ranges and a 3 KiB handler
   stack are resident now: 0x8200 -> 0xBC00 (production, 47,168 B used) and 0xAC00 -> 0xE200 (diagnosis, 56,768 B used).  The reference game's FST then starts at
   0x817ECCC0 (production) / 0x817EA6C0 (diagnosis), both above the generic exploit layout's list end 0x817E3700 (CUBEBOOT_VMC_NEXT.md 2). */
/* Build 3: + the freeze diagnostics line (counters 512 B) -> 0xC800 / 0xE600 (about 1 KiB slack each); the reference game's FST 0x817EC4C0 (production), still above 0x817E3700. */
#define CBVMC_R_SIZE_PROD   0xC800u
#define CBVMC_R_SIZE_DIAG   0xE600u
#if defined(CBVMC_DIAG) || defined(CBVMC_STALL)     /* CBVMC_STALL: production + the stall snapshot (build with CBVMC_DIAG=2) */
#define CBVMC_R_SIZE        CBVMC_R_SIZE_DIAG
#else
#define CBVMC_R_SIZE        CBVMC_R_SIZE_PROD
#endif                                                      /* resident region, bytes (32-aligned) */
#define CBVMC_R_BASE        (0x81800000u - 0x200u - CBVMC_R_SIZE)
#define CBVMC_B_BASE        0x817B0000u                     /* install image + installer stack, inside the arena */
#define CBVMC_B_SIZE        0x10000u
/* Hook block, the last 4 KiB of the install region (written by the loader, read by the install image and its trampoline; the installer stack ends below it):
     +0 magic CBVMC_HOOKBLK_MAGIC, +4 address of the hooked `bl`, +8 original target of that `bl`, +12 number of words, +16 words {addr, orig, patched} x n.
   Without the magic the install image refuses (no table): every loader writes the block. */
#define CBVMC_HOOKBLK       (CBVMC_B_BASE + 0xF000u)
#define CBVMC_HOOKBLK_MAGIC 0x4342484Bu
#define CBVMC_HOOKBLK_MAXW  64u

#endif
