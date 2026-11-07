/* Copyright (C) 2026 Nigzel / Bjerreman. SPDX-License-Identifier: GPL-2.0-only */
#ifndef CBVMC_IMAGE_H
#define CBVMC_IMAGE_H

/*
 * Memory map of the test image's reserve: the bake reserve after the baked section (below ArenaLo, not file backed).
 *   +0x00000  handler stack (2 KB)
 *   +0x00800  CARD work area (0xA000, 32-byte aligned)
 *   +0x0A800  three 8 KB buffers (write pattern, read-back, scratch)
 *   +0x10800  RAM card image, 4 Mbit = 0x80000
 *   +0x91000  installer stack (40 KB), entered by cbvmc_entry
 *   +0x9B000  slot B RAM card image (4 Mbit), +0x11B000 its journal buffer (CBVMC_WB_JHDR + 8 KiB), +0x11D000 its CARD work area (0xA000)
 * Total 0x127000 (CBVMC_RSV_TOTAL); build_image.py passes it as --reserve.
 */
#ifndef __ASSEMBLER__
#include <stdint.h>
extern char __cbvmc_img_end[];
#endif
#ifdef CBVMC_LOADER
/* Loader build (docs/virtual_memory_card.md): the INSTALL image is staged inside the arena (cbvmc_plan.h) and is followed by the
   installer stack.  The handler stack and everything else that stays belongs to the resident image. */
#define CBVMC_RSV_ISTACK       0x00000u
#define CBVMC_ISTACK_BYTES     0x0A000u
#define CBVMC_RSV_TOTAL        0x0A000u
#else
#define CBVMC_RSV_STACK        0x00000u
#define CBVMC_RSV_STACK_BYTES  0x00800u
#define CBVMC_RSV_WA           0x00800u
#define CBVMC_WA_BYTES         0x0A000u
#define CBVMC_RSV_BUF          0x0A800u
#define CBVMC_BUF_BYTES        0x02000u
#define CBVMC_RSV_IMAGE        0x10800u
#define CBVMC_IMAGE_BYTES      0x80000u
#define CBVMC_RSV_ISTACK       0x91000u    /* installer stack: the card formatter keeps 8 KB blocks on the stack, more than OSInit's stack can spare */
#define CBVMC_ISTACK_BYTES     0x0A000u
#define CBVMC_RSV_IMAGE_B      0x9B000u
#define CBVMC_RSV_WAL_B        0x11B000u
#define CBVMC_RSV_WA_B         0x11D000u
#define CBVMC_RSV_TOTAL        0x127000u
#endif

#ifndef __ASSEMBLER__
static inline uint32_t cbvmc_reserve_base(void)
{
    return ((uint32_t)__cbvmc_img_end + 31u) & ~31u;
}
#endif

#endif
