#include <gctypes.h>

typedef struct settings {
    u32 cube_color;

} settings_t;

extern settings_t settings;

void load_settings();
