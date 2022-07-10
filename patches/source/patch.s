#define _LANGUAGE_ASSEMBLY
#include "asm.h"

.macro  patch_inst name, addr, inst:vararg
.section .patch.\name\()_func
	.set \name\()_address, \addr
	.globl \name\()_address
	.globl \name
\name:
	\inst
.endm

.macro  patch_routine name, addr
.section .patch.\name\()_func
	.set \name\()_address, \addr
	.globl \name\()_address
	.globl \name
\name:
.endm

patch_inst "_stub_dvdwait" 0x8130108c nop
// patch_inst "_stub_memcard" 0x813010ec nop
patch_inst "_replace_bs2tick" 0x81300968 b bs2tick
patch_inst "_patch_cube_init" 0x8130ebac bl pre_cube_init
// patch_inst "_disable_menu_entry" 0x8130c990 li r3, 1
// patch_inst "_disable_controls" 0x8130c950 nop

patch_routine "_replace_bs2start" 0x81300888
	bl bs2start
	b _addr_8130089c // restore stack and return

// patch_inst "_stub_menu_load" 0x81302200 blr
// patch_inst "_stub_splash_anim" 0x8130d4d4 nop

// patch_inst "_hide_cube_draw" 0x81310790 nop
// patch_inst "_hide_final_cube" 0x8130d50c nop
// patch_inst "_hide_shadow_draw" 0x81310340 nop
// patch_inst "_hide_inner_draw" 0x8130d428 nop
// patch_inst "_hide_outer_draw_node_2" 0x8130d4a4 nop
// patch_inst "_hide_outer_draw_node_5" 0x8130d52c nop
// patch_inst "_hide_nin_draw" 0x8130d3d0 nop

// patch_inst "_change_scale" 0x8130bf0c lfs fr0,-0x7e74(r2)
// patch_inst "_change_logo_orientation" 0x8130bde4 li r5, -0x2000
