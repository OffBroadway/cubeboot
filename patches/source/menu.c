#include <math.h>
#include "picolibc.h"
#include "structs.h"

#include "util.h"
#include "reloc.h"
#include "attr.h"

#include <ogc/machine/processor.h>

#include "usbgecko.h"
#include "menu.h"
#include "grid.h"
#include "games.h"
#include "gameid.h"

#include "dolphin_dvd.h"

#include "dir_tex_bin.h"
#include "dol_tex_bin.h"
#include "font.h"
#include "boot.h"
#include "ipl.h"
#include "os.h"

#include "time.h"

// TODO: this is all zeros except for one BNRDesc, so replace it with a sparse version
#include "default_opening_bin.h"
#include "gcm.h"
#include "bnr.h"

// for setup
__attribute_reloc__ void (*menu_alpha_setup)();

// for custom menus
__attribute_reloc__ void (*prep_text_mode)();
__attribute_reloc__ void (*gx_draw_text)(u16 index, text_group* text, text_draw_group* text_draw, GXColor* color);
__attribute_reloc__ void (*setup_gameselect_menu)(u8 alpha_0, u8 alpha_1, u8 alpha_2);
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

// for menu elements
__attribute_reloc__ void (*draw_grid)(Mtx position, u8 alpha);
__attribute_reloc__ void (*draw_box)(u32 index, box_draw_group* header, GXColor* texa, int inside_x, int inside_y, int inside_width, int inside_height);
// __attribute_reloc__ void (*draw_start_info)(u8 alpha);
__attribute_reloc__ void (*draw_start_anim)(u8 alpha);
__attribute_reloc__ void (*draw_blob_fixed)(void *blob_ptr, void *blob_a, void *blob_b, GXColor *color);
__attribute_reloc__ void (*draw_blob_text)(u32 type, void *blob, GXColor *color, char *str, s32 len);
__attribute_reloc__ void (*draw_blob_text_long)(u32 type, void *blob, GXColor *color, char *str, s32 len);
__attribute_reloc__ void (*draw_blob_border)(u32 type, void *blob, GXColor *color);
__attribute_reloc__ void (*draw_blob_tex)(u32 type, void *blob, GXColor *color, tex_data *dat);
__attribute_reloc__ void (*setup_tex_draw)(s32 unk0, s32 unk1, s32 unk2);
__attribute_reloc__ void (*draw_named_tex)(u32 type, void *blob, GXColor *color, s16 x, s16 y);

// unknown blob (from memcard menu)
__attribute_reloc__ void **ptr_menu_blob;
__attribute_data__ void *menu_blob = NULL;

// unknown blobs (from gameselect menu)
__attribute_reloc__ void *game_blob_text;
__attribute_reloc__ void **ptr_game_blob_a;
__attribute_data__ void *game_blob_a = NULL;
__attribute_reloc__ void **ptr_game_blob_b;
__attribute_data__ void *game_blob_b = NULL;

// for camera gx
__attribute_reloc__ void (*set_obj_pos)(model* m, MtxP matrix, guVector vector);
__attribute_reloc__ void (*set_obj_cam)(model* m, MtxP matrix);
__attribute_reloc__ MtxP (*get_camera_mtx)();

// helpers
__attribute_reloc__ f32 (*fast_sin)(s16 deg);
__attribute_reloc__ f32 (*fast_cos)(s16 deg);
__attribute_reloc__ void (*apply_save_rot)(s32 x, s32 y, s32 z, Mtx matrix);
__attribute_reloc__ u32 *bs2start_ready;
__attribute_reloc__ u32 *banner_pointer;
__attribute_reloc__ u32 *banner_ready;

typedef struct {
    f32 scale;
    f32 opacity;
    Mtx m;
} position_t;

static position_t icons_positions[8];

typedef struct {
    s32 rot_diff_x;
    s32 rot_diff_y;
    s32 rot_diff_z;

    f32 move_diff_y;
    f32 move_diff_z;
} selected_mod_t;

static selected_mod_t selected_icon_mod;

// Define constants for max dimensions
void setup_icon_positions();

__attribute__((aligned(4))) static tex_data icon_texture;
__attribute__((aligned(4))) static tex_data banner_texture;

void draw_text(char *s, s16 size, u16 x, u16 y, GXColor *color) {
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
            .line_spacing = 0,
            .size = 0,
            .border_obj = 0xffff,
        }
    };

    strcpy(text.contents, s);

    draw.metadata.size = draw.metadata.line_spacing = size;
    draw.metadata.x = (x + 64) * 20;
    draw.metadata.y = (y + 64) * 10;

    gx_draw_text(0, &text.group, &draw.group, color);
}

__attribute_data__ u16 anim_step = 0;

__attribute_data__ GXColorS10 *menu_color_icon;
__attribute_data__ GXColorS10 *menu_color_icon_sel;

__attribute_data__ GXColorS10 *menu_color_empty;
__attribute_data__ GXColorS10 *menu_color_empty_sel;

__attribute_data__ model global_textured_icon = {};
__attribute_data__ model global_empty_icon = {};
// __attribute_data__ BNR global_start_banner = {};

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

    // menu setup
    menu_blob = *ptr_menu_blob;
    game_blob_a = *ptr_game_blob_a;
    game_blob_b = *ptr_game_blob_b;


    // if (*banner_pointer) {
    //     char *names[] = {"blue", "green", "yellow", "orange", "red", "purple"};
    //     u32 colors[] = {SAVE_COLOR_BLUE, SAVE_COLOR_GREEN, SAVE_COLOR_YELLOW, SAVE_COLOR_ORANGE, SAVE_COLOR_RED, SAVE_COLOR_PURPLE};
    //     for (int i = 0; i < countof(colors); i++) {
    //         u32 color_num = colors[i];
    //         u32 color_index = 1 << (10 + 3 + color_num);
    //         GXColorS10 *color_bright = get_save_color(color_index, SAVE_ICON);
    //         GXColorS10 *color_bright_seleted = get_save_color(color_index, SAVE_ICON_SEL);
    //         GXColorS10 *color_dim = get_save_color(color_index, SAVE_EMPTY);
    //         GXColorS10 *color_dim_selected = get_save_color(color_index, SAVE_EMPTY_SEL);
    //         OSReport("color = %s\n", names[i]);

    //         DUMP_COLOR(color_bright);
    //         DUMP_COLOR(color_bright_seleted);
    //         DUMP_COLOR(color_dim);
    //         DUMP_COLOR(color_dim_selected);
    //     }

    //     while(1);
    // }

    // colors
    u32 color_num = SAVE_COLOR_PURPLE; // TODO: make a setting for this
    u32 color_index = 1 << (10 + 3 + color_num);
    menu_color_icon = get_save_color(color_index, SAVE_ICON);
    menu_color_icon_sel = get_save_color(color_index, SAVE_ICON_SEL);
    menu_color_empty = get_save_color(color_index, SAVE_EMPTY);
    menu_color_empty_sel = get_save_color(color_index, SAVE_EMPTY_SEL);

    // DUMP_COLOR(menu_color_icon);
    // DUMP_COLOR(menu_color_icon_sel);
    // DUMP_COLOR(menu_color_empty);
    // DUMP_COLOR(menu_color_empty_sel);

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
    textured_icon_tex->width = 64;
    textured_icon_tex->height = 64;

    // icon texture
    icon_texture.format = GX_TF_RGB5A3;
    icon_texture.width = 32;
    icon_texture.height = 32;

    icon_texture.lodbias = 0; // used by GX_InitTexObjLOD
    icon_texture.index = 0x00;

    icon_texture.unk1 = 0x00;
    icon_texture.unk2 = 0x00;
    icon_texture.unk3 = 0x00;
    icon_texture.unk4 = 0x00;
    icon_texture.unk5 = 0x00;
    icon_texture.unk6 = 0x00;
    icon_texture.unk7 = 0x01; // used by GX_InitTexObjLOD
    icon_texture.unk8 = 0x01; // used by GX_InitTexObjLOD
    icon_texture.unk9 = 0x00;
    icon_texture.unk10 = 0x00;

    // banner image
    banner_texture.format = GX_TF_RGB5A3;
    banner_texture.width = 96;
    banner_texture.height = 32;

    banner_texture.lodbias = 0; // used by GX_InitTexObjLOD
    banner_texture.index = 0x00;

    banner_texture.unk1 = 0x00;
    banner_texture.unk2 = 0x00;
    banner_texture.unk3 = 0x00;
    banner_texture.unk4 = 0x00;
    banner_texture.unk5 = 0x00;
    banner_texture.unk6 = 0x00;
    banner_texture.unk7 = 0x01; // used by GX_InitTexObjLOD
    banner_texture.unk8 = 0x01; // used by GX_InitTexObjLOD
    banner_texture.unk9 = 0x00;
    banner_texture.unk10 = 0x00;

    // // init anim list
    // ????

    // icon positions
    setup_icon_positions();
}

int selected_slot = 0;
int top_line_num = 0;

__attribute_used__ void draw_save_icon(position_t *pos, u32 slot_num, u8 alpha, bool selected) {
    f32 sc = pos->scale;
    guVector scale = {sc, sc, sc};
    bool has_texture = false;

    gm_file_entry_t *entry = gm_get_game_entry(slot_num);
    if (entry != NULL) {
        if (entry->type == GM_FILE_TYPE_PROGRAM || entry->type == GM_FILE_TYPE_DIRECTORY) {
            has_texture = true;
        } else if (entry->asset.use_banner && entry->asset.banner.state == GM_LOAD_STATE_LOADED) {
            has_texture = true;
        } else if (entry->asset.icon.state == GM_LOAD_STATE_LOADED) {
            has_texture = true;
        }
    }

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
    m->alpha = (u8)((f32)alpha * pos->opacity);
    if (has_texture) {
        // cube
        draw_partial(m, &m->data->parts[2]);
        draw_partial(m, &m->data->parts[10]);

        // icon
        tex_data *icon_tex = &m->data->tex->dat[1];
        if (entry->type == GM_FILE_TYPE_PROGRAM || entry->type == GM_FILE_TYPE_DIRECTORY || entry->asset.icon.state != GM_LOAD_STATE_NONE) {
            u32 target_texture_data = 0;
            if (entry->asset.icon.state == GM_LOAD_STATE_NONE) {
                const uint8_t *default_icon = entry->type == GM_FILE_TYPE_DIRECTORY ? &dir_tex_bin[0] : &dol_tex_bin[0];
                target_texture_data = (u32)default_icon;
            } else {
                target_texture_data = (u32)entry->asset.icon.buf->data;
            }

            s32 desired_offset = (s32)((u32)target_texture_data - (u32)icon_tex);
            icon_tex->offset = desired_offset;
            icon_tex->format = GX_TF_RGB5A3;
            icon_tex->width = 32;
            icon_tex->height = 32;
        } else {
            u16 *source_texture_data = (u16*)entry->asset.banner.buf->data;
            u32 target_texture_data = (u32)source_texture_data;

            s32 desired_offset = (s32)((u32)target_texture_data - (u32)icon_tex);
            icon_tex->offset = desired_offset;
            icon_tex->format = GX_TF_RGB5A3;
            icon_tex->width = 96;
            icon_tex->height = 32;
        }

        // TODO: instead set m->data->mat[1].texmap_index[0] = 0xFFFF
        draw_partial(m, &m->data->parts[6]);
    } else {
        draw_model(m);
    }

    return;
}

inline u16 get_border_index() {
    u16 border_index = 0;
    switch (get_ipl_revision()) {
    case IPL_NTSC_10_001:
    case IPL_NTSC_10_002:
    case IPL_NTSC_11_001:
    case IPL_NTSC_12_001:
    case IPL_NTSC_12_101:
        border_index = 0x28;
        break;
    case IPL_PAL_10_001:
    case IPL_PAL_10_002:
    case IPL_PAL_12_101:
        border_index = 0x4b;
        break;
    case IPL_MPAL_11:
        border_index = 0x12;
        break;
    default:
        break;
    }

    return border_index;
}

__attribute_used__ void draw_info_box(u16 width, u16 height, u16 center_x, u16 center_y, u8 alpha, GXColor *top_color, GXColor *bottom_color) {
    struct {
        box_draw_group group;
        box_draw_metadata metadata;
    } blob = {
        .group = {
            .type = make_type('G','L','H','0'),
            .metadata_offset = sizeof(box_draw_group),
        },
        .metadata = {0}, // zero out
    };

    box_draw_metadata *box = &blob.metadata;
    u16 border_index = get_border_index();
    box->border_index[0] = box->border_index[1] = box->border_index[2] = box->border_index[3] = border_index;
    box->border_unk[0] = 0x27; // unk const

    s16 border_offset = 0x80;
    box->inside_center_x = border_offset;
    box->inside_center_y = border_offset;
    box->center_x = center_x;
    box->center_y = center_y;

    box->width = width;
    box->height = height;
    box->inside_width = width - (border_offset << 1);
    box->inside_height = height - (border_offset << 1);

    copy_gx_color(top_color, &box->top_color[0]);
    copy_gx_color(top_color, &box->top_color[1]);
    copy_gx_color(bottom_color, &box->bottom_color[0]);
    copy_gx_color(bottom_color, &box->bottom_color[1]);

	int inside_x = box->center_x - (box->inside_width / 2);
	int inside_y = box->center_y - (box->inside_height / 2);

    GXColor box_color = {255, 255, 255, alpha};
	draw_box(0, &blob.group, &box_color, inside_x, inside_y, box->inside_width, box->inside_height);

    return;
}

#if 0
void patch_anim_draw() {
    prep_text_mode();

    GXColor top_color = {0x6e, 0x00, 0xb3, 0xc8};
    GXColor bottom_color = {0x80, 0x00, 0x57, 0xb4};
    draw_info_box(0x1200, 0x560, 0x1230, 0xb20, 0xff, &top_color, &bottom_color);
}
#endif

// #define WITH_SPACE 1

void setup_icon_positions() {
#if defined(WITH_SPACE) && WITH_SPACE
    const int base_x = -208;
#else
    const int base_x = -196;
#endif

    for (int col = 0; col < 8; col++) {
        position_t *pos = &icons_positions[col];
        pos->scale = 1.3;
        pos->opacity = 1.0;


        f32 pos_x = base_x + (col * DRAW_OFFSET_Y);
#if defined(WITH_SPACE) && WITH_SPACE
        if (col >= 4) pos_x += 24; // card spacing
#endif

        C_MTXIdentity(pos->m);
        pos->m[0][3] = pos_x;
        pos->m[1][3] = 0.0;
        pos->m[2][3] = 1.0;
    }
}

__attribute_used__ void update_icon_positions() {
    f32 mult = 0.7; // 1.0 is more accurate
    selected_icon_mod.rot_diff_x = fast_cos(anim_step * 70) * 350 * mult;
    selected_icon_mod.rot_diff_y = fast_cos(anim_step * 35 - 15000) * 1000 * mult;
    selected_icon_mod.rot_diff_z = fast_cos(anim_step * 35) * 1000 * mult;

    selected_icon_mod.move_diff_y = fast_sin(35 * anim_step - 0x4000) * 10.0 * mult;
    selected_icon_mod.move_diff_z = fast_sin(70 * anim_step) * 5.0 * mult;

    anim_step += 0x7; // why is this the const?
}


__attribute_data__ Mtx global_gameselect_matrix;
__attribute_data__ Mtx global_gameselect_inverse;
void set_gameselect_view(Mtx matrix, Mtx inverse) {
    C_MTXCopy(matrix, global_gameselect_matrix);
    C_MTXCopy(inverse, global_gameselect_inverse);
}

void fix_gameselect_view() {
    GX_LoadPosMtxImm(global_gameselect_matrix,0);
    GX_LoadNrmMtxImm(global_gameselect_inverse,0);
    GXSetCurrentMtx(0);
}

__attribute_data__ u32 current_gameselect_state = SUBMENU_GAMESELECT_LOADER;
__attribute_used__ void custom_gameselect_menu(u8 broken_alpha_0, u8 alpha_1, u8 broken_alpha_2) {
    // color
    u8 ui_alpha = alpha_1;
    // u8 ui_alpha = alpha_2; // correct with animation
    GXColor white = {0xFF, 0xFF, 0xFF, ui_alpha};

    // text
    draw_text("cubeboot loader", 20, 20, 4, &white);

    // icons
    for (int pass = 0; pass < 2; pass++) {
        for (int line_num = 0; line_num < number_of_lines; line_num++) {
            line_backing_t *line_backing = &browser_lines[line_num];

            if (line_backing->transparency > 0 && line_backing->raw_position_y >= 0 && line_backing->raw_position_y < SCREEN_BOUND_TOTAL_Y) {
                f32 real_position_y = SCREEN_BOUND_TOP - line_backing->raw_position_y;
                // OSReport("line %d: %f\n", line_num, real_position_y);
                for (int col = 0; col < 8; col++) {
                    int slot_num = (line_num * 8) + col;

                    // bool has_texture = (slot_num < game_backing_count);
                    bool selected = (slot_num == selected_slot);

                    if (selected && pass == 0) continue; // skip selected icon on first pass
                    if (!selected && pass == 1) continue; // skip unselected icons on second pass

                    position_t *pos = &icons_positions[col];
                    f32 saved_x = pos->m[0][3];

                    // modify
                    pos->opacity = line_backing->transparency;
                    if (selected) {
                        pos->scale = 2.0;

                        apply_save_rot(selected_icon_mod.rot_diff_x, selected_icon_mod.rot_diff_y, selected_icon_mod.rot_diff_z, pos->m);

                        pos->m[0][3] = saved_x + selected_icon_mod.move_diff_y;
                        pos->m[1][3] = real_position_y - selected_icon_mod.move_diff_z;
                        pos->m[2][3] = 2.0;
                    } else {
                        pos->scale = 1.3;
                        pos->m[1][3] = real_position_y;
                    }
                    draw_save_icon(pos, slot_num, alpha_1, selected);

                    C_MTXIdentity(pos->m);
                    pos->m[0][3] = saved_x; // reset x
                    pos->m[1][3] = 0.0; // reset y
                    pos->m[2][3] = 1.0; // reset z
                }
            }
        }
    }

    // arrows
    fix_gameselect_view();
    setup_tex_draw(1, 0, 0);
    if (top_line_num > 0) {
        draw_named_tex(make_type('a','r','a','u'), menu_blob, &white, 0x800 - 80, 0); // TODO: y pos anim
    }
    if (number_of_lines > DRAW_TOTAL_ROWS && top_line_num < (number_of_lines - DRAW_TOTAL_ROWS)) {
        draw_named_tex(make_type('a','r','a','d'), menu_blob, &white, 0x800 - 80, 0); // TODO: y pos anim
    }

    // box
    GXColor top_color = {0x6e, 0x00, 0xb3, 0xc8};
    GXColor bottom_color = {0x80, 0x00, 0x57, 0xb4};
    draw_info_box(0x20f0, 0x560, 0x1230, 0x1640, ui_alpha, &top_color, &bottom_color);

    gm_file_entry_t *entry = gm_get_game_entry(selected_slot);
    if (entry != NULL && selected_slot < game_backing_count) {
        if (entry->extra.game_id[3] == 'J') switch_lang_jpn();
        else switch_lang_eng();

        // info
        draw_blob_text(make_type('t','i','t','l'), menu_blob, &white, entry->desc.fullGameName, 0x1f);
        draw_blob_text(make_type('i','n','f','o'), menu_blob, &white, entry->desc.description, 0x1f);

        switch_lang_eng();
        if (entry->type == GM_FILE_TYPE_PROGRAM || entry->type == GM_FILE_TYPE_DIRECTORY) {
            // game source
            switch_lang_eng();
            draw_blob_border(make_type('f','r','m','c'), menu_blob, &white);

            char *type_text = entry->type == GM_FILE_TYPE_DIRECTORY ? "DIR" : "DOL";
            draw_text(type_text, 20, 125, 540, &white);

            const uint8_t *default_icon = entry->type == GM_FILE_TYPE_DIRECTORY ? &dir_tex_bin[0] : &dol_tex_bin[0];
            if (entry->asset.icon.state == GM_LOAD_STATE_NONE) {
                // icon image
                setup_tex_draw(1, 0, 1);
                icon_texture.offset = (s32)((u32)default_icon - (u32)&icon_texture);
                draw_blob_tex(make_type('i','c','0','0'), menu_blob, &white, &icon_texture);
            } else if (entry->asset.icon.state == GM_LOAD_STATE_LOADED) {
                // icon image
                setup_tex_draw(1, 0, 1);
                // TODO: handle format changes for compressed icons
                icon_texture.offset = (s32)((u32)entry->asset.icon.buf->data - (u32)&icon_texture);
                draw_blob_tex(make_type('i','c','0','0'), menu_blob, &white, &icon_texture);
            }
        } else if (entry->type == GM_FILE_TYPE_GAME) {
            // game source
            switch_lang_eng();
            draw_blob_border(make_type('f','r','m','c'), menu_blob, &white);
            draw_text("ISO", 20, 125, 540, &white);

            if (entry->asset.banner.state == GM_LOAD_STATE_LOADED) {
                // banner image
                setup_tex_draw(1, 0, 1);
                banner_texture.offset = (s32)((u32)(entry->asset.banner.buf->data) - (u32)&banner_texture);
                draw_blob_tex(make_type('b','a','n','a'), menu_blob, &white, &banner_texture);
            }
        }
        switch_lang_orig();
    }

    return;
}

__attribute_used__ void original_gameselect_menu(u8 broken_alpha_0, u8 alpha_1, u8 broken_alpha_2) {
    // menu alpha
    u8 ui_alpha = alpha_1;
    GXColor white = {0xFF, 0xFF, 0xFF, ui_alpha};

    gm_file_entry_t *entry = gm_get_game_entry(selected_slot);
    if (entry == NULL) return; // protect against transition during enum
    if (entry->extra.game_id[3] == 'J') switch_lang_jpn();
    else switch_lang_eng();

    if (entry->type == GM_FILE_TYPE_GAME && entry->asset.banner.state == GM_LOAD_STATE_LOADED) {
        // game banner
        setup_tex_draw(1, 0, 1);
        banner_texture.offset = (s32)((u32)(entry->asset.banner.buf->data) - (u32)&banner_texture);
        draw_blob_tex(make_type('b','a','n','a'), game_blob_b, &white, &banner_texture);
    }

    // game info
    prep_text_mode();
    draw_blob_text(make_type('t','i','t','l'), game_blob_b, &white, entry->desc.fullGameName, 0x40);
    if (entry->type == GM_FILE_TYPE_GAME) {
        draw_blob_text(make_type('m','a','k','r'), game_blob_b, &white, entry->desc.fullCompany, 0x40);
        draw_blob_text_long(make_type('i','n','f','o'), game_blob_b, &white, entry->desc.description, 0x80);
    } else {
        draw_blob_text(make_type('m','a','k','r'), game_blob_b, &white, entry->desc.description, 0x40);
    }

    // press start anim
    draw_start_anim(ui_alpha); // TODO: fix alpha timing

    // fix camera again
    setup_gameselect_menu(0, 0, 0);

    // start string
    switch_lang_orig();
    draw_blob_fixed(game_blob_text, game_blob_a, game_blob_b, &white);

    return;
}

static bool first_transition = true;
static bool in_submenu_transition = false;
static u8 custom_menu_transition_alpha = 0xFF;
static u8 original_menu_transition_alpha = 0;
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

__attribute_used__ void mod_gameselect_draw(u8 alpha_0, u8 alpha_1, u8 alpha_2) {
    // this is for the camera
    setup_gameselect_menu(0, 0, 0);
    draw_grid(global_gameselect_matrix, alpha_1);

    // TODO: use GXColor instead of alpha byte
    u8 custom_alpha_1 = custom_menu_transition_alpha;
    u8 original_alpha_1 = original_menu_transition_alpha;

    if (alpha_1 != 0xFF) {
        custom_alpha_1 = alpha_1;
    }

    if (custom_alpha_1 != 0) custom_gameselect_menu(alpha_0, custom_alpha_1, alpha_2);
    if (original_alpha_1 != 0) original_gameselect_menu(0, original_alpha_1, 0); // fix alpha_0 + alpha_2?

    return;
}

__attribute_used__ s32 handle_gameselect_inputs() {
    update_icon_positions();
    grid_update_icon_positions();

    // TODO: this code is so annoying haha... I should add a direction var
    // TODO: only works with numbers that do not divide into 255 (switch to floats?)
    u8 transition_step = 14;
    if (rmode->viTVMode >> 2 != VI_NTSC) transition_step = 16;
    if (in_submenu_transition) {
        if (custom_menu_transition_alpha != 0 && original_menu_transition_alpha != 0) {
            if ((255 - custom_menu_transition_alpha) < transition_step || (255 - original_menu_transition_alpha) < transition_step) {
                in_submenu_transition = false;

                custom_menu_transition_alpha = custom_menu_transition_alpha < 127 ? 0 : 255;
                original_menu_transition_alpha = original_menu_transition_alpha < 127 ? 0 : 255;
            }
        }
    }

    if (in_submenu_transition) {
        switch(current_gameselect_state) {
            case SUBMENU_GAMESELECT_LOADER:
                custom_menu_transition_alpha += transition_step;
                original_menu_transition_alpha -= transition_step;
                break;
            case SUBMENU_GAMESELECT_START:
                custom_menu_transition_alpha -= transition_step;
                original_menu_transition_alpha += transition_step;
                break;
            default:
        }
    }

    if (pad_status->buttons_down & PAD_TRIGGER_Z) {
        // add test code here
        load_stub(); // exit to loader again
        u32 *sig = (u32*)0x80001804;
        if ((*sig++ == 0x53545542 || *sig++ == 0x53545542) && *sig == 0x48415858) {
            static void (*reload)(void) = (void(*)(void))0x80001800;
            run(reload);
        }
    }

    if (pad_status->buttons_down & PAD_BUTTON_B) {
        if (current_gameselect_state == SUBMENU_GAMESELECT_START && !in_submenu_transition) {
            in_submenu_transition = true;
            current_gameselect_state = SUBMENU_GAMESELECT_LOADER;
            Jac_PlaySe(SOUND_SUBMENU_EXIT);
        } else if (!in_submenu_transition) {
            // TODO: check current path depth
            if (strcmp(game_enum_path, "/") != 0) {
                gm_deinit_thread();
                Jac_PlaySe(SOUND_MENU_EXIT);
                gm_start_thread("..");
            } else {
                anim_step = 0; // anim reset
                *banner_pointer = (u32)&default_opening_bin[0]; // banner reset
                Jac_PlaySe(SOUND_MENU_EXIT);
                return MENU_GAMESELECT_ID;
            }
        }
    }

    if (pad_status->buttons_down & PAD_BUTTON_A && current_gameselect_state == SUBMENU_GAMESELECT_LOADER) {
        if (selected_slot < game_backing_count && !in_submenu_transition) {
            gm_file_entry_t *entry = gm_get_game_entry(selected_slot);
            if (entry->type == GM_FILE_TYPE_DIRECTORY) {
                OSReport("Selected DIR slot: %d (%p)\n", selected_slot, entry);

                gm_deinit_thread();
                Jac_PlaySe(SOUND_SUBMENU_ENTER);

                char path[128];
                sprintf(path, "%s/", entry->path);
                gm_start_thread(path);
            } else {
                in_submenu_transition = true;
                current_gameselect_state = SUBMENU_GAMESELECT_START;

                Jac_PlaySe(SOUND_SUBMENU_ENTER);
                setup_gameselect_anim();
                setup_cube_anim();

                if (entry->type == GM_FILE_TYPE_GAME) {
                    mcp_set_gameid(entry);
                }

                // OSReport("Selected slot: %d (%p)\n", selected_slot, asset);
            }
        }
    }

    // if (pad_status->buttons_down & PAD_BUTTON_START && current_gameselect_state == SUBMENU_GAMESELECT_LOADER) {
    //     ...
    // }

    if (pad_status->buttons_down & PAD_BUTTON_START && current_gameselect_state == SUBMENU_GAMESELECT_START) {
        Jac_StopSoundAll();
        Jac_PlaySe(SOUND_MENU_FINAL);
        gm_file_entry_t *entry = gm_get_game_entry(selected_slot);
        memcpy(&boot_entry, entry, sizeof(gm_file_entry_t));
        if (boot_entry.second != NULL) {
            memcpy(&second_boot_entry, boot_entry.second, sizeof(gm_file_entry_t));
            boot_entry.second = &second_boot_entry;
        }
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
            if (number_of_lines - top_line_num == 4 && (selected_slot + 8) > (number_of_lines * 8 - 1)) {
                // OSReport("SKIP MOVE DOWN: top_line_num = %d\n", top_line_num);
                Jac_PlaySe(SOUND_CARD_ERROR);
            } else {
                Jac_PlaySe(SOUND_CARD_MOVE);
                line_backing_t *line_backing = &browser_lines[selected_slot / 8];
                if (get_position_after(line_backing) >= DRAW_BOUND_BOTTOM - DRAW_OFFSET_Y - 10) {
                    if (gm_can_move() && grid_dispatch_navigate_down() == GRID_MOVE_SUCCESS) {
                        gm_line_changed(1);
                        selected_slot += 8;
                        top_line_num++;
                    }
                } else {
                    selected_slot += 8;
                }
            }
        }

        if (pad_status->analog_down & ANALOG_UP) {
            if (top_line_num == 0 && (selected_slot - 8) < 0) {
                // OSReport("SKIP MOVE UP: top_line_num = %d\n", top_line_num);
                Jac_PlaySe(SOUND_CARD_ERROR);
            } else {
                Jac_PlaySe(SOUND_CARD_MOVE);
                line_backing_t *line_backing = &browser_lines[selected_slot / 8];
                if (top_line_num != 0 && get_position_after(line_backing) <= DRAW_BOUND_TOP + DRAW_OFFSET_Y - 10) {
                    if (gm_can_move() && grid_dispatch_navigate_up() == GRID_MOVE_SUCCESS) {
                        gm_line_changed(-1);
                        selected_slot -= 8;
                        top_line_num--;
                    }
                } else {
                    selected_slot -= 8;
                }
            }
            
            // OSReport("top_line_num = %d\n", top_line_num);
            // OSReport("selected_slot = %d\n", selected_slot);
        }
    }

    return MENU_GAMESELECT_TRANSITION_ID;
}

__attribute_data__ u8 show_watermark = 1;
void alpha_watermark(void) {
    if (!show_watermark && !is_running_dolphin) return;
    prep_text_mode();

    GXColor yellow_alpha = {0xFF, 0xFF, 0x00, 0x80};
    draw_text("BETA TEST", 24, 330, 0, &yellow_alpha);
    draw_text("cubeboot rc" CONFIG_BETA_RC, 22, 330, 28, &yellow_alpha);
}
