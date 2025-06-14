#include "device_selector.h"

#include "bs2.h"
#include "button_descriptions.h"
#include "custom_ui_blob.h"
#include "draw.h"
#include "element_alpha.h"
#include "menu.h"
#include "reloc.h"
#include "structs.h"

#include "l_button_bti.h"
#include "r_button_bti.h"
#include "disc_bti.h"
#include "flippydrive_bti.h"

#include <ogc/gx.h>

static element_alpha_state_t device_icons_alpha = (element_alpha_state_t){ .current_alpha = 0, .fade_duration = 20, .start_delay = 0, .max_output = 0xFF };
static element_alpha_state_t disc_drive_icon_alpha = (element_alpha_state_t){ .current_alpha = 0, .fade_duration = 20, .start_delay = 0, .max_output = 0xFF };
static element_alpha_state_t flippydrive_icon_alpha = (element_alpha_state_t){ .current_alpha = 0, .fade_duration = 20, .start_delay = 0, .max_output = 0xFF };

void draw_device_icons() {
    u16 device_icons_output_alpha;
    get_element_alpha(&device_icons_alpha, &device_icons_output_alpha, NULL);

    if (device_icons_output_alpha > 0) {
        u16 disc_drive_icon_output_alpha;
        u16 flippydrive_icon_output_alpha;
        get_element_alpha(&disc_drive_icon_alpha, &disc_drive_icon_output_alpha, NULL);
        get_element_alpha(&flippydrive_icon_alpha, &flippydrive_icon_output_alpha, NULL);

        disc_drive_icon_output_alpha = (disc_drive_icon_output_alpha * device_icons_output_alpha) / disc_drive_icon_alpha.max_output;
        flippydrive_icon_output_alpha = (flippydrive_icon_output_alpha * device_icons_output_alpha) / disc_drive_icon_alpha.max_output;

        setup_buttons_matrix();
        GXColor disc_color = {0xFF, 0xFF, 0xFF, disc_drive_icon_output_alpha};
        GXColor flippydrive_color = {0xFF, 0xFF, 0xFF, flippydrive_icon_output_alpha};

        // TODO: What parameters are appropriate?!
        setup_tex_draw(1, 0, 1); // 101 / 110 / 100

        draw_blob_tex(L_BUTTON_BLOB_TYPE, custom_ui_blob, &disc_color, (const tex_data *)l_button_bti);
        draw_blob_tex(DISC_BLOB_TYPE, custom_ui_blob, &disc_color, (const tex_data *)disc_bti);
        draw_blob_tex(R_BUTTON_BLOB_TYPE, custom_ui_blob, &flippydrive_color, (const tex_data *)r_button_bti);
        draw_blob_tex(FLIPPYDRIVE_BLOB_TYPE, custom_ui_blob, &flippydrive_color, (const tex_data *)flippydrive_bti);
    }
}

void update_device_icon_alphas() {
    bool should_show_device_icons = *next_menu_id == MENU_GAMESELECT_ID;
    update_element_alpha(&device_icons_alpha, should_show_device_icons ? element_alpha_visible : element_alpha_hidden);

    update_element_alpha(&disc_drive_icon_alpha, is_disc_drive_selected ? element_alpha_visible : element_alpha_dimmed);
    update_element_alpha(&flippydrive_icon_alpha, !is_disc_drive_selected ? element_alpha_visible : element_alpha_dimmed);
}
