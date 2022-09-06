#define _LANGUAGE_ASSEMBLY
#include "asm.h"
#include "patch_asm.h"

// TODO: install a pre_main function for init

patch_inst_ntsc "_stub_dvdwait" 0x00000000 0x8130108c 0x81301440 0x81301444 nop
patch_inst_pal  "_stub_dvdwait" 0x8130108c 0x8130108c 0x813011f8 nop

patch_inst_ntsc "_replace_bs2tick" 0x81300a70 0x81300968 0x81300d08 0x81300d0c b bs2tick
patch_inst_pal  "_replace_bs2tick" 0x81300968 0x81300968 0x81300ac0 b bs2tick

patch_inst_ntsc "_patch_cube_init" 0x8130e9b4 0x8130ebac 0x8130ef20 0x8130ef38 bl pre_cube_init
patch_inst_pal  "_patch_cube_init" 0x8130f2c8 0x8130ead8 0x8130f408 bl pre_cube_init

patch_inst_ntsc "_replace_bs2start" 0x813023e0 0x813021e8 0x81302590 0x813025a8 bl bs2start
patch_inst_pal  "_replace_bs2start" 0x813021e8 0x813021e8 0x8130235c bl bs2start
