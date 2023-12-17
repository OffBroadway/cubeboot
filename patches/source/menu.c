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
__attribute_reloc__ GXColorS10 *(*get_save_color)(u32 color_index, s32 save_type);
__attribute_reloc__ model_data *save_icon;
__attribute_reloc__ model_data *save_empty;

// for audio
__attribute_reloc__ void (*Jac_PlaySe)(u32);

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

__attribute_data__ GXColorS10 *menu_color_icon;
__attribute_data__ GXColorS10 *menu_color_icon_sel;

__attribute_data__ GXColorS10 *menu_color_empty;
__attribute_data__ GXColorS10 *menu_color_empty_sel;

__attribute_data__ model textured_icon = {};
__attribute_data__ model single_icon = {};
__attribute_used__ void custom_gameselect_init() {
    single_icon.data = save_empty;
    model_init(&single_icon, 0);

    u32 color_num = SAVE_COLOR_PURPLE;
    u32 color_index = 1 << (10 + 3 + color_num);

    menu_color_icon = get_save_color(color_index, SAVE_ICON);
    menu_color_icon_sel = get_save_color(color_index, SAVE_ICON_SEL);
    menu_color_empty = get_save_color(color_index, SAVE_EMPTY);
    menu_color_empty_sel = get_save_color(color_index, SAVE_EMPTY_SEL);

    single_icon.data->mat[0].tev_color[0] = menu_color_empty;
    single_icon.data->mat[1].tev_color[0] = menu_color_empty;

    textured_icon.data = save_icon;
    model_init(&textured_icon, 0);

    textured_icon.data->mat[0].tev_color[0] = menu_color_icon_sel;
    textured_icon.data->mat[2].tev_color[0] = menu_color_icon_sel;

    // DUMP_COLOR(single_icon.data->mat[0].tev_color[0]);
    // DUMP_COLOR(single_icon.data->mat[2].tev_color[0]);

    // single_icon.data->mat[0].tev_color[0] = &menu_color_sel;
    // single_icon.data->mat[2].tev_color[0] = &menu_color_sel;

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
    {-208.0, 62.0},
    {-152.0, 62.0},
    {-96.0, 62.0},
    {-40.0, 62.0},
    // B
    {40.0, 62.0},
    {96.0, 62.0},
    {152.0, 62.0},
    {208.0, 62.0},

    // A
    {-208.0, 6.0},
    {-152.0, 6.0},
    {-96.0, 6.0},
    {-40.0, 6.0},
    // B
    {40.0, 6.0},
    {96.0, 6.0},
    {152.0, 6.0},
    {208.0, 6.0},

    // A
    {-208.0, -50.0},
    {-152.0, -50.0},
    {-96.0, -50.0},
    {-40.0, -50.0},
    // B
    {40.0, -50.0},
    {96.0, -50.0},
    {152.0, -50.0},
    {208.0, -50.0},
};


// y = top of screen + (row*56)

// test only
coord_pair current_position;
f32 current_scale;

int selected_slot = 0;
__attribute_data__ u32 current_gameselect_state = SUBMENU_GAMESELECT_LOADER;
__attribute_used__ void custom_gameselect_menu(u8 alpha_0, u8 alpha_1, u8 alpha_2) {
    draw_gameselect_menu(alpha_0, alpha_1, alpha_2);
    draw_text("Logo", 10, 10, alpha_2);

    // TODO: move this to reloc
    GXColor white = {0xFF, 0xFF, 0xFF, alpha_2};
    void (*draw_box)(u32 type, u32 param_2, GXColor* param_3) = (void (*)(u32,  u32,  GXColor *))0x81309d24;
    draw_box(make_type('m','e','s','6'), *(u32*)0x814815e4, &white);

    // int base_x = 60 + 5;
    int base_y = 118;

    // f32 mod = (f32)unk2 * 0.2 / 0xFF;
    // f32 sc = 0.8 + mod;

    for (int row = 0; row < 4; row++) {
        for (int col = 0; col < 8; col++) {
            int slot_num = (row * 8) + col;

            f32 sc = 1.3;
            if (slot_num == selected_slot) {
                sc = 2.0; // 1.3;
            }

            // // test only
            // if (slot_num == 0) {
            //     sc = current_scale;
            // }

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
            if (slot_num == selected_slot) {
                pos_x += -10.0;
            }

            if (pos_x == 0.0 && pos_y == 0.0) {
                continue;
            }

            // // test only
            // if (slot_num == 0) {
            //     pos_x = current_position.x;
            //     pos_y = current_position.y;
            // }

            Mtx matrix = {
                {1, 0, 0, pos_x},
                {0, 1, 0, pos_y},
                {0, 0, 1, 0}, // or 2
            };

            model *m = &single_icon;
            if (slot_num == selected_slot) {
                m = &textured_icon;
                matrix[2][3] = 2.0;
            }

            set_obj_pos(m, matrix, scale);
            set_obj_cam(m, get_camera_mtx());
            change_model(m);

            // draw icon
            m->alpha = alpha_1;
            if (slot_num == selected_slot) {
                // cube
                draw_partial(m, &m->data->parts[2]);
                draw_partial(m, &m->data->parts[10]);

                // icon
                draw_partial(m, &m->data->parts[6]);
            } else {
                draw_model(m);
            }
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

static bool first_transition = true;
__attribute_used__ void pre_menu_alpha_setup() {
    menu_alpha_setup(); // run original function

    if (*cur_menu_id == MENU_GAMESELECT_ID && *prev_menu_id == MENU_GAMESELECT_TRANSITION_ID) {
        OSReport("Resetting back to SUBMENU_GAMESELECT_LOADER\n");
        current_gameselect_state = SUBMENU_GAMESELECT_LOADER;

        if (first_transition) {
            Jac_PlaySe(SOUND_MENU_ENTER);
            first_transition = false;
        }
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
    update_icon_positions();

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

    if (pad_status->analog_down & ANALOG_RIGHT) {
        if ((selected_slot % 8) == (8 - 1)) {
            Jac_PlaySe(SOUND_ERROR);
        }
        else {
            Jac_PlaySe(SOUND_MOVE);
            selected_slot++;
        }
    }

    if (pad_status->analog_down & ANALOG_LEFT) {
        if ((selected_slot % 8) == 0) {
            Jac_PlaySe(SOUND_ERROR);
        }
        else {
            Jac_PlaySe(SOUND_MOVE);
            selected_slot--;
        }
    }

    if (pad_status->analog_down & ANALOG_DOWN) {
        if ((32 - selected_slot) <= 8) {
            Jac_PlaySe(SOUND_ERROR);
        }
        else {
            Jac_PlaySe(SOUND_MOVE);
            selected_slot += 8;
        }
    }

    if (pad_status->analog_down & ANALOG_UP) {
        if (selected_slot < 8) {
            Jac_PlaySe(SOUND_ERROR);
        }
        else {
            Jac_PlaySe(SOUND_MOVE);
            selected_slot -= 8;
        }
    }

    return MENU_GAMESELECT_TRANSITION_ID;
}

static int index = 0;
coord_pair positions[] = {
    {20.392272, -0.675339},
    {37.939971, -0.839874},
    {52.764099, -0.519302},
    {64.985626, 0.260773},
    {74.725494, 1.474670},
    {82.104644, 3.96771},
    {87.244079, 5.101395},
    {90.264709, 7.462921},
    {91.287475, 10.155716},
    {90.433410, 13.154114},
    {87.823364, 16.432449},
    {83.578369, 19.965103},
    {77.819397, 23.726394},
    {70.667297, 27.690719},
    {62.243103, 31.832397},
    {52.667785, 36.125793},
    {42.62286, 40.545272},
    {30.547515, 45.65185},
    {18.244445, 49.659851},
    {5.274139, 54.303649},
    {-8.242615, 58.970932},
    {-22.184753, 63.636047},
    {-36.431365, 68.273361},
    {-50.861450, 72.857193},
    {-65.354125, 77.361938},
    {-79.788330, 81.761917},
    {-94.43258, 86.31494},
    {-107.997818, 90.145034},
    {-121.531097, 94.76873},
    {-134.522201, 97.801368},
    {-146.850036, 101.292854},
    {-158.393814, 104.525733},
    {-169.32440, 107.474311},
    {-178.645065, 110.112953},
    {-187.110641, 112.416023},
    {-194.308303, 114.357894},
    {-200.117019, 115.912826},
    {-204.415863, 117.55274},
    {-207.83770, 117.759552},
    {-208.0, 118.0},
    {-207.998870, 118.1701},
    {-207.932403, 118.67581},
    {-207.754669, 118.248855},
    {-207.466400, 118.537826},
    {-207.106567, 118.889259},
    {-206.806350, 119.192565},
    {-206.578231, 119.414680},
    {-206.335739, 119.641052},
    {-206.109313, 119.856590},
    {-205.883880, 120.75172},
    {-205.644668, 120.289070},
    {-205.421691, 120.491142},
    {-205.200073, 120.694397},
    {-204.965316, 120.891571},
    {-204.746887, 121.76156},
    {-204.530197, 121.260055},
    {-204.315338, 121.430999},
    {-204.88287, 121.600013},
    {-203.877594, 121.760887},
    {-203.669067, 121.908477},
    {-203.449157, 122.52284},
    {-203.245498, 122.186935},
    {-203.44342, 122.308235},
    {-202.832672, 122.423988},
    {-202.637069, 122.526481},
    {-202.444305, 122.622329},
    {-202.241912, 122.707717},
    {-202.55328, 122.780227},
    {-201.871902, 122.844512},
    {-201.679809, 122.897850},
    {-201.503189, 122.938919},
    {-201.330001, 122.970375},
    {-201.160339, 122.990112},
    {-200.983367, 122.999412},
    {-200.821289, 122.997406},
    {-200.663024, 122.984710},
    {-200.498474, 122.960495},
    {-200.348327, 122.925071},
    {-200.202224, 122.880188},
    {-200.50933, 122.822952},
    {-199.913436, 122.757171},
    {-199.780227, 122.678627},
    {-199.642944, 122.589500},
    {-199.518798, 122.493370},
    {-199.399139, 122.384040},
    {-199.276535, 122.264801},
    {-199.166366, 122.140228},
    {-199.60867, 122.2288},
    {-198.960113, 121.860183},
    {-198.857910, 121.704757},
    {-198.767089, 121.540954},
    {-198.681152, 121.374809},
    {-198.594940, 121.195625},
    {-198.519256, 121.9208},
    {-198.448577, 120.822326},
    {-198.378784, 120.622947},
    {-198.318603, 120.424346},
    {-198.263549, 120.213737},
    {-198.210510, 119.998123},
    {-198.166152, 119.785156},
    {-198.126983, 119.561126},
    {-198.90972, 119.333564},
    {-198.62622, 119.110466},
    {-198.39550, 118.877471},
    {-198.21743, 118.650093},
    {-198.8575, 118.413703},
    {-198.1693, 118.176368},
    {-198.106, 117.946311},
    {-198.4241, 117.708709},
    {-198.13595, 117.471763},
    {-198.28228, 117.243591},
    {-198.49667, 117.9506},
    {-198.75195, 116.785095},
    {0.0, 0.0},
    {0.0, 0.0},
    {0.0, 0.0},
    {0.0, 0.0},
    {0.0, 0.0},
    {0.0, 0.0},
    {0.0, 0.0},
};

f32 scales[] = {
    1.300000,
    1.300000,
    1.300000,
    1.300000,
    1.300000,
    1.300000,
    1.300000,
    1.300000,
    1.300000,
    1.300000,
    1.300000,
    1.300000,
    1.300000,
    1.300000,
    1.300000,
    1.300000,
    1.300000,
    1.300000,
    1.300000,
    1.300000,
    1.300000,
    1.300000,
    1.300000,
    1.300000,
    1.300000,
    1.300000,
    1.300000,
    1.300000,
    1.300000,
    1.300000,
    1.300000,
    1.300000,
    1.300000,
    1.300000,
    1.300000,
    1.300000,
    1.300000,
    1.300000,
    1.300000,
    1.300000,
    1.351852,
    1.481482,
    1.650000,
    1.818519,
    1.948148,
    2.0,
    2.0,
    2.0,
    2.0,
    2.0,
    2.0,
    2.0,
    2.0,
    2.0,
    2.0,
    2.0,
    2.0,
    2.0,
    2.0,
    2.0,
    2.0,
    2.0,
    2.0,
    2.0,
    2.0,
    2.0,
    2.0,
    2.0,
    2.0,
    2.0,
    2.0,
    2.0,
    2.0,
    2.0,
    2.0,
    2.0,
    2.0,
    2.0,
    2.0,
    2.0,
    2.0,
    2.0,
    2.0,
    2.0,
    2.0,
    2.0,
    2.0,
    2.0,
    2.0,
    2.0,
    2.0,
    2.0,
    2.0,
    2.0,
    2.0,
    2.0,
    2.0,
    2.0,
    2.0,
    2.0,
    2.0,
    2.0,
    2.0,
    2.0,
    2.0,
    2.0,
    2.0,
    2.0,
    2.0,
    2.0,
    2.0,
    2.0,
    2.0,
    0.0,
    0.0,
    0.0,
    0.0,
    0.0,
    0.0,
    0.0,
};

__attribute_used__ void update_icon_positions() {
    current_position = positions[index];
    current_scale = scales[index];

    if (index < 112) index++;
}
