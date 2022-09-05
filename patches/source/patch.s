#define _LANGUAGE_ASSEMBLY
#include "asm.h"
#include "patch_asm.h"

patch_inst_ntsc "_stub_something" 0x81761010 0x81761020 0x81761030 nop
// patch_inst_pal "_stub_something" 0x81401040 0x81401050 0x81401060 nop

patch_inst vNTSC_11(_stub_dvdwait) 0x8130108c nop
patch_inst vNTSC_11(_replace_bs2tick) 0x81300968 b bs2tick
patch_inst vNTSC_11(_patch_cube_init) 0x8130ebac bl pre_cube_init

patch_routine vNTSC_11(_replace_bs2start) 0x81300888
	bl bs2start
	b _addr_8130089c // restore stack and return
