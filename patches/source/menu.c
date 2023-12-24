#include "picolibc.h"
#include "structs.h"

#include "util.h"
#include "reloc.h"
#include "attr.h"

#include <ogc/machine/processor.h>

#include "usbgecko.h"
#include "menu.h"

#include "../build/default_opening_bin.h"
#include "../../cubeboot/include/gcm.h"

// for setup
__attribute_reloc__ void (*menu_alpha_setup)();

// for custom menus
__attribute_reloc__ void (*gx_draw_text)(u16 index, text_group* text, text_draw_group* text_draw, GXColor* color);
__attribute_reloc__ void (*draw_gameselect_menu)(u8 unk0, u8 unk1, u8 unk2);
__attribute_reloc__ GXColorS10 *(*get_save_color)(u32 color_index, s32 save_type);
__attribute_reloc__ void (*setup_gameselect_anim)();
__attribute_reloc__ void (*setup_cube_anim)();
__attribute_reloc__ model_data *save_icon;
__attribute_reloc__ model_data *save_empty;

// for audio
__attribute_reloc__ void (*Jac_PlaySe)(u32);
__attribute_reloc__ void (*Jac_StopSoundAll)();

// for model gx
__attribute_reloc__ void (*model_init)(model* m, int process);
__attribute_reloc__ void (*draw_model)(model* m);
__attribute_reloc__ void (*draw_partial)(model* m, model_part* part);
__attribute_reloc__ void (*change_model)(model* m);
// __attribute_reloc__ void (*gx_load_tex)(u8 texmap_index, tex_data data); // why does this use texture data directly?
__attribute_reloc__ void (*draw_box)(u32 index, box_draw_group* header, GXColor* rasc, int inside_x, int inside_y, int inside_width, int inside_height);

// for camera gx
__attribute_reloc__ void (*set_obj_pos)(model* m, MtxP matrix, guVector vector);
__attribute_reloc__ void (*set_obj_cam)(model* m, MtxP matrix);
__attribute_reloc__ MtxP (*get_camera_mtx)();

// helpers
__attribute_reloc__ f32 (*fast_sin)();
__attribute_reloc__ f32 (*fast_cos)();
__attribute_reloc__ void (*apply_save_rot)(s32 x, s32 y, s32 z, Mtx matrix);
__attribute_reloc__ u32 *bs2start_ready;
__attribute_reloc__ u32 *banner_pointer;
__attribute_reloc__ u32 *banner_ready;

// test only
typedef struct {
    struct gcm_disk_header header;
    u8 banner[0x1960];
    u8 icon_rgb5[160*160*2];
} game_asset;

game_asset *assets;

typedef struct {
    f32 scale;
    Mtx m;
} position_t;

static position_t icons_positions[40];
// static asset_t (*game_assets)[40] = 0x80400000; // needs ~2.5mb

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

__attribute_data__ u16 anim_step = 0;

__attribute_data__ GXColorS10 *menu_color_icon;
__attribute_data__ GXColorS10 *menu_color_icon_sel;

__attribute_data__ GXColorS10 *menu_color_empty;
__attribute_data__ GXColorS10 *menu_color_empty_sel;

__attribute_data__ model global_textured_icon = {};
__attribute_data__ model global_empty_icon = {};

// pointers
model *textured_icon = &global_textured_icon;
model *empty_icon = &global_empty_icon;

void set_empty_icon_selected() {
    empty_icon->data->mat[0].tev_color[0] = menu_color_empty_sel;
    empty_icon->data->mat[1].tev_color[0] = menu_color_empty_sel;
}

void set_empty_icon_unselected() {
    empty_icon->data->mat[0].tev_color[0] = menu_color_empty;
    empty_icon->data->mat[1].tev_color[0] = menu_color_empty;
}

void set_textured_icon_selected() {
    textured_icon->data->mat[0].tev_color[0] = menu_color_icon_sel;
    textured_icon->data->mat[2].tev_color[0] = menu_color_icon_sel;
}

void set_textured_icon_unselected() {
    textured_icon->data->mat[0].tev_color[0] = menu_color_icon;
    textured_icon->data->mat[2].tev_color[0] = menu_color_icon;
}

__attribute_used__ void custom_gameselect_init() {
    // default banner
    *banner_pointer = (u32)&default_opening_bin[0];
    *banner_ready = 1;

    u32 color_num = SAVE_COLOR_PURPLE;
    u32 color_index = 1 << (10 + 3 + color_num);

    // colors
    menu_color_icon = get_save_color(color_index, SAVE_ICON);
    menu_color_icon_sel = get_save_color(color_index, SAVE_ICON_SEL);
    menu_color_empty = get_save_color(color_index, SAVE_EMPTY);
    menu_color_empty_sel = get_save_color(color_index, SAVE_EMPTY_SEL);

    DUMP_COLOR(menu_color_icon);
    DUMP_COLOR(menu_color_icon_sel);
    DUMP_COLOR(menu_color_empty);
    DUMP_COLOR(menu_color_empty_sel);

    // empty icon
    empty_icon->data = save_empty;
    model_init(empty_icon, 0);
    set_empty_icon_unselected();

    // textured icon
    textured_icon->data = save_icon;
    model_init(textured_icon, 0);
    set_textured_icon_unselected();

    // change the texture format (disc scans)
    tex_data *textured_icon_tex = &textured_icon->data->tex->dat[1];
    textured_icon_tex->format = GX_TF_RGB5A3;
    textured_icon_tex->width = 160;
    textured_icon_tex->height = 160;
}

int selected_slot = 0;

__attribute_used__ void draw_save_icon(u32 index, u8 alpha, bool selected, bool has_texture) {
    position_t *pos = &icons_positions[index];

    f32 sc = pos->scale;
    guVector scale = {sc, sc, sc};

    model *m = NULL;
    if (has_texture) {
        m = textured_icon;
        if (selected) {
            set_textured_icon_selected();
        } else {
            set_textured_icon_unselected();
        }
    } else {
        m = empty_icon;
        if (selected) {
            set_empty_icon_selected();
        } else {
            set_empty_icon_unselected();
        }
    }

    // setup camera
    set_obj_pos(m, pos->m, scale);
    set_obj_cam(m, get_camera_mtx());
    change_model(m);

    // draw icon
    m->alpha = alpha;
    if (has_texture) {
        // cube
        draw_partial(m, &m->data->parts[2]);
        draw_partial(m, &m->data->parts[10]);

        // icon
        tex_data *icon_tex = &m->data->tex->dat[1];
        u32 target_texture_data = (u32)assets[index].icon_rgb5;

        s32 desired_offset = (s32)((u32)target_texture_data - (u32)icon_tex);
        icon_tex->offset = desired_offset;

        // TODO: instead set m->data->mat[1].texmap_index[0] = 0xFFFF
        draw_partial(m, &m->data->parts[6]);
    } else {
        draw_model(m);
    }

    return;
}

__attribute_used__ void update_icon_positions() {
    const int base_x = -208;
    const int base_y = 118;

    for (int row = 0; row < 4; row++) {
        for (int col = 0; col < 8; col++) {
            int slot_num = (row * 8) + col;
            position_t *pos = &icons_positions[slot_num];

            f32 sc = 1.3;
            if (slot_num == selected_slot) {
                sc = 2.0;
            }
            pos->scale = sc;

            f32 pos_x = base_x + (col * 56);
            if (slot_num % 8 >= 4) pos_x += 24; // card spacing
            f32 pos_y = base_y - (row * 56);

            C_MTXIdentity(pos->m);

            if (slot_num == selected_slot) {
                f32 mult = 0.7; // 1.0 is more accurate
                s32 rot_diff_x = fast_cos(anim_step * 70) * 350 * mult;
                s32 rot_diff_y = fast_cos(anim_step * 35 - 15000) * 1000 * mult;
                s32 rot_diff_z = fast_cos(anim_step * 35) * 1000 * mult;

                apply_save_rot(rot_diff_x, rot_diff_y, rot_diff_z, pos->m);

                f32 move_diff_y = fast_sin(35 * anim_step - 0x4000) * 10.0 * mult;
                f32 move_diff_z = fast_sin(70 * anim_step) * 5.0 * mult;

                pos->m[0][3] = pos_x + move_diff_y;
                pos->m[1][3] = pos_y - move_diff_z;
                pos->m[2][3] = 2.0;
            } else {
                pos->m[0][3] = pos_x;
                pos->m[1][3] = pos_y;
                pos->m[2][3] = 1.0;
            }
        }
    }

    anim_step += 0x7; // why is this the const?
}

__attribute_data__ u32 current_gameselect_state = SUBMENU_GAMESELECT_LOADER;
__attribute_used__ void custom_gameselect_menu(u8 alpha_0, u8 alpha_1, u8 alpha_2) {
    draw_gameselect_menu(alpha_0, alpha_1, alpha_2);
    draw_text("cubeboot loader", 20, 4, alpha_2);

    static struct {
        box_draw_group group;
        box_draw_metadata metadata;
    } blob = {
        .group = {
            .type = make_type('G','L','H','0'),
            .metadata_offset = sizeof(box_draw_group),
        },
        .metadata = {
            .center_x = 0x1230,
            .center_y = 0x1640,
            .width = 0x20f0,
            .height = 0x560,

            .inside_center_x = 0x80,
            .inside_center_y = 0x80,
            .inside_width = 0x1ff0,
            .inside_height = 0x460,

            .boarder_index = { 0x28, 0x28, 0x28, 0x28 },
            .boarder_unk = { 0x27, 0x0, 0x0, 0x0 },

            .top_color = {},
            .bottom_color = {},
        }
    };

    GXColor white = {0xFF, 0xFF, 0xFF, alpha_2};
    GXColor top_color = {0x6e, 0x00, 0xb3, 0xc8};
    GXColor bottom_color = {0x80, 0x00, 0x57, 0xb4};
    copy_gx_color(&top_color, &blob.metadata.top_color[0]);
    copy_gx_color(&top_color, &blob.metadata.top_color[1]);
    copy_gx_color(&bottom_color, &blob.metadata.bottom_color[0]);
    copy_gx_color(&bottom_color, &blob.metadata.bottom_color[1]);

    box_draw_metadata *box = (box_draw_metadata*)((u32)&blob + blob.group.metadata_offset);
	int inside_x = box->center_x - (box->inside_width / 2);
	int inside_y = box->center_y - (box->inside_height / 2);
	draw_box(0, &blob.group, &white, inside_x, inside_y, box->inside_width, box->inside_height);

    for (int pass = 0; pass < 2; pass++) {
        for (int row = 0; row < 4; row++) {
            for (int col = 0; col < 8; col++) {
                int slot_num = (row * 8) + col;

                bool has_texture = (slot_num < 4);
                bool selected = (slot_num == selected_slot);

                if (selected && pass == 0) continue;
                if (!selected && pass == 1) continue;
                draw_save_icon(slot_num, alpha_1, selected, has_texture);
            }
        }
    }
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

    if (pad_status->buttons_down & PAD_TRIGGER_Z) {
        // placeholder
    }

    if (pad_status->buttons_down & PAD_BUTTON_B) {
        if (current_gameselect_state == SUBMENU_GAMESELECT_START) {
            current_gameselect_state = SUBMENU_GAMESELECT_LOADER;
            Jac_PlaySe(SOUND_SUBMENU_EXIT);
        } else {
            anim_step = 0; // anim reset
            *banner_pointer = (u32)&default_opening_bin[0]; // banner reset
            return MENU_GAMESELECT_ID;
        }
    }

    if (pad_status->buttons_down & PAD_BUTTON_A && current_gameselect_state == SUBMENU_GAMESELECT_LOADER) {
        if (selected_slot < 4) {
            current_gameselect_state = SUBMENU_GAMESELECT_START;
            *banner_pointer = (u32)&assets[selected_slot].banner[0]; // banner buf

            Jac_PlaySe(SOUND_SUBMENU_ENTER);
            setup_gameselect_anim();
            setup_cube_anim();
        }
    }

    if (pad_status->buttons_down & PAD_BUTTON_START && current_gameselect_state == SUBMENU_GAMESELECT_START) {
        Jac_StopSoundAll();
        Jac_PlaySe(SOUND_MENU_FINAL);
        *bs2start_ready = 1;
    }

    if (current_gameselect_state == SUBMENU_GAMESELECT_LOADER) {
        if (pad_status->analog_down & ANALOG_RIGHT) {
            if ((selected_slot % 8) == (8 - 1)) {
                Jac_PlaySe(SOUND_CARD_ERROR);
            }
            else {
                Jac_PlaySe(SOUND_CARD_MOVE);
                selected_slot++;
            }
        }

        if (pad_status->analog_down & ANALOG_LEFT) {
            if ((selected_slot % 8) == 0) {
                Jac_PlaySe(SOUND_CARD_ERROR);
            }
            else {
                Jac_PlaySe(SOUND_CARD_MOVE);
                selected_slot--;
            }
        }

        if (pad_status->analog_down & ANALOG_DOWN) {
            if ((32 - selected_slot) <= 8) {
                Jac_PlaySe(SOUND_CARD_ERROR);
            }
            else {
                Jac_PlaySe(SOUND_CARD_MOVE);
                selected_slot += 8;
            }
        }

        if (pad_status->analog_down & ANALOG_UP) {
            if (selected_slot < 8) {
                Jac_PlaySe(SOUND_CARD_ERROR);
            }
            else {
                Jac_PlaySe(SOUND_CARD_MOVE);
                selected_slot -= 8;
            }
        }
    }

    return MENU_GAMESELECT_TRANSITION_ID;
}
