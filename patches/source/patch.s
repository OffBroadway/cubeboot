#define _LANGUAGE_ASSEMBLY
#include "asm.h"
#include "patch_asm.h"

patch_inst_ntsc "_stub_dvdwait" 0x0 0x8130108c 0x0 nop

patch_inst_ntsc "_replace_bs2tick" 0x81300a70 0x81300968 0x0 b bs2tick

patch_inst_ntsc "_patch_cube_init" 0x8130e9b4 0x8130ebac 0x0 bl pre_cube_init

patch_inst_ntsc "_replace_bs2start" 0x813023e0 0x813021e8 0x0 bl bs2start
