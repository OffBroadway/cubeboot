#include "custom_ui_blob.h"

#include <stddef.h>

#define NUM_TEXTURE_ELEMENTS 4

typedef struct {
    blob_header_t header;
    blob_texture_element_t textures[NUM_TEXTURE_ELEMENTS];
} custom_ui_blob_t;

static const custom_ui_blob_t custom_ui_blob_impl = (custom_ui_blob_t){
    .header = (blob_header_t){
        .magic = make_type('C','B','U','I'), // Cubeboot UI
        .textures_offset = offsetof(custom_ui_blob_t, textures),
        .text_offset = 0,
        .borders_offset = 0,
        .texture_count = NUM_TEXTURE_ELEMENTS,
        .text_count = 0,
        .border_count = 0,
        .unk0 = 0

    },
    .textures = {
        // L button
        (blob_texture_element_t){
            .magic = L_BUTTON_BLOB_TYPE,
            .x_position = 0x240, // Total width seems to be about 0x2500.
            .y_position = 0x240, // Buttons icons use 0x19C0. Total height seems to be about 0x1C00 - so this should be approximately the same distance from the top of the screen
            .width = 0x140,
            .height = 0x130,
            .unk0 = 0,
            .texture_index = 0, // Should be unused when using `draw_blob_tex()`
            .unk1 = 0xF,
            .unk2 = 0
        },

        // R button
        (blob_texture_element_t){
            .magic = R_BUTTON_BLOB_TYPE,
            .x_position = 0x2500 - 0x240, // Total width seems to be about 0x2500, so this should be approximately the same distance from the right of the screen
            .y_position = 0x240,
            .width = 0x140,
            .height = 0x130,
            .unk0 = 0,
            .texture_index = 0, // Should be unused when using `draw_blob_tex()`
            .unk1 = 0xF,
            .unk2 = 0
        },

        // Disc icon
        (blob_texture_element_t){
            .magic = DISC_BLOB_TYPE,
            .x_position = 0x240 + (0x140 / 2) + (0x280 / 2) + 0x30, // ~3 pixels to the right of the L button
            .y_position = 0x240,
            .width = 0x280,
            .height = 0x280,
            .unk0 = 0,
            .texture_index = 0, // Should be unused when using `draw_blob_tex()`
            .unk1 = 0xF,
            .unk2 = 0
        },

        // FlippyDrive icon
        (blob_texture_element_t){
            .magic = FLIPPYDRIVE_BLOB_TYPE,
            .x_position = 0x2500 - 0x240 - (0x140 / 2) - (0x400 / 2) - 0x30, // ~3 pixels to the left of the R button
            .y_position = 0x240,
            .width = 0x400,
            .height = 0x200,
            .unk0 = 0,
            .texture_index = 0, // Should be unused when using `draw_blob_tex()`
            .unk1 = 0xF,
            .unk2 = 0
        }
    }
};

const blob_header_t *const custom_ui_blob = &custom_ui_blob_impl.header;
