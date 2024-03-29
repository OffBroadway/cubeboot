#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <malloc.h>

#include "sd.h"
#include "halt.h"
#include "helpers.h"

#include "ini.h"
#include "settings.h"

settings_t settings;

char *buttons_names[] = {
    "left",  // LEFT	0x0001
    "right", // RIGHT	0x0002
    "down",  // DOWN	0x0004
    "up",    // UP		0x0008
    "z",     // Z		0x0010
    "r",     // R		0x0020
    "l",     // L		0x0040
    "_",     // origin  0x0080 (NOT USED)
    "_",     // A		0x0100 (NOT ALLOWED)
    "b",     // B		0x0200
    "x",     // X		0x0400
    "y",     // Y		0x0800
    "start", // START	0x1000
};

void load_settings() {
    memset(&settings, 0, sizeof(settings));
    int config_size = get_file_size("/cubeboot.ini");
    if (config_size == SD_FAIL) return;

    void *config_buf = memalign(32, config_size + 1);
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

    // default program
    const char *default_program = ini_get(conf, "", "default_program");
    if (default_program != NULL) {
        iprintf("Found default_program = %s\n", default_program);
        settings.default_program = (char*)default_program;
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

    // preboot delay
    u32 preboot_delay_ms = 0;
    if (!ini_sget(conf, "", "preboot_delay_ms", "%u", &preboot_delay_ms)) {
        settings.preboot_delay_ms = 0;
    } else {
        iprintf("Found preboot_delay_ms = %u\n", preboot_delay_ms);
        settings.preboot_delay_ms = preboot_delay_ms;
    }

    // postboot delay
    u32 postboot_delay_ms = 0;
    if (!ini_sget(conf, "", "postboot_delay_ms", "%u", &postboot_delay_ms)) {
        settings.postboot_delay_ms = 0;
    } else {
        iprintf("Found postboot_delay_ms = %u\n", postboot_delay_ms);
        settings.postboot_delay_ms = postboot_delay_ms;
    }

    // button presses
    for (int i = 0; i < (sizeof(buttons_names) / sizeof(char *)); i++) {
        char *button_name = buttons_names[i];

        // ignore disabled buttons
        if (*button_name == '_') {
            continue;
        }

        char button_config_name[255];
        sprintf(button_config_name, "button_%s", button_name);

        const char *dol_path = ini_get(conf, "", button_config_name);
        if (dol_path != NULL) {
            iprintf("Found %s = %s\n", button_config_name, dol_path);

            settings.boot_buttons[i] = (char*)dol_path;
        }
    }

    // // must stay allocated!!
    // free(config_buf);
}
