#ifndef __PATCH_ASM_H__
#define __PATCH_ASM_H__

#ifdef _LANGUAGE_ASSEMBLY

#define asm(...) #__VA_ARGS__

#define vNTSC_10(x) x##_VER_NTSC_10
#define vNTSC_11(x) x##_VER_NTSC_11
#define vNTSC_12(x) x##_VER_NTSC_12
#define vPAL_10(x) x##_VER_PAL_10
#define vPAL_11(x) x##_VER_PAL_11
#define vPAL_12(x) x##_VER_PAL_12

.macro patch_routine name, addr
.section .patch.\name\()_func
	.set \name\()_address, \addr
	.globl \name\()_address
	.globl \name
\name:
.endm


.macro patch_inst name, addr, inst:vararg
patch_routine \name, \addr
	\inst
.endm


.macro patch_inst_ntsc name:req, addr_ntsc10, addr_ntsc11, addr_ntsc12, inst:vararg
.if \addr_ntsc10
patch_inst vNTSC_10(\name\()) \addr_ntsc10 \inst
.endif

.if \addr_ntsc11
patch_inst vNTSC_11(\name\()) \addr_ntsc11 \inst
.endif

.if \addr_ntsc12
patch_inst vNTSC_12(\name\()) \addr_ntsc12 \inst
.endif
.endm


.macro patch_inst_pal name:req, addr_pal10, addr_pal11, addr_pal12, inst:vararg
.if \addr_pal10
patch_inst vPAL_10(\name\()) \addr_pal10 \inst
.endif

.if \addr_pal11
patch_inst vPAL_11(\name\()) \addr_pal11 \inst
.endif

.if \addr_pal12
patch_inst vPAL_12(\name\()) \addr_pal12 \inst
.endif
.endm

.macro patch_inst_all name:req, addr_ntsc10, addr_ntsc11, addr_ntsc12, addr_pal10, addr_pal11, addr_pal12, inst:vararg
patch_inst_ntsc \name \addr_ntsc10 \addr_ntsc11 \addr_ntsc12 \inst
patch_inst_pal \name \addr_pal10 \addr_pal11 \addr_pal12 \inst
.endm

#endif		//_LANGUAGE_ASSEMBLY
#endif		//__PATCH_ASM_H__
