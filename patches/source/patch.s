#define _LANGUAGE_ASSEMBLY
#include "asm.h"
#include "patch_asm.h"

// TODO: install a pre_main function for init
// TODO: replace bs2start with a funciton overload

// patch_inst vNTSC_11(_move_heap_start) 0x81307dc0 lis r3, 0x8080
// patch_inst vNTSC_11(_replace_bs2start_again) 0x81300830 b bs2start

patch_inst vNTSC_11(_disable_bgm) 0x81357640 blr

_play_sfx_tramp:
    addi r31,r3,0x0
    bl play_sfx_hook
    b _addr_813574b4

patch_inst vNTSC_11(_hook_play_sfx) 0x813574b0 b _play_sfx_tramp
patch_inst vNTSC_11(_hook_close_sfx_track_a) 0x8135409c bl close_sfx_track_hook
patch_inst vNTSC_11(_hook_close_sfx_track_b) 0x813528fc bl close_sfx_track_hook
patch_inst vNTSC_11(_hook_ai_dma) 0x813355dc bl ai_dma_hook

// patch_inst vNTSC_11(_hook_menu_entry_snd_a) 0x8130c258 bl menu_entry_snd_hook
// patch_inst vNTSC_11(_hook_menu_entry_snd_b) 0x8130c288 bl menu_entry_snd_hook
// patch_inst vNTSC_11(_hook_menu_entry_snd_c) 0x8130e2fc bl menu_entry_snd_hook
// patch_inst vNTSC_11(_hook_menu_entry_snd_d) 0x8130e2d4 bl menu_entry_snd_hook

// patch_inst vNTSC_11(_test_trap) 0x81335ca8 trap
// patch_inst vNTSC_11(_test_trap_a) 0x81335ca8 bl mega_trap

// patch_inst vNTSC_11(_fast_audio_a) 0x813355dc bl _addr_81335d40
// patch_inst vNTSC_11(_fast_audio_b) 0x81335ca4 li r5, 1
// patch_inst vNTSC_11(_fast_audio_c) 0x81335da8 li r5, 1

// patch_inst vNTSC_11(_hide_miss_send_aud) 0x81335dc4 nop
patch_inst vNTSC_11(_jac_debug_a) 0x81353f30 nop

patch_inst vNTSC_11(_skip_gfx_setup) 0x81301050 b _addr_81301098
patch_inst vNTSC_11(_replace_program) 0x813010a0 bl audio_extract_main
// patch_inst vNTSC_11(_reduce_heap_a) 0x81307dc0 lis r3, 0x80f00000@h
// patch_inst vNTSC_11(_reduce_heap_b) 0x81307df4 lis r5, 0x81200000@h
patch_inst vNTSC_11(_reduce_heap_a) 0x81307dc0 lis r3, 0x80f00000@h

patch_inst_ntsc "_stub_dvdwait" 0x00000000 0x8130108c 0x81301440 0x81301444 nop
patch_inst_pal  "_stub_dvdwait" 0x8130108c 0x8130108c 0x813011f8 nop

patch_inst_ntsc "_replace_bs2tick" 0x81300a70 0x81300968 0x81300d08 0x81300d0c b bs2tick
patch_inst_pal  "_replace_bs2tick" 0x81300968 0x81300968 0x81300ac0 b bs2tick

patch_inst_ntsc "_replace_bs2start" 0x813023e0 0x813021e8 0x81302590 0x813025a8 bl bs2start
patch_inst_pal  "_replace_bs2start" 0x813021e8 0x813021e8 0x8130235c bl bs2start

patch_inst_ntsc "_replace_report" 0x8133491c 0x8135a344 0x81300520 0x81300520 b usb_OSReport
patch_inst_pal  "_replace_report" 0x8135d924 0x8135a264 0x81300520 b usb_OSReport

patch_inst_ntsc "_patch_cube_init" 0x8130e9b4 0x8130ebac 0x8130ef20 0x8130ef38 bl pre_cube_init
patch_inst_pal  "_patch_cube_init" 0x8130f2c8 0x8130ead8 0x8130f408 bl pre_cube_init

patch_inst_pal "_fix_video_mode_init" 0x81300520 0x81300520 0x81300610 bl get_tvmode

patch_inst_global "_patch_pre_main" 0x81300090 bl pre_main
