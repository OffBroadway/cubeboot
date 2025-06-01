#pragma once

#include <gctypes.h>
#include <stdatomic.h>

#include "dolphin_os.h"
#include "icon.h"
#include "bnr.h"

#include "decomp_ar.h"

// Backing
typedef enum {
    GM_LOAD_STATE_NONE,
    GM_LOAD_STATE_SETUP, // valid for use
    GM_LOAD_STATE_UNLOADED,
    GM_LOAD_STATE_UNLOADING,
    GM_LOAD_STATE_LOADING,
    GM_LOAD_STATE_LOADED, // valid for use
} gm_load_state_t;

typedef enum {
    GM_FILE_TYPE_UNKNOWN,
    GM_FILE_TYPE_DIRECTORY,
    GM_FILE_TYPE_PROGRAM,
    GM_FILE_TYPE_GAME
} gm_file_type_t;

#pragma pack(push,1)
typedef struct {
    u32 used;
    u32 padding[7];
    u8 data[ICON_PIXELDATA_LEN];
} gm_icon_buf_t;

typedef struct {
    u32 used;
    u32 padding[7];
    u8 data[BNR_PIXELDATA_LEN];
} gm_banner_buf_t __attribute__((aligned(32)));
#pragma pack(pop)

typedef struct {
    ARQRequest req;
    u32 aram_offset;
    bool schedule_free;
    gm_load_state_t state;
    gm_icon_buf_t *buf;
} gm_icon_t;

typedef struct {
    ARQRequest req;
    u32 aram_offset;
    bool schedule_free;
    gm_load_state_t state;
    gm_banner_buf_t *buf;
} gm_banner_t;

typedef struct {
    bool use_banner;
    gm_icon_t icon;
    gm_banner_t banner;
} gm_asset_t;

// typedef struct gm_disc_header {
//     char game_code[4];
// 	char maker_code[2];
// 	char disk_id;
// 	char version;
// } gm_disc_header_t;

typedef struct {
    u8 game_id[6];
    u8 disc_num;
    u8 disc_ver;
    u8 padding;
    u8 dvd_bnr_type;
    u32 dvd_bnr_offset;
    u32 dvd_dol_offset;
    u32 dvd_fst_offset;
    u32 dvd_fst_size;
    u32 dvd_max_fst_size;
} gm_extra_t;

typedef struct {
    char path[128];
    gm_file_type_t type;
} gm_path_entry_t;

typedef struct gm_file_entry_struct gm_file_entry_t;

struct gm_file_entry_struct {
    char path[128];
    BNRDesc desc;
    gm_extra_t extra;
    gm_asset_t asset;
    gm_file_type_t type;
    gm_file_entry_t *second;
};

extern int number_of_lines;
extern int game_backing_count;
extern OSMutex *game_enum_mutex;
extern bool game_enum_running;
extern char game_enum_path[];

// for boot but defined in main (???)
extern gm_file_entry_t boot_entry;
extern gm_file_entry_t second_boot_entry;

// For DVD-reading thread
extern atomic_bool request_disc_stop_thread;
extern atomic_bool request_disc_start_game;
extern atomic_uint disc_read_state;
extern atomic_bool disc_read_banner_ready;
extern atomic_char disc_read_region;

extern BNR* stock_banner_ptr;

extern bool game_enum_running;
extern bool game_disc_running;

void gm_init_heap();
void gm_init_thread();
void gm_deinit_thread();
void gm_start_thread(const char *target);
void gm_start_disc_thread();
void gm_line_changed(int delta);
bool gm_can_move();
gm_file_entry_t *gm_get_game_entry(int index);
