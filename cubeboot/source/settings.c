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

    // cube logo
    const char *cube_logo = ini_get(conf, "", "cube_logo");
    if (cube_logo != NULL) {
        iprintf("Found cube_logo = %s\n", cube_logo);
        settings.cube_logo = (char*)cube_logo;
    }

    // fallback enable
    int fallback_enabled = 0;
    if (!ini_sget(conf, "", "force_fallback", "%d", &fallback_enabled)) {
        settings.fallback_enabled = 0;
    } else {
        iprintf("Found force_fallback = %d\n", fallback_enabled);
        settings.fallback_enabled = fallback_enabled;
    }

    // progressive enable
    int progressive_enabled = 0;
    if (!ini_sget(conf, "", "force_progressive", "%d", &progressive_enabled)) {
        settings.progressive_enabled = 0;
    } else {
        iprintf("Found progressive_enabled = %d\n", progressive_enabled);
        settings.progressive_enabled = progressive_enabled;
    }

    free(config_buf);
}
