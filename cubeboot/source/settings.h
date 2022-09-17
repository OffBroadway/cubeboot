#include <gctypes.h>

typedef struct settings {
    u32 cube_color;
    char *cube_text;
    int cube_text_size;

} settings_t;

extern settings_t settings;

void load_settings();
