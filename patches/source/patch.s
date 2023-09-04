#define _LANGUAGE_ASSEMBLY
#include "asm.h"
#include "patch_asm.h"

// patch_inst vNTSC_11(_change_background_color) 0x81481cc8 .4byte 0xFFFF00FF
// patch_inst vNTSC_11(_no_background_color) 0x8137377c blr

patch_inst vNTSC_11(_gameselect_hide_cubes) 0x81327454 nop
patch_inst vNTSC_11(_gameselect_replace_draw) 0x81314518 bl mod_gameselect_draw
patch_inst vNTSC_11(_gameselect_replace_input) 0x81326f94 bl handle_gameselect_inputs
patch_inst vNTSC_11(_gameselect_replace_grid) 0x81327438 b ntsc11_sym(_gameselect_grid_helper)

.section .text
ntsc11_sym(_gameselect_grid_helper):
    bl ntsc11_sym(draw_grid) // original insr
    bl is_gameselect_draw
    mr r0, r3
    cmplwi r0, 1
    beq r0, 8 // skip next instr
    b _addr_813274fc // early exit
    b _addr_8132743c // continue

// expand_after_call vNTSC_11(_code_after_grid) 0x81327438 _addr_8130b32c test_func

// patch_inst vNTSC_11(_disable_main_menu_text) 0x81314d24 nop
// patch_inst vNTSC_11(_disable_main_menu_cube_a) 0x8130df44 nop
// patch_inst vNTSC_11(_disable_main_menu_cube_b) 0x8130dfac nop
// patch_inst vNTSC_11(_disable_menu_transition_cube) 0x8130de04 nop
// patch_inst vNTSC_11(_disable_menu_transition_outer) 0x8130ddc0 nop

// patch_inst vNTSC_11(_disable_menu_transition_text_a) 0x8130de88 nop
// patch_inst vNTSC_11(_disable_menu_transition_text_b) 0x8130de70 nop

// patch_inst vNTSC_11(_disable_options_menu_transition) 0x81327c4c nop

// patch_inst vNTSC_11(_disable_card_menu_exit) 0x8131612c b _addr_813161e8
// patch_inst vNTSC_11(_disable_card_menu_text_a) 0x8131b328 nop
// patch_inst vNTSC_11(_disable_card_menu_text_b) 0x8131b3bc nop
// patch_inst vNTSC_11(_disable_card_menu_text_c) 0x8131b45c nop
// patch_inst vNTSC_11(_disable_card_menu_text_d) 0x8131b4f0 nop
// patch_inst vNTSC_11(_disable_memory_card_detect) 0x813010ec nop

patch_inst_ntsc "_stub_dvdwait" 0x00000000 0x8130108c 0x81301440 0x81301444 nop
patch_inst_pal  "_stub_dvdwait" 0x8130108c 0x8130108c 0x813011f8 nop

patch_inst_ntsc "_replace_bs2tick" 0x81300a70 0x81300968 0x81300d08 0x81300d0c b bs2tick
patch_inst_pal  "_replace_bs2tick" 0x81300968 0x81300968 0x81300ac0 b bs2tick

patch_inst_ntsc "_replace_bs2start" 0x813023e0 0x813021e8 0x81302590 0x813025a8 bl bs2start
patch_inst_pal  "_replace_bs2start" 0x813021e8 0x813021e8 0x8130235c bl bs2start

patch_inst_ntsc "_replace_report" 0x8133491c 0x8135a344 0x81300520 0x81300520 b custom_OSReport
patch_inst_pal  "_replace_report" 0x8135d924 0x8135a264 0x81300520 b custom_OSReport

patch_inst vNTSC_11(_patch_menu_init) 0x81301094 bl pre_menu_init // TODO: find offsets
patch_inst vNTSC_11(_patch_menu_alpha_setup) 0x81312358 bl pre_menu_alpha_setup // TODO: find offsets

patch_inst_pal "_fix_video_mode_init" 0x81300520 0x81300520 0x81300610 bl get_tvmode

patch_inst_global "_patch_pre_main" 0x81300090 bl pre_main
