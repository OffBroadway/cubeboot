#include "gameid.h"

#include "dolphin_dvd.h"
#include "dolphin_os.h"
#include "games.h"
#include "mcp.h"

#include "os.h"
#include "attr.h"

#include "reloc.h"
#include "picolibc.h"

__attribute_reloc__ OSMessageQueue *card_thread_mq;

__attribute_data__ u32 disable_mcp_select = 0;

static OSMutex disk_data_mutex;
__attribute_data__ static struct gcm_disk_info disk_id;
__attribute_data__ static char disk_name[64];

void mcp_set_gameid(gm_file_entry_t *entry) {
    if (disable_mcp_select) return;

    OSLockMutex(&disk_data_mutex);
    {
        gm_extra_t *extra = &entry->extra;
        disk_id = (struct gcm_disk_info){
            .game_code = { extra->game_id[0], extra->game_id[1], extra->game_id[2], extra->game_id[3] },
            .maker_code = { extra->game_id[4], extra->game_id[5] },
            .disk_id = extra->disc_num,
            .version = extra->disc_ver,
        };
        DCFlushRange(&disk_id, sizeof(disk_id));

        strcpy(disk_name, entry->desc.gameName);
        DCFlushRange(disk_name, sizeof(disk_name));
    }
    OSUnlockMutex(&disk_data_mutex);

    OSSendMessage(card_thread_mq, (OSMessage)0xc, 0);
}

void mcp_set_gameid_for_disc(dolphin_game_into_t* game_info, const BNRDesc *desc) {
    if (disable_mcp_select) return;

    OSLockMutex(&disk_data_mutex);
    {
        disk_id = (struct gcm_disk_info){
            .game_code = { game_info->game_id[0], game_info->game_id[1], game_info->game_id[2], game_info->game_id[3] },
            .maker_code = { game_info->game_id[4], game_info->game_id[5] },
            .disk_id = game_info->disc_num,
            .version = game_info->disc_ver,
        };
        DCFlushRange(&disk_id, sizeof(disk_id));

        strcpy(disk_name, desc->gameName);
        DCFlushRange(disk_name, sizeof(disk_name));
    }
    OSUnlockMutex(&disk_data_mutex);

    OSSendMessage(card_thread_mq, (OSMessage)0xc, 0);
}

void setup_gameid_commands(struct gcm_disk_info *di, char diskName[64]) {
    if (disable_mcp_select) return;

    const s32 chan = 0;
    u32 id;
    s32 ret;

    while ((ret = MCP_ProbeEx(chan)) == MCP_RESULT_BUSY);
    if (ret < 0) return;
    while ((ret = MCP_GetDeviceID(chan, &id)) == MCP_RESULT_BUSY);
    if (ret < 0) return;
    while ((ret = MCP_SetDiskID(chan, di)) == MCP_RESULT_BUSY);
    if (ret < 0) return;
    while ((ret = MCP_SetDiskInfo(chan, diskName)) == MCP_RESULT_BUSY);
    if (ret < 0) return;
}

BOOL pre_custom_card_OSSendMessage(OSMessageQueue* mq, OSMessage msg, s32 flags) {
    OSReport("Sending message %d to %08x\n", msg, mq);

    OSLockMutex(&disk_data_mutex);
    {
        setup_gameid_commands(&disk_id, disk_name);
    }
    OSUnlockMutex(&disk_data_mutex);

    return OSSendMessage(mq, msg, flags);
}
