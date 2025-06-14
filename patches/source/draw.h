#pragma once

#include "blob.h"
#include "structs.h"

#include <gctypes.h>
#include <ogc/gx.h>

extern void (*prep_text_mode)();
extern void (*gx_draw_text)(u16 index, text_group* text, text_draw_group* text_draw, GXColor* color);

extern void (*draw_grid)(Mtx position, u8 alpha);
extern void (*draw_box)(u32 index, const box_draw_group* header, const GXColor* texa, int inside_x, int inside_y, int inside_width, int inside_height);
extern void (*draw_blob_fixed)(const blob_header_t *blob_ptr, const void *blob_a, const void *blob_b, const GXColor *color);
extern void (*draw_blob_text)(u32 type, const blob_header_t *blob, const GXColor *color, const char *str, s32 len);
extern void (*draw_blob_text_long)(u32 type, const blob_header_t *blob, const GXColor *color, const char *str, s32 len);
extern void (*draw_blob_border)(u32 type, const blob_header_t *blob, const GXColor *color);
extern void (*draw_blob_tex)(u32 type, const blob_header_t *blob, const GXColor *color, const tex_data *dat);
extern void (*setup_tex_draw)(s32 unk0, s32 unk1, s32 unk2);
extern void (*draw_named_tex)(u32 type, const blob_header_t *blob, const GXColor *color, s16 x, s16 y);

// for model gx
extern void (*model_init)(model* m, int process);
extern void (*draw_model)(model* m);
extern void (*draw_partial)(model* m, model_part* part);
extern void (*change_model)(model* m);

// for camera gx
extern void (*set_obj_pos)(model* m, MtxP matrix, guVector vector);
extern void (*set_obj_cam)(model* m, MtxP matrix);
extern MtxP (*get_camera_mtx)();

// helpers
extern f32 (*fast_sin)(s16 deg);
extern f32 (*fast_cos)(s16 deg);
