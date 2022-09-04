#ifndef __PATCH_ASM_H__
#define __PATCH_ASM_H__

#ifdef _LANGUAGE_ASSEMBLY

#define JOIN_(X,Y) X##Y
#define JOIN(X,Y) JOIN_(X,Y)

#define vNTSC_10(x) JOIN(JOIN(x, _VER_), NTSC_10)
#define vNTSC_11(x) JOIN(JOIN(x, _VER_), NTSC_11)
#define vNTSC_12(x) JOIN(JOIN(x, _VER_), NTSC_12)
#define vPAL_10(x) JOIN(JOIN(x, _VER_), PAL_10)
#define vPAL_11(x) JOIN(JOIN(x, _VER_), PAL_11)
#define vPAL_12(x) JOIN(JOIN(x, _VER_), PAL_12)

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

#endif		//_LANGUAGE_ASSEMBLY
#endif		//__PATCH_ASM_H__
