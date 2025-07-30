#include "draw.h"

#include "attr.h"

__attribute_reloc__ void (*prep_text_mode)();
__attribute_reloc__ void (*gx_draw_text)(u16 index, text_group* text, text_draw_group* text_draw, GXColor* color);

__attribute_reloc__ void (*draw_grid)(Mtx position, u8 alpha);
__attribute_reloc__ void (*draw_box)(u32 index, const box_draw_group* header, const GXColor* texa, int inside_x, int inside_y, int inside_width, int inside_height);
__attribute_reloc__ void (*draw_blob_fixed)(const element_alpha_state_t *element_alpha, const blob_header_t *sth0_blob, const blob_header_t *glh0_blob, const GXColor *color);
__attribute_reloc__ void (*draw_blob_text)(u32 type, const blob_header_t *blob, const GXColor *color, const char *str, s32 len);
__attribute_reloc__ void (*draw_blob_text_long)(u32 type, const blob_header_t *blob, const GXColor *color, const char *str, s32 len);
__attribute_reloc__ void (*draw_blob_border)(u32 type, const blob_header_t *blob, const GXColor *color);
__attribute_reloc__ void (*draw_blob_tex)(u32 type, const blob_header_t *blob, const GXColor *color, const tex_data *dat);
__attribute_reloc__ void (*setup_tex_draw)(bool unk0, bool unk1, bool is_srgb);
__attribute_reloc__ void (*draw_named_tex)(u32 type, const blob_header_t *blob, const GXColor *color, s16 x, s16 y);

// for model gx
__attribute_reloc__ void (*model_init)(model* m, int process);
__attribute_reloc__ void (*draw_model)(model* m);
__attribute_reloc__ void (*draw_partial)(model* m, model_part* part);
__attribute_reloc__ void (*change_model)(model* m);

// for camera gx
__attribute_reloc__ void (*set_obj_pos)(model* m, MtxP matrix, guVector vector);
__attribute_reloc__ void (*set_obj_cam)(model* m, MtxP matrix);
__attribute_reloc__ MtxP (*get_camera_mtx)();

// helpers
__attribute_reloc__ f32 (*fast_sin)(s16 deg);
__attribute_reloc__ f32 (*fast_cos)(s16 deg);
