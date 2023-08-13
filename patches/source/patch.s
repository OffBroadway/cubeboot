#define _LANGUAGE_ASSEMBLY
#include "asm.h"
#include "patch_asm.h"

#include "../../cubeboot/source/direct.h"

patch_inst vNTSC_11(_disable_ARInit) 0x81301010 nop
patch_inst vNTSC_11(_disable_ARQInit) 0x81301014 nop
patch_inst vNTSC_11(_disable_audio_alloc) 0x81301038 nop
patch_inst vNTSC_11(_disable_audio_start) 0x81301044 nop
// patch_inst vNTSC_11(_skip_audio_setup) 0x81301034 b _addr_81301048
patch_inst vNTSC_11(_always_draw_fatal) 0x81314d3c nop
patch_inst vNTSC_11(_set_alpha_draw_fatal) 0x8130bfa0 sth r5,0x88(r30)

patch_inst vNTSC_11(_skip_draw_logo) 0x8130d3d0 b _addr_8130d530 // interesting to turn on (draws 3D)
patch_inst vNTSC_11(_always_draw_menu) 0x8130bc34 b _addr_8130bc50
patch_inst vNTSC_11(_always_update_menu) 0x8130bb7c b _addr_8130bbc8
patch_inst vNTSC_11(_never_exit_menu) 0x813020f4 lis r0, 0x0

patch_inst vNTSC_11(_skip_card_unk0) 0x8130106c nop
patch_inst vNTSC_11(_skip_card_thread) 0x81301070 nop

#ifdef BS2_DIRECT_EXEC
patch_inst vNTSC_11(_skip_heap_init) 0x81301024 nop
patch_inst vNTSC_11(_replace_malloc) 0x81307e40 b bs2alloc_wrapper
patch_inst vNTSC_11(_replace_free) 0x81307e90 b bs2free_wrapper
patch_inst vNTSC_11(_replace_viconfig) 0x813664d4 b VIConfigure_wrapper
patch_inst vNTSC_11(_replace_visetblack) 0x81366cec b VISetBlack_wrapper
patch_inst vNTSC_11(_replace_visetnextframebuffer) 0x81366c80 b VISetNextFrameBuffer_wrapper
patch_inst vNTSC_11(_replace_viwaitforretrace) 0x8136600c b VIWaitForRetrace_wrapper
patch_inst vNTSC_11(_replace_vigetnextfield) 0x81366e24 b VIGetNextField_wrapper
patch_inst vNTSC_11(_replace_viflush) 0x81366b64 b VIFlush_wrapper

patch_inst vNTSC_11(_trap_something) 0x813010bc bl mega_trap
#endif


// patch_inst vNTSC_11(_change_background_color) 0x81481cc8 .4byte 0xFFFF00FF
// patch_inst vNTSC_11(_no_background_color) 0x8137377c blr

// patch_inst vNTSC_11(_change_default_menu) 0x81310b38 li r0, 0x4 // memcard
patch_inst vNTSC_11(_change_default_menu) 0x81310b38 li r0, 0x2 // gameselect

patch_inst vNTSC_11(_gameselect_hide_cubes) 0x81327454 nop
patch_inst vNTSC_11(_gameselect_hide_text) 0x8132747c b _addr_813274fc
patch_inst vNTSC_11(_gameselect_replace_draw) 0x81314518 bl custom_gameselect_menu

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
patch_inst vNTSC_11(_disable_memory_card_detect) 0x813010ec nop

patch_inst_ntsc "_stub_dvdwait" 0x00000000 0x8130108c 0x81301440 0x81301444 nop
patch_inst_pal  "_stub_dvdwait" 0x8130108c 0x8130108c 0x813011f8 nop

patch_inst_ntsc "_replace_bs2tick" 0x81300a70 0x81300968 0x81300d08 0x81300d0c b bs2tick
patch_inst_pal  "_replace_bs2tick" 0x81300968 0x81300968 0x81300ac0 b bs2tick

patch_inst_ntsc "_replace_bs2start" 0x813023e0 0x813021e8 0x81302590 0x813025a8 bl bs2start
patch_inst_pal  "_replace_bs2start" 0x813021e8 0x813021e8 0x8130235c bl bs2start

patch_inst_ntsc "_replace_report" 0x8133491c 0x8135a344 0x81300520 0x81300520 b custom_OSReport
patch_inst_pal  "_replace_report" 0x8135d924 0x8135a264 0x81300520 b custom_OSReport

patch_inst vNTSC_11(_patch_menu_init) 0x81301094 bl pre_menu_init // TODO: find offsets

patch_inst_pal "_fix_video_mode_init" 0x81300520 0x81300520 0x81300610 bl get_tvmode

patch_inst_global "_patch_pre_main" 0x81300090 bl pre_main
