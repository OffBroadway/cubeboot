#include <gctypes.h>

#include "attr.h"
#include "reloc.h"
#include "picolibc.h"

#include "memcard.h"
#include "font.h"

// from https://github.com/Prakxo/ac-decomp/blob/b9554ef0cc3d8474047148882e898191f6e7bbb2/include/dolphin/private/card.h#L76
#define CARD_MAX_FILE 127
#define CARD_FILENAME_MAX 32
typedef struct CARDDir {
    // total size: 0x40
    u8 gameName[4];                 // offset 0x0, size 0x4
    u8 company[2];                  // offset 0x4, size 0x2
    u8 _padding0;                   // offset 0x6, size 0x1
    u8 bannerFormat;                // offset 0x7, size 0x1
    u8 fileName[CARD_FILENAME_MAX]; // offset 0x8, size 0x20
    u32 time;                       // offset 0x28, size 0x4
    u32 iconAddr;                   // offset 0x2C, size 0x4
    u16 iconFormat;                 // offset 0x30, size 0x2
    u16 iconSpeed;                  // offset 0x32, size 0x2
    u8 permission;                  // offset 0x34, size 0x1
    u8 copyTimes;                   // offset 0x35, size 0x1
    u16 startBlock;                 // offset 0x36, size 0x2
    u16 length;                     // offset 0x38, size 0x2
    u8 _padding1[2];                // offset 0x3A, size 0x2
    u32 commentAddr;                // offset 0x3C, size 0x4
} CARDDir;

__attribute_reloc__ s32 (*__CARDGetStatusEx)(s32 chan, s32 fileNo, CARDDir* dirent);
__attribute_reloc__ s32 (*read_save_banner_text)(s32 chan, s32 fileNo, CARDDir *dirent, void *unknown);
__attribute_reloc__ char* (*get_card_info)(s32 chan, s32 fileNo);
__attribute_reloc__ void (*draw_card_info)(char unk);

__attribute_data__ gameid_t card_game_ids[2][CARD_MAX_FILE];
__attribute_used__ s32 read_save_banner_text_and_game_id(s32 chan, s32 fileNo, CARDDir *dirent, void *unknown) {
    s32 ret = read_save_banner_text(chan, fileNo, dirent, unknown);
    if (ret >= 0) {
        gameid_t *id = &card_game_ids[chan][fileNo];
        memcpy(id, &dirent->gameName[0], sizeof(gameid_t));
    }

    return ret;
}

__attribute_used__ char *patched_card_info(s32 chan, s32 fileNo) {
    if (card_game_ids[chan][fileNo].parts.gamecode[3] == 'J') switch_lang_jpn();
    else switch_lang_eng();

    return get_card_info(chan, fileNo);
}

__attribute_used__ void fix_card_info(char unk) {
    draw_card_info(unk);
    switch_lang_orig();
}
