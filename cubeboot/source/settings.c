#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <malloc.h>
#include <ctype.h>

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
    "_",     // Z		0x0010 (NOT ALLOWED) used to switch sound
    "_",     // R		0x0020 (NOT ALLOWED) used by bootloader
    "_",     // L		0x0040 (NOT ALLOWED) used by bootloader
    "_",     // origin  0x0080 (NOT ALLOWED) used by pad library
    "_",     // A		0x0100 (NOT ALLOWED) used to force menu
    "b",     // B		0x0200
    "_",     // X		0x0400 (NOT ALLOWED) used by bootloader
    "y",     // Y		0x0800
    "start", // START	0x1000
};

static void trim_string(const char **inout_string, u32 *inout_length) {
    while (*inout_length > 0 && isspace((unsigned char)(*inout_string)[0])) {
        // Trim leading whitespace
        *inout_string += 1;
        *inout_length -= 1;
    }
    while (*inout_length > 0 && isspace((unsigned char)(*inout_string)[*inout_length - 1])) {
        // Trim trailing whitespace
        *inout_length -= 1;
    }
}

static bool get_device_from_string(const char *string, u32 length, device_t *out_device) {
    trim_string(&string, &length);

    if (strncmp("disc_drive", string, length) == 0) {
        *out_device = device_disc_drive;
        return true;
    }
    if (strncmp("flippydrive", string, length) == 0) {
        *out_device = device_flippydrive;
        return true;
    }
    return false;
}

static bool tokenise_string(const char **inout_string, u32 *inout_substring_length, const char *delimeters) {
    bool has_found_any_delimeters = *inout_substring_length > 0;
    if (has_found_any_delimeters) {
        // Advance `string` to the next delimiter
        *inout_string += *inout_substring_length;
        *inout_substring_length = 0;
    }

    while (*inout_substring_length == 0) {
        if ((*inout_string)[0] == '\0') {
            // Reached the end of the input string
            return false;
        }

        if (has_found_any_delimeters) {
            // Advance beyond that delimiter
            *inout_string += 1;
        }
        has_found_any_delimeters = true;

        // Find the next delimiter, and therefore the length of the substring up to that delimiter
        const char *delimeter_ptr = strpbrk(*inout_string, delimeters);
        if (delimeter_ptr) {
            *inout_substring_length = delimeter_ptr - *inout_string;
        } else {
            *inout_substring_length = strlen(*inout_string);
        }
    }

    return true;
}

void load_settings() {
    memset(&settings, 0, sizeof(settings));
    int config_size = get_file_size("/config.ini");
    if (config_size == SD_FAIL) return;

    void *config_buf = memalign(32, config_size + 1);
    if (config_buf == NULL) {
        prog_halt("Could not allocate buffer for config file\n");
        return;
    }

    if (load_file_buffer("/config.ini", config_buf) != SD_OK) {
        prog_halt("Could not find config file\n");
        return;
    }

    ((char*)config_buf)[config_size] = '\0';

    // iprintf("DUMP:\n");
    // iprintf("%s[END]\n\n", (char*)config_buf);

    ini_t *conf = ini_load(config_buf, config_size);

    // cube color
    const char *cube_color_raw = ini_get(conf, "cubeboot", "cube_color");
    if (cube_color_raw != NULL) {
        if (strcmp(cube_color_raw, "random") == 0) {
            settings.cube_color = generate_random_color();
        } else {
            int vars = sscanf(cube_color_raw, "%x", &settings.cube_color);
            if (vars == EOF) settings.cube_color = 0;
            iprintf("Found cube_color = #%x\n", settings.cube_color);
        }
    }

    // cube logo
    const char *cube_logo = ini_get(conf, "cubeboot", "cube_logo");
    if (cube_logo != NULL) {
        iprintf("Found cube_logo = %s\n", cube_logo);
        settings.cube_logo = (char*)cube_logo;
    }

    // default program
    const char *default_program = ini_get(conf, "cubeboot", "default_program");
    if (default_program != NULL) {
        iprintf("Found default_program = %s\n", default_program);
        settings.default_program = (char*)default_program;
    }

    // swiss enable
    int force_swiss_default = 0;
    if (!ini_sget(conf, "cubeboot", "force_swiss_default", "%d", &force_swiss_default)) {
        settings.force_swiss_default = 0;
    } else {
        iprintf("Found force_swiss_default = %d\n", force_swiss_default);
        settings.force_swiss_default = force_swiss_default;
    }

    // progressive enable
    int progressive_enabled = 0;
    if (!ini_sget(conf, "cubeboot", "force_progressive", "%d", &progressive_enabled)) {
        settings.progressive_enabled = 0;
    } else {
        iprintf("Found progressive_enabled = %d\n", progressive_enabled);
        settings.progressive_enabled = progressive_enabled;
    }

    // preboot delay
    u32 preboot_delay_ms = 0;
    if (!ini_sget(conf, "cubeboot", "preboot_delay_ms", "%u", &preboot_delay_ms)) {
        settings.preboot_delay_ms = 0;
    } else {
        iprintf("Found preboot_delay_ms = %u\n", preboot_delay_ms);
        settings.preboot_delay_ms = preboot_delay_ms;
    }

    // postboot delay
    u32 postboot_delay_ms = 0;
    if (!ini_sget(conf, "cubeboot", "postboot_delay_ms", "%u", &postboot_delay_ms)) {
        settings.postboot_delay_ms = 0;
    } else {
        iprintf("Found postboot_delay_ms = %u\n", postboot_delay_ms);
        settings.postboot_delay_ms = postboot_delay_ms;
    }

    // show_watermark
    int show_watermark = 0;
    if (!ini_sget(conf, "cubeboot", "show_watermark", "%d", &show_watermark)) {
        settings.show_watermark = 0;
    } else {
        iprintf("Found show_watermark = %d\n", show_watermark);
        settings.show_watermark = show_watermark;
    }

    // disable_mcp_select
    int disable_mcp_select = 0;
    if (!ini_sget(conf, "cubeboot", "disable_mcp_select", "%d", &disable_mcp_select)) {
        settings.disable_mcp_select = 0;
    } else {
        iprintf("Found disable_mcp_select = %d\n", disable_mcp_select);
        settings.disable_mcp_select = disable_mcp_select;
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

        const char *dol_path = ini_get(conf, "cubeboot", button_config_name);
        if (dol_path != NULL) {
            iprintf("Found %s = %s\n", button_config_name, dol_path);

            settings.boot_buttons[i] = (char*)dol_path;
        }
    }

    // Boot order
    settings.boot_devices_count = 0;
    const char *remaining_boot_order = ini_get(conf, "cubeboot", "boot_order");
    if (remaining_boot_order != NULL) {

        u32 device_string_length = 0;
        while (tokenise_string(&remaining_boot_order, &device_string_length, ",")) {
            device_t found_device;
            if (get_device_from_string(remaining_boot_order, device_string_length, &found_device)) {
                bool already_in_boot_devices = false;
                for (u32 i = 0; i < settings.boot_devices_count; i++) {
                    already_in_boot_devices |= settings.boot_devices[i] == found_device;
                }

                if (!already_in_boot_devices) {
                    settings.boot_devices[settings.boot_devices_count] = found_device;
                    settings.boot_devices_count += 1;
                } else {
                    iprintf("Found duplicate boot device: %.*s\n", device_string_length, remaining_boot_order);
                }
            } else {
                iprintf("Found unknown boot device: %.*s\n", device_string_length, remaining_boot_order);
            }

            if (settings.boot_devices_count >= MAX_BOOT_DEVICES) {
                if (strlen(remaining_boot_order + device_string_length) > 0) {
                    iprintf("Reached max boot devices (%d), skipping any remaining ones in config\n", MAX_BOOT_DEVICES);
                }
                break;
            }
        }
    }

    // // must stay allocated!!
    // free(config_buf);
}
