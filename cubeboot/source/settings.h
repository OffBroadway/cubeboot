#include <gctypes.h>

#include "const.h"

typedef struct settings {
    u32 cube_color;
    char *cube_logo;
    u32 fallback_enabled;
    u32 progressive_enabled;
    u32 preboot_delay_ms;
    u32 postboot_delay_ms;
    char *default_program;
    char *boot_buttons[MAX_BUTTONS];
} settings_t;

extern char *buttons_names[];
extern settings_t settings;

void load_settings();
