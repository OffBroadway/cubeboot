#pragma once

#include <gctypes.h>

typedef struct {
    u32 magic;
    u32 textures_offset;
    u32 text_offset;
    u32 borders_offset;
    u16 texture_count;
    u16 text_count;
    u16 border_count;
    u16 unk0;
} blob_header_t;

typedef struct {
    u32 magic;
    u16 x_position;
    u16 y_position;
    u16 width;
    u16 height;
    u8 unk0;
    u8 texture_index;
    u8 unk1;
    u8 unk2;
} blob_texture_element_t;
