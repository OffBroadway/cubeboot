#ifndef __PATCH_ASM_H__
#define __PATCH_ASM_H__

#define _LANGUAGE_ASSEMBLY
#ifdef _LANGUAGE_ASSEMBLY

#define asm(...) #__VA_ARGS__
#define CONST(...) __VA_ARGS__

#define vNTSC_10(x) x##_VER_NTSC_10
#define vNTSC_11(x) x##_VER_NTSC_11
#define vNTSC_12_001(x) x##_VER_NTSC_12_001
#define vNTSC_12_101(x) x##_VER_NTSC_12_101
#define vPAL_10(x) x##_VER_PAL_10
#define vPAL_11(x) x##_VER_PAL_11
#define vPAL_12(x) x##_VER_PAL_12

#define ntsc10_sym(x) ntsc10_##x
#define ntsc11_sym(x) ntsc11_##x
#define ntsc12_001_sym(x) ntsc12_001_##x
#define ntsc12_101_sym(x) ntsc12_101_##x
#define pal10_sym(x) pal10_##x
#define pal11_sym(x) pal11_##x
#define pal12_sym(x) pal12_##x

.set vNTSC_10(_SDA_BASE_), 0x81465320
.set vNTSC_11(_SDA_BASE_), 0x81489120
.set vNTSC_12_001(_SDA_BASE_), 0x8148b1c0
.set vNTSC_12_101(_SDA_BASE_), 0x8148b640
.set vPAL_10(_SDA_BASE_), 0x814b4fc0
.set vPAL_11(_SDA_BASE_), 0x81483de0
.set vPAL_12(_SDA_BASE_), 0x814b7280

// helpers
.macro rept_inst count, inst:vararg
.rept \count
    \inst
.endr
.endm

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


.macro patch_inst_ntsc name:req, addr_ntsc10, addr_ntsc11, addr_ntsc12_001, addr_ntsc12_101, inst:vararg
.if \addr_ntsc10
patch_inst vNTSC_10(\name\()) \addr_ntsc10 \inst
.endif

.if \addr_ntsc11
patch_inst vNTSC_11(\name\()) \addr_ntsc11 \inst
.endif

.if \addr_ntsc12_001
patch_inst vNTSC_12_001(\name\()) \addr_ntsc12_001 \inst
.endif

.if \addr_ntsc12_101
patch_inst vNTSC_12_101(\name\()) \addr_ntsc12_101 \inst
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


.macro patch_inst_global name:req, addr, inst:vararg
patch_inst_ntsc \name \addr \addr \addr \addr \inst
patch_inst_pal \name \addr \addr \addr \inst
.endm


// expand

.macro expand_after_call name:req, addr, orig, func
// patch_inst 

patch_inst \name \addr nop

\name\()_after_expander:
	bl \orig
	bl \func
	b _addr_op_\addr\()_plus_4
.endm


// expand_with_skip

#endif		//_LANGUAGE_ASSEMBLY
#endif		//__PATCH_ASM_H__
