/* SPDX-License-Identifier: GPL-2.0-only */
/* Entry table of the IPL resident image (cbvmc_ipl_plan.h): two branches at the very start of the image, so the Cubeboot patch needs no symbol addresses.
     +0  cbvmc_ipl_install_c(struct cbvmc_ipl_args *)   installer (cbvmc_install_ipl.c)
     +4  cbvmc_ipl_finish_c(void)                       hand-over to a game (cbvmc_resident_ipl.c) */
        .section .text.cbvmc_ipl_entry,"ax"
        .globl  cbvmc_ipl_tbl
cbvmc_ipl_tbl:
        b       cbvmc_ipl_install_c
        b       cbvmc_ipl_finish_c
        b       cbvmc_ipl_replug_c
