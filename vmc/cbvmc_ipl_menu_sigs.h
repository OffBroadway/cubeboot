/* SPDX-License-Identifier: GPL-2.0-only */
/* Generated from the IPL analysis of 2026-11-06 (the IPL analysis notes, not part of this tree): masked instruction-word signatures of the BS2 functions the Memory Card page
   popup extension patches (cbvmc_ipl_menu.c).  Each matches exactly once in the USA, JAP, PAL and PAL 1.2 images.  Masks wildcard branch targets, small-data
   displacements and lis immediates.  Original work (no Nintendo code: 16 instruction words per function, used only to locate it). */
#ifndef CBVMC_IPL_MENU_SIGS_H
#define CBVMC_IPL_MENU_SIGS_H
#include <stdint.h>

struct cbvmc_menu_sig { const char *name; const uint32_t *val; const uint32_t *mask; unsigned n; };

/* MC_StateBrowse: usa 0x813157a0 pal 0x813164fc */
static const uint32_t cbvmc_sig_MC_StateBrowse_v[] = { 0x7c0802a6u, 0x3c600000u, 0x90010004u, 0x9421ffd8u, 0xbf410010u, 0x3ba30000u, 0x3b600004u, 0x48000001u, 0x7c7e1b78u, 0x48000001u, 0x7c7f1b78u, 0x48000001u, 0x2c030000u, 0x4082000cu, 0x38600004u, 0x48000000u };
static const uint32_t cbvmc_sig_MC_StateBrowse_m[] = { 0xffffffffu, 0xffff0000u, 0xffffffffu, 0xffffffffu, 0xffffffffu, 0xffff0000u, 0xffffffffu, 0xfc000003u, 0xffffffffu, 0xfc000003u, 0xffffffffu, 0xfc000003u, 0xffffffffu, 0xffffffffu, 0xffffffffu, 0xfc000003u };
/* MC_PopupMeasureLabels: usa 0x81319954 pal 0x8131a90c */
static const uint32_t cbvmc_sig_MC_PopupMeasureLabels_v[] = { 0x7c0802a6u, 0x38e00002u, 0x90010004u, 0x9421ffc0u, 0xbf010020u, 0x3b230000u, 0x3b460000u, 0x3b640008u, 0x54f81838u, 0x3be00000u, 0x3bc00000u, 0x80050008u, 0x7f850214u, 0x48000000u, 0x57c3043eu, 0x7c1bc22eu };
static const uint32_t cbvmc_sig_MC_PopupMeasureLabels_m[] = { 0xffffffffu, 0xffffffffu, 0xffffffffu, 0xffffffffu, 0xffffffffu, 0xffffffffu, 0xffffffffu, 0xffffffffu, 0xffffffffu, 0xffffffffu, 0xffffffffu, 0xffffffffu, 0xffffffffu, 0xfc000003u, 0xffffffffu, 0xffffffffu };
/* MC_PopupDrawItems: usa 0x81319abc pal 0x8131aa74 */
static const uint32_t cbvmc_sig_MC_PopupDrawItems_v[] = { 0x7c0802a6u, 0x3d600000u, 0x90010004u, 0x9421ff70u, 0xbec10068u, 0x3ae30000u, 0x3b040000u, 0x3b850000u, 0x3ac60000u, 0x3b270000u, 0x3bc80000u, 0x3ba90000u, 0x3b6a0000u, 0x3beb0000u, 0x48000001u, 0x80b60008u };
static const uint32_t cbvmc_sig_MC_PopupDrawItems_m[] = { 0xffffffffu, 0xffff0000u, 0xffffffffu, 0xffffffffu, 0xffffffffu, 0xffffffffu, 0xffffffffu, 0xffffffffu, 0xffffffffu, 0xffffffffu, 0xffffffffu, 0xffffffffu, 0xffffffffu, 0xffff0000u, 0xfc000003u, 0xffffffffu };
/* MC_StatePopup: usa 0x81315e50 pal 0x81316bac */
static const uint32_t cbvmc_sig_MC_StatePopup_v[] = { 0x7c0802a6u, 0x90010004u, 0x9421ffd8u, 0x93e10024u, 0x93c10020u, 0x48000001u, 0x3c600000u, 0x38630000u, 0xa0030000u, 0x3be30000u, 0x540004e7u, 0x4182002cu, 0x3860000au, 0x48000001u, 0xa86d0000u, 0x3803ffffu };
static const uint32_t cbvmc_sig_MC_StatePopup_m[] = { 0xffffffffu, 0xffffffffu, 0xffffffffu, 0xffffffffu, 0xffffffffu, 0xfc000003u, 0xffff0000u, 0xffff0000u, 0xffff0000u, 0xffff0000u, 0xffffffffu, 0xffffffffu, 0xffffffffu, 0xfc000003u, 0xffff0000u, 0xffffffffu };
/* MC_StateConfirm: usa 0x81316080 pal 0x81316ddc */
static const uint32_t cbvmc_sig_MC_StateConfirm_v[] = { 0x7c0802a6u, 0x90010004u, 0x9421ffd8u, 0xbf610014u, 0x48000001u, 0x3c600000u, 0x38630000u, 0xa0030000u, 0x3b630000u, 0x540004e7u, 0x41820020u, 0xa80d0000u, 0x2c000000u, 0x4182000cu, 0x38600009u, 0x48000001u };
static const uint32_t cbvmc_sig_MC_StateConfirm_m[] = { 0xffffffffu, 0xffffffffu, 0xffffffffu, 0xffffffffu, 0xfc000003u, 0xffff0000u, 0xffff0000u, 0xffff0000u, 0xffff0000u, 0xffffffffu, 0xffffffffu, 0xffff0000u, 0xffffffffu, 0xffffffffu, 0xffffffffu, 0xfc000003u };
/* CARDFormatAsync: usa 0x81346540 pal 0x81371974 */
static const uint32_t cbvmc_sig_CARDFormatAsync_v[] = { 0x7c0802a6u, 0x90010004u, 0x9421ffb0u, 0xbe410018u, 0x3a440000u, 0x3ac30000u, 0x38810014u, 0x48000001u, 0x2c030000u, 0x40800008u, 0x48000000u, 0x80610014u, 0x388000ffu, 0x38a02000u, 0x83630000u, 0x7f63db78u };
static const uint32_t cbvmc_sig_CARDFormatAsync_m[] = { 0xffffffffu, 0xffffffffu, 0xffffffffu, 0xffffffffu, 0xffdfffffu, 0xffffffffu, 0xffffffffu, 0xfc000003u, 0xffffffffu, 0xffffffffu, 0xfc000003u, 0xffffffffu, 0xffffffffu, 0xffffffffu, 0xffffff57u, 0xffffffffu };
/* MC_ReqFormat: usa 0x8131aed4 pal 0x8131bfac */
static const uint32_t cbvmc_sig_MC_ReqFormat_v[] = { 0x7c0802a6u, 0x38800001u, 0x90010004u, 0x3800ffebu, 0x9421ffe8u, 0x93e10014u, 0x986d0000u, 0x5463821eu, 0x607f0001u, 0x900d0000u, 0x908d0000u, 0x900d0000u, 0x48000001u, 0x389f0000u, 0x38a00000u, 0x48000001u };
static const uint32_t cbvmc_sig_MC_ReqFormat_m[] = { 0xffffffffu, 0xffffffffu, 0xffffffffu, 0xffffffffu, 0xffffffffu, 0xffffffffu, 0xffff0000u, 0xffffffffu, 0xffffffffu, 0xffff0000u, 0xffff0000u, 0xffff0000u, 0xfc000003u, 0xffffffffu, 0xffffffffu, 0xfc000003u };
/* MC_ClosePopup: usa 0x813156b8 pal 0x81316414 */
static const uint32_t cbvmc_sig_MC_ClosePopup_v[] = { 0x7c0802a6u, 0x90010004u, 0x9421ffe8u, 0x93e10014u, 0x93c10010u, 0x7c7e1b78u, 0x48000001u, 0xa80d0000u, 0x2c000005u, 0x4082000cu, 0x2c1e0000u, 0x418200a4u, 0x88ad0000u, 0x68be0001u, 0x1c1e0014u, 0x7c830214u };
static const uint32_t cbvmc_sig_MC_ClosePopup_m[] = { 0xffffffffu, 0xffffffffu, 0xffffffffu, 0xffffffffu, 0xffffffffu, 0xffffffffu, 0xfc000003u, 0xffff0000u, 0xffffffffu, 0xffffffffu, 0xffffffffu, 0xffffffffu, 0xffff0000u, 0xffffffffu, 0xffffffffu, 0xffffffffu };

enum { CBVMC_SIG_MC_STATEBROWSE, CBVMC_SIG_MC_POPUPMEASURELABELS, CBVMC_SIG_MC_POPUPDRAWITEMS, CBVMC_SIG_MC_STATEPOPUP, CBVMC_SIG_MC_STATECONFIRM, CBVMC_SIG_CARDFORMATASYNC, CBVMC_SIG_MC_REQFORMAT, CBVMC_SIG_MC_CLOSEPOPUP, CBVMC_SIG_COUNT };
static const struct cbvmc_menu_sig cbvmc_menu_sigs[CBVMC_SIG_COUNT] = {
    { "MC_StateBrowse", cbvmc_sig_MC_StateBrowse_v, cbvmc_sig_MC_StateBrowse_m, 16 },
    { "MC_PopupMeasureLabels", cbvmc_sig_MC_PopupMeasureLabels_v, cbvmc_sig_MC_PopupMeasureLabels_m, 16 },
    { "MC_PopupDrawItems", cbvmc_sig_MC_PopupDrawItems_v, cbvmc_sig_MC_PopupDrawItems_m, 16 },
    { "MC_StatePopup", cbvmc_sig_MC_StatePopup_v, cbvmc_sig_MC_StatePopup_m, 16 },
    { "MC_StateConfirm", cbvmc_sig_MC_StateConfirm_v, cbvmc_sig_MC_StateConfirm_m, 16 },
    { "CARDFormatAsync", cbvmc_sig_CARDFormatAsync_v, cbvmc_sig_CARDFormatAsync_m, 16 },
    { "MC_ReqFormat", cbvmc_sig_MC_ReqFormat_v, cbvmc_sig_MC_ReqFormat_m, 16 },
    { "MC_ClosePopup", cbvmc_sig_MC_ClosePopup_v, cbvmc_sig_MC_ClosePopup_m, 16 },
};

#endif
