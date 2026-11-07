#include "dolphin_os.h"
#include "games.h"
#include "mcp.h"

#include "os.h"
#include "attr.h"

#include "reloc.h"
#include "picolibc.h"

__attribute_reloc__ OSMessageQueue *card_thread_mq;

__attribute_data__ u32 disable_mcp_select = 0;
#ifdef CBVMC_IPL
extern volatile u32 cbvmc_ipl_cnt[16];     // heartbeat counters (vmc/cbvmc_ipl_dev.c): [2] mcp_set_gameid calls, [3] setup_gameid_commands calls, [4] spins
#endif
__attribute_data__ static gm_file_entry_t *mcp_selected_entry = NULL;

void mcp_set_gameid(gm_file_entry_t *entry) {
#ifdef CBVMC_IPL
    cbvmc_ipl_cnt[2]++;
#endif
    if (disable_mcp_select) return;
    mcp_selected_entry = entry;
    OSSendMessage(card_thread_mq, (OSMessage)0xc, 0);
}

void setup_gameid_commands(struct gcm_disk_info *di, char diskName[64]) {
    const s32 chan = 0;
    u32 id;
    s32 ret;

#ifdef CBVMC_IPL
    // G4: with a virtual card on the channel the BUSY loops below could spin for ever (EXILock held by the card library): bounded, counted
    u32 spins = 0;
    cbvmc_ipl_cnt[3]++;
    #define MCPWAIT(call) do { spins = 0; while ((ret = (call)) == MCP_RESULT_BUSY && spins < 20000) { spins++; cbvmc_ipl_cnt[4]++; } if (ret < 0 || ret == MCP_RESULT_BUSY) return; } while (0)
    MCPWAIT(MCP_ProbeEx(chan));
    MCPWAIT(MCP_GetDeviceID(chan, &id));
    MCPWAIT(MCP_SetDiskID(chan, di));
    MCPWAIT(MCP_SetDiskInfo(chan, diskName));
    #undef MCPWAIT
#else
    while ((ret = MCP_ProbeEx(chan)) == MCP_RESULT_BUSY);
    if (ret < 0) return;
    while ((ret = MCP_GetDeviceID(chan, &id)) == MCP_RESULT_BUSY);
    if (ret < 0) return;
    while ((ret = MCP_SetDiskID(chan, di)) == MCP_RESULT_BUSY);
    if (ret < 0) return;
    while ((ret = MCP_SetDiskInfo(chan, diskName)) == MCP_RESULT_BUSY);
    if (ret < 0) return;
#endif
}

BOOL pre_custom_card_OSSendMessage(OSMessageQueue* mq, OSMessage msg, s32 flags) {
    OSReport("Sending message %d to %08x\n", msg, mq);
    if (mcp_selected_entry != NULL) {
        gm_extra_t *extra = &mcp_selected_entry->extra;
        struct gcm_disk_info diskID = {
            .game_code = { extra->game_id[0], extra->game_id[1], extra->game_id[2], extra->game_id[3] },
            .maker_code = { extra->game_id[4], extra->game_id[5] },
            .disk_id = extra->disc_num,
            .version = extra->disc_ver,
        };
        DCFlushRange(&diskID, sizeof(diskID));

        char diskInfo[64];
        strcpy(&diskInfo[0], mcp_selected_entry->desc.gameName);
        DCFlushRange(diskInfo, 64);

        setup_gameid_commands(&diskID, diskInfo);
    }
    return OSSendMessage(mq, msg, flags);
}
