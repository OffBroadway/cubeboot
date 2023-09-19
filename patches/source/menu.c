#include "picolibc.h"
#include "structs.h"

// #include "util.h"
#include "reloc.h"
#include "attr.h"

#include <ogc/machine/processor.h>

#include "usbgecko.h"
#include "menu.h"

// for setup
__attribute_reloc__ void (*menu_alpha_setup)();

// for custom menus
__attribute_reloc__ void (*gx_draw_text)(u16 index, text_group* text, text_draw_group* text_draw, GXColor* color);
__attribute_reloc__ void (*draw_gameselect_menu)(u8 unk0, u8 unk1, u8 unk2);
__attribute_reloc__ model_data *save_icon;
__attribute_reloc__ model_data *save_empty;

// for model gx
__attribute_reloc__ void (*model_init)(model* m, int process);
__attribute_reloc__ void (*draw_model)(model* m);
__attribute_reloc__ void (*draw_partial)(model* m, model_part* part);
__attribute_reloc__ void (*change_model)(model* m);

// for camera gx
__attribute_reloc__ void (*set_obj_pos)(model* m, MtxP matrix, guVector vector);
__attribute_reloc__ void (*set_obj_cam)(model* m, MtxP matrix);
__attribute_reloc__ MtxP (*get_camera_mtx)();

void draw_text(char *s, u16 x, u16 y, u8 alpha) {
    static struct {
        text_group group;
        text_metadata metadata;
        char contents[255];
    } text = {
        .group = {
            .type = make_type('S','T','H','0'),
            .arr_size = 1, // arr size
        },
        .metadata = {
            .draw_metadata_index = 0,
            .text_data_offset = sizeof(text_metadata),
        },
    };

    static struct {
        text_draw_group group;
        text_draw_metadata metadata;
    } draw = {
        .group = {
            .type = make_type('G','L','H','0'),
            .metadata_offset = sizeof(text_draw_group),
        },
        .metadata = {
            .type = make_type('m','e','s','g'),
            .x = 0, // x position
            .y = 0, // y position
            .y_align = TEXT_ALIGN_CENTER,
            .x_align = TEXT_ALIGN_TOP,
            .letter_spacing = -1,
            .line_spacing = 20,
            .size = 20,
            .border_obj = 0xffff,
        }
    };

    strcpy(text.contents, s);

    draw.metadata.x = (x + 64) * 20;
    draw.metadata.y = (y + 64) * 10;

    GXColor white = {0xFF, 0xFF, 0xFF, alpha};
    gx_draw_text(0, &text.group, &draw.group, &white);
}

__attribute_data__ GXColorS10 menu_color_none = {0x00, 0x25, 0x00, 0xFF};
__attribute_data__ GXColorS10 menu_color_sel = {0x00, 0x69, 0x46, 0xFF};
__attribute_data__ model single_icon = {};
__attribute_used__ void custom_gameselect_init() {
    single_icon.data = save_empty;
    model_init(&single_icon, 0);

    // DUMP_COLOR(single_icon.data->mat[0].tev_color[0]);
    // DUMP_COLOR(single_icon.data->mat[2].tev_color[0]);

    // single_icon.data->mat[0].tev_color[0] = &menu_color_sel;
    // single_icon.data->mat[2].tev_color[0] = &menu_color_sel;

    single_icon.data->mat[0].tev_color[0] = &menu_color_none;
    single_icon.data->mat[1].tev_color[0] = &menu_color_none;
}

//// with texture
// __attribute_used__ void custom_gameselect_init() {
//     single_icon.data = save_icon;
//     model_init(&single_icon, 0);

//     DUMP_COLOR(single_icon.data->mat[0].tev_color[0]);
//     DUMP_COLOR(single_icon.data->mat[2].tev_color[0]);

//     single_icon.data->mat[0].tev_color[0] = &color_cube;
//     single_icon.data->mat[2].tev_color[0] = &color_cube;

//     // tex_data *base = single_icon.data->tex->dat;
//     // for (int i = 0; i < 8; i++) {
//     //     tex_data *p = base + i;
//     //     // void *img_ptr = (void*)((u8*)base + p->offset + (i * 0x20));
//     //     OSReport("Icon tex, %u\n", p->format);
//     // }
// }

typedef struct coord_pair {
    f32 x;
    f32 y;
} coord_pair;

__attribute_data__ coord_pair stored_slot_coords[2][128] = {{}, {}};

__attribute_data__ f32 stored_slot_scale[2][128] = {{}, {}};

__attribute_data__ coord_pair slot_coords[32] = {
    // A
    {-208.0, 118.0},
    {-152.0, 118.0},
    {-96.0, 118.0},
    {-40.0, 118.0},
    // B
    {40.0, 118.0},
    {96.0, 118.0},
    {152.0, 118.0},
    {208.0, 118.0},

    // A
    {-152.0, 62.0},
    {-208.0, 62.0},
    {-40.0, 62.0},
    {-96.0, 62.0},
    // B
    {96.0, 62.0},
    {40.0, 62.0},
    {208.0, 62.0},
    {152.0, 62.0},

    // A
    {-152.0, 6.0},
    {-208.0, 6.0},
    {-40.0, 6.0},
    {-96.0, 6.0},
    // B
    {96.0, 6.0},
    {40.0, 6.0},
    {208.0, 6.0},
    {152.0, 6.0},

    // A
    {-152.0, -50.0},
    {-208.0, -50.0},
    {-40.0, -50.0},
    {-96.0, -50.0},
    // B
    {96.0, -50.0},
    {40.0, -50.0},
    {208.0, -50.0},
    {152.0, -50.0},
};


// y = top of screen + (row*56)

__attribute_data__ u32 current_gameselect_state = SUBMENU_GAMESELECT_LOADER;
__attribute_used__ void custom_gameselect_menu(u8 unk0, u8 unk1, u8 unk2) {
    draw_gameselect_menu(unk0, unk1, unk2);
    draw_text("Logo", 10, 10, unk0);

    // int base_x = 60 + 5;
    int base_y = 118;

    // f32 mod = (f32)unk2 * 0.2 / 0xFF;
    // f32 sc = 0.8 + mod;

    for (int row = 0; row < 4; row++) {
        for (int col = 0; col < 8; col++) {
            int slot_num = (row * 8) + col;

            f32 sc = 1.3;
            if (slot_num == 0) {
                sc = 1.5; // 1.3;
            }

            guVector scale = {sc, sc, sc};


            // // draw setup
            // Mtx matrix = {
            //     { 1, 0, 0, -292 + (base_x + (col * 65)) },
            //     { 0, 1, 0, 224 - (base_y + (row * 60)) },
            //     { 0, 0 , 1, 0 },
            // };

            // s16 pos_x = -292 + (base_x + (col * 65));
            // s16 pos_y = 224 - (base_y + (row * 60));

            // stored_slot_coords[slot_num].x = pos_x;
            // stored_slot_coords[slot_num].y = pos_y;

            // s16 pos_x = -200 + 50 + (slot_coords[slot_num].x / 20 * 8 / 6);
            // s16 pos_y = -200 - 50 + (slot_coords[slot_num].y / 20 * 8 / 6);

            f32 pos_x = slot_coords[slot_num].x;
            f32 pos_y = base_y - (row * 56); // slot_coords[slot_num].y;
            if (slot_num == 0) {
                pos_x += -10.0;
            }

            if (pos_x == 0.0 && pos_y == 0.0) {
                continue;
            }

            Mtx matrix = {
                {1, 0, 0, pos_x},
                {0, 1, 0, pos_y},
                {0, 0, 1, 0}, // or 2
            };

            if (slot_num == 0) {
                matrix[2][3] = 2.0;
            }

            set_obj_pos(&single_icon, matrix, scale);
            set_obj_cam(&single_icon, get_camera_mtx());
            change_model(&single_icon);

            // draw icon
            single_icon.alpha = unk1;
            // draw_save_empty(&single_icon, 1, 1);
            draw_model(&single_icon);
        }
    }

    // if (*(u32*)0x80000000 == 0xFEEDFACE) {
    //     for (int slot_num = 0; slot_num < 32; slot_num++) {
    //         s16 pos_x = stored_slot_coords[slot_num].x;
    //         s16 pos_y = stored_slot_coords[slot_num].y;
    //         custom_OSReport("(%d) %d %d\n", slot_num, pos_x, pos_y);
    //     }
    //     ppchalt();
    // }
}

__attribute_used__ void pre_menu_alpha_setup() {
    menu_alpha_setup(); // run original function

    if (*cur_menu_id == MENU_GAMESELECT_ID && *prev_menu_id == MENU_GAMESELECT_TRANSITION_ID) {
        OSReport("Resetting back to SUBMENU_GAMESELECT_LOADER\n");
        current_gameselect_state = SUBMENU_GAMESELECT_LOADER;
    }
}

__attribute_used__ u32 is_gameselect_draw() {
    return current_gameselect_state == SUBMENU_GAMESELECT_START;
}

__attribute_used__ void mod_gameselect_draw(u8 unk0, u8 unk1, u8 unk2) {
    switch(current_gameselect_state) {
        case SUBMENU_GAMESELECT_LOADER:
            custom_gameselect_menu(unk0, unk1, unk2);
            break;
        case SUBMENU_GAMESELECT_START:
            draw_gameselect_menu(unk0, unk1, unk2);
            break;
        default:
    }
}

__attribute_used__ s32 handle_gameselect_inputs() {
    if (pad_status->buttons_down & PAD_BUTTON_B) {
        if (current_gameselect_state == SUBMENU_GAMESELECT_START) {
            current_gameselect_state = SUBMENU_GAMESELECT_LOADER;
        } else {
            return MENU_GAMESELECT_ID;
        }
    }

    if (pad_status->buttons_down & PAD_BUTTON_A && current_gameselect_state == SUBMENU_GAMESELECT_LOADER) {
        current_gameselect_state = SUBMENU_GAMESELECT_START;
    }

    return MENU_GAMESELECT_TRANSITION_ID;
}
