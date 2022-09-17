#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "sd.h"
#include "halt.h"
#include "helpers.h"

#include "ini.h"
#include "settings.h"

settings_t settings;

void load_settings() {
    memset(&settings, 0, sizeof(settings));
    int config_size = get_file_size("/cubeboot.ini");
    if (config_size == SD_FAIL) return;

    void *config_buf = malloc(config_size + 1);
    if (config_buf == NULL) {
        prog_halt("Could not allocate buffer for config file\n");
        return;
    }

    if (load_file_buffer("/cubeboot.ini", config_buf) != SD_OK) {
        prog_halt("Could not find config file\n");
        return;
    }

    ((u8*)config_buf)[config_size - 1] = '\0';

    ini_t *conf = ini_load(config_buf, config_size);

    // cube color
    const char *cube_color_raw = ini_get(conf, "", "cube_color");
    if (cube_color_raw != NULL) {
        if (strcmp(cube_color_raw, "random") == 0) {
            settings.cube_color = generate_random_color();
        } else {
            int vars = sscanf(cube_color_raw, "%x", &settings.cube_color);
            if (vars == EOF) settings.cube_color = 0;
        }
    }

    // cube text
    const char *cube_text = ini_get(conf, "", "cube_text");
    if (cube_text != NULL) {
        // uppercase
        for (char *p = (char*)cube_text; *p; ++p)
            *p = (*p >= 'a' && *p <= 'z') ? *p - 0x20 : *p;

        iprintf("Found cube_text = %s\n", cube_text);
    }
    settings.cube_text = (char*)cube_text;

    int cube_text_size = 0;
    if (!ini_sget(conf, "", "cube_text_size", "%d", &cube_text_size)) {
        cube_text_size = 8;
    } else {
        iprintf("Found cube_text_size = %d\n", cube_text_size);
    }
    settings.cube_text_size = cube_text_size;

    free(config_buf);
}
