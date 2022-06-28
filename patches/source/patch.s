#define _LANGUAGE_ASSEMBLY
#include "asm.h"

.extern _start

.macro  patch_inst name, addr, inst:vararg
.section .patch.\name\()_func
	.set \name\()_address, \addr
	.globl \name\()_address
	.globl \name
\name:
	\inst
.endm

patch_inst "_stub_dvdwait" 0x8130108c nop
patch_inst "_stub_bs2tick" 0x81300968 b _start
