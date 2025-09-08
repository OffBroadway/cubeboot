#define _LANGUAGE_ASSEMBLY
#include "asm.h"
#include "patch_asm.h"

// patch_inst vNTSC_11(_change_background_color) 0x81481cc8 .4byte 0x000435FF
// patch_inst vNTSC_11(_test_only_a) 0x8131e360 bl mega_trap
// patch_inst vNTSC_11(_test_only_b) 0x81301448 bl before_audio_init
// patch_inst vNTSC_11(_gameselect_hide_cubes) 0x81327454 nop
// patch_inst vNTSC_11(_skip_menu_logo_a) 0x8130bcb4 li r3, 1
// patch_inst vNTSC_11(_skip_menu_logo_b) 0x8130bcc0 li r0, 5

// Trevor's greatest hits
// patch_inst vNTSC_11(_skip_menu_logo_c) 0x8130d178 li r3, 4 // force menu fast
// patch_inst vNTSC_11(_force_anim_draw) 0x8130d588 nop
// patch_inst vNTSC_11(_patch_anim_draw) 0x8130d590 bl patch_anim_draw

patch_inst_ntsc "_reduce_aram_alloc" 0x81301204 0x81301040 0x813013f4 0x813013f8 lis r5, 0x10 // Reduce ARAM to 1MB for JAudio (also used in Pikmin)
patch_inst_pal "_reduce_aram_alloc" 0x81301040 0x81301040 0x81301040 lis r5, 0x10 // Reduce ARAM to 1MB for JAudio (also used in Pikmin)

// patch_inst_ntsc "_force_lang" 0x8130b5b8 0x8130b740 0x8130bab4 0x8130bacc bl set_forced_lang
patch_inst_ntsc "_force_lang" 0x8130b5b4 0x8130b73c 0x8130bab0 0x8130bac8 li r0, 0 // Force English=0, Japanese=2 (NTSC Only)
patch_inst_pal "_fix_font_japanese" 0x81309600 0x00000000 0x81309740 b GetFontCode
patch_inst vNTSC_10(_fix_font_decode_buf) 0x813083d0 lis r3, -0x7e90 // Move buffer to himem (0x81700000) TODO: wrap this with an alloc
patch_inst vNTSC_10(_fix_banner_bnr2) 0x81302778 nop

patch_inst_ntsc "_patch_font_init" 0x81301240 0x8130107c 0x81301430 0x81301434 bl setup_fonts
patch_inst_pal "_patch_font_init" 0x8130107c 0x8130107c 0x813011e8 bl setup_fonts

patch_inst_ntsc "_patch_card_status_a" 0x8131c770 0x8131ce9c 0x8131d234 0x8131d24c bl save_card_status
patch_inst_pal "_patch_card_status_a" 0x8131d848 0x8131cdc8 0x8131d988 bl save_card_status

patch_inst_ntsc "_patch_card_info_a" 0x8131a694 0x8131aca0 0x8131b038 0x8131b050 bl patched_card_info
patch_inst_pal "_patch_card_info_a" 0x8131b64c 0x8131abcc 0x8131b78c bl patched_card_info

patch_inst_ntsc "_patch_card_info_b" 0x8131a6bc 0x8131acc8 0x8131b060 0x8131b078 bl patched_card_info
patch_inst_pal "_patch_card_info_b" 0x8131b674 0x8131abf4 0x8131b7b4 bl patched_card_info

// this will show the correct language JPN/ENG in the memcard menu
patch_inst_ntsc "_fix_card_info" 0x81319000 0x81319600 0x81319998 0x813199b0 bl fix_card_info
patch_inst_pal "_fix_card_info" 0x81319fac 0x8131952c 0x8131a0ec bl fix_card_info

// this forces some control flow in an unused card thread function
patch_inst_ntsc "_skip_card_slot_checks" 0x8131dc14 0x8131e340 0x8131e6d8 0x8131e6f0 li r0, 0
patch_inst_pal "_skip_card_slot_checks" 0x8131ecec 0x8131e26c 0x8131ee2c li r0, 0

patch_inst_ntsc "_patch_custom_card_handler" 0x8131dccc 0x8131e3f8 0x8131e790 0x8131e7a8 bl pre_custom_card_OSSendMessage
patch_inst_pal "_patch_custom_card_handler" 0x8131eda4 0x8131e324 0x8131eee4 bl pre_custom_card_OSSendMessage

patch_inst_ntsc "_reduce_arena_size" 0x813328ec 0x8135825c 0x8135d998 0x8135d998 lis r3, -0x7ea0 // 0x81700000 -> 0x81600000
patch_inst_pal "_reduce_arena_size" 0x8135b83c 0x8135817c 0x81360d10 lis r3, -0x7ea0 // 0x81700000 -> 0x81600000

patch_inst vNTSC_10(_increase_heap_size) 0x81307ed8 lis r3, -0x7fa0 // NTSC10: 0x80800000 -> 0x80600000 (stability hack)
patch_inst_ntsc "_increase_heap_size" 0x00000000 0x81307dc0 0x8130815c 0x81308174 lis r3, -0x7fb0 // 0x80700000 -> 0x805000000
patch_inst_pal "_increase_heap_size" 0x81307dc0 0x81307dc0 0x81307f28 lis r3, -0x7fb0 // 0x80700000 -> 0x805000000

patch_inst_ntsc "_patch_thread_init" 0x81301234 0x81301070 0x81301424 0x81301428 bl pre_thread_init
patch_inst_pal "_patch_thread_init" 0x81301070 0x81301070 0x813011dc bl pre_thread_init

patch_inst_ntsc "_draw_watermark" 0x81314898 0x81314bb0 0x81314f48 0x81314f60 bl alpha_watermark
patch_inst_pal "_draw_watermark" 0x81315630 0x81314adc 0x81315770 bl alpha_watermark

patch_inst_ntsc "_gameselect_replace_draw" 0x81314200 0x81314518 0x813148b0 0x813148c8 bl mod_gameselect_draw
patch_inst_pal "_gameselect_replace_draw" 0x81314e58 0x81314444 0x81314f98 bl mod_gameselect_draw

patch_inst_ntsc "_gameselect_replace_input" 0x81326818 0x81326f94 0x8132732c 0x81327344 bl handle_gameselect_inputs
patch_inst_pal "_gameselect_replace_input" 0x81327968 0x81326ec0 0x81327aa8 bl handle_gameselect_inputs

.macro routine_gameselect_matrix_helper
    addi r3, r1, 0x74
    addi r4, r1, 0x14
    bl set_gameselect_view
    repeat_inst 44 nop
.endm

// NOTE: this is a mid-function patch
patch_inst_ntsc "_gameselect_draw_helper" 0x81326c14 0x81327430 0x813277c8 0x813277e0 routine_gameselect_matrix_helper
patch_inst_pal "_gameselect_draw_helper" 0x81327e04 0x8132735c 0x81327f44 routine_gameselect_matrix_helper

.macro routine_load_r0_r3_val1
    li r0, 1
    li r3, 1
.endm

// This makes the compare succeed
patch_inst_ntsc "_force_valid_encoding" 0x81345130 0x8136cdd4 0x8135a3e8 0x8135a3e8 routine_load_r0_r3_val1
patch_inst_pal "_force_valid_encoding" 0x813703b4 0x8136ccf4 0x8135d768 routine_load_r0_r3_val1

patch_inst_ntsc "_ignore_boot_error_setup" 0x8130bad8 0x8130bcd8 0x8130c04c 0x8130c064 li r0, 0x3
patch_inst_pal "_ignore_boot_error_setup" 0x8130bf30 0x8130bc04 0x8130c070 li r0, 0x3

patch_inst_ntsc "_ignore_boot_error_rtc" 0x8130bac0 0x8130bcc0 0x8130c034 0x8130c04c li r0, 0x3
patch_inst_pal "_ignore_boot_error_rtc" 0x8130bf18 0x8130bbec 0x8130c058 li r0, 0x3

patch_inst_ntsc "_stub_dvdwait" 0x00000000 0x8130108c 0x81301440 0x81301444 nop
patch_inst_pal  "_stub_dvdwait" 0x8130108c 0x8130108c 0x813011f8 nop

patch_inst_ntsc "_replace_bs2tick" 0x81300a70 0x81300968 0x81300d08 0x81300d0c b bs2tick
patch_inst_pal  "_replace_bs2tick" 0x81300968 0x81300968 0x81300ac0 b bs2tick

patch_inst_ntsc "_replace_bs2start_a" 0x813023e0 0x813021e8 0x81302590 0x813025a8 bl bs2start
patch_inst_pal  "_replace_bs2start_a" 0x813021e8 0x813021e8 0x8130235c bl bs2start

patch_inst_ntsc "_replace_bs2start_b" 0x813024a4 0x813022ac 0x81302648 0x81302660 bl bs2start
patch_inst_pal "_replace_bs2start_b" 0x813022ac 0x813022ac 0x81302414 bl bs2start

patch_inst_ntsc "_replace_report" 0x8133491c 0x8135a344 0x81300520 0x81300520 b custom_OSReport
patch_inst_pal  "_replace_report" 0x8135d924 0x8135a264 0x81300520 b custom_OSReport

patch_inst_ntsc "_patch_menu_init" 0x8130128c 0x81301094 0x81301448 0x8130144c bl pre_menu_init
patch_inst_pal "_patch_menu_init" 0x81301094 0x81301094 0x81301200 bl pre_menu_init

patch_inst_ntsc "_patch_menu_alpha_setup" 0x81312108 0x81312358 0x813126f0 0x81312708 bl pre_menu_alpha_setup
patch_inst_pal "_patch_menu_alpha_setup" 0x81312c3c 0x81312284 0x81312d7c bl pre_menu_alpha_setup

patch_inst_pal "_fix_video_mode_init" 0x81300520 0x81300520 0x81300610 bl get_tvmode

patch_inst_global "_patch_pre_main" 0x81300090 bl pre_main

patch_inst_ntsc "_patch_update_button_alphas" 0x81312104 0x81312354 0x813126ec 0x81312704 bl patch_update_button_alphas
patch_inst_pal "_patch_update_button_alphas" 0x81312c38 0x81312280 0x81312d78 bl patch_update_button_alphas
