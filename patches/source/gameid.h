#include "games.h"

typedef struct dolphin_game_into_t dolphin_game_into_t;

void setup_gameid_commands(struct gcm_disk_info *di, char diskName[64]); // direct
void mcp_set_gameid(gm_file_entry_t *entry);
void mcp_set_gameid_for_disc(dolphin_game_into_t* game_info, const BNRDesc *desc);
