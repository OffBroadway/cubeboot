#include <gctypes.h>

#include "const.h"

typedef struct settings {
    u32 cube_color;
    char *cube_logo;
    u32 force_swiss_default;
    u32 show_watermark;
    u32 disable_mcp_select;
    u32 progressive_enabled;
    u32 preboot_delay_ms;
    u32 postboot_delay_ms;
    char *default_program;
    u32 vmc_enable;           // cubeboot_vmc: virtual memory cards (default ON when the key is missing, also without config.ini)
    u32 vmc_blocks;           // cubeboot_vmc: blocks of a card file generated when missing (59..2043); 0 = 2043
    u32 vmc_ipl_diag;         // cubeboot_vmc: vmc_diag, support switches (cbvmc_ipl_plan.h CBVMC_IPLD_*), default 0
    char *vmc_dir;            // cubeboot_vmc: the card folder (NULL = /cubeboot/saves); MemoryCard{A,B}.{USA,EUR,JAP}.raw live in it
    u32 vmc_regions;          // cubeboot_vmc: 0 = stock (US and PAL share the USA pair, as a real card), 1 = swiss (three pairs by game region)
    char *boot_buttons[MAX_BUTTONS];
} settings_t;

extern char *buttons_names[];
extern settings_t settings;

void load_settings();
