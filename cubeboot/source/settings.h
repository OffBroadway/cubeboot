#include <gctypes.h>

typedef struct settings {
    u32 cube_color;
    char *cube_logo;
    u32 fallback_enabled;
    u32 progressive_enabled;
} settings_t;

extern settings_t settings;

void load_settings();
