/* Copyright (C) 2026 Nigzel / Bjerreman. SPDX-License-Identifier: GPL-2.0-only */
/* cbvmc_tramp: the entry of the install image.  Original work.
     cbvmc_entry replaces the `bl` the scanner chose in the game's OSInit (the call right after `bl __OSInitSystemCall`): it runs the installer
     on its own stack, then re-issues the replaced call (LR = hook + 4, jump to the call's original target) from the hook block the loader wrote. */
#include "cbvmc_image.h"
        .section .text.cbvmc_entry,"ax"
        .globl  cbvmc_entry
cbvmc_entry:
        /* the installer runs on its own stack in the reserve (CBVMC_RSV_ISTACK): the formatter needs about 30 KB */
        lis     r11, __cbvmc_img_end@ha
        addi    r11, r11, __cbvmc_img_end@l
        addi    r11, r11, 31
        rlwinm  r11, r11, 0, 0, 26                      /* reserve base */
        addis   r11, r11, (CBVMC_RSV_ISTACK + CBVMC_ISTACK_BYTES)@ha
        addi    r11, r11, (CBVMC_RSV_ISTACK + CBVMC_ISTACK_BYTES)@l
        addi    r11, r11, -32
        stw     r1, 0(r11)                              /* back chain = the game's stack */
        mr      r1, r11
        bl      cbvmc_install_c
        lwz     r1, 0(r1)
#ifdef CBVMC_LOADER
        /* hook block (cbvmc_plan.h CBVMC_HOOKBLK = 0x817BF000): +4 hooked `bl`, +8 its original target */
        lis     r10, 0x817C
        addi    r10, r10, -0x1000           /* 0x817BF000 */
        lwz     r11, 0(r10)
        lis     r12, 0x4342
        ori     r12, r12, 0x484B            /* CBVMC_HOOKBLK_MAGIC */
        cmpw    r11, r12
        bne-    1f
        lwz     r11, 4(r10)
        addi    r11, r11, 4                 /* hook + 4: where the replaced call returns to */
        mtlr    r11
        lwz     r12, 8(r10)                 /* the replaced call's target */
        mtctr   r12
        bctr
1:      b       1b                          /* no hook block: cannot happen (the loader always writes one) */
#endif

/* cbvmc_osreport: the install image has no OSReport to call in a game it does not know (there is no fixed address in any game), so this is a
   no-op; the same information goes to the status file (cbvmc_resident_note) and the loader's report. */
        .text
        .globl  cbvmc_osreport
cbvmc_osreport:
        blr
