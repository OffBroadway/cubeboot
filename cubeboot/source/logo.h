#include <gctypes.h>

#include <ft2build.h>
#include FT_FREETYPE_H
#include FT_CACHE_H
#include FT_BITMAP_H
#include FT_GLYPH_H

#define GAMECUBE_LOGO_WIDTH 352
#define GAMECUBE_LOGO_HEIGHT 40

// credit to https://github.com/jakeprobst/cb3ds
typedef enum color_type_t {
    COLOR_RGB,
    COLOR_BGR,
    COLOR_MONO
} color_type_t;

typedef struct texture_t {
    int width;
    int height;
    int alpha;
    u8* data;
    color_type_t colortype;
} texture_t;

typedef struct color_t {
    u8 r;
    u8 g;
    u8 b;
} color_t;

typedef struct text_render_t {
    FT_Library library;
    FT_Face face;
} text_render_t;

void texture_alloc_data(texture_t* texture);
void texture_init(texture_t *texture, int width, int height, int alpha);
void texture_destroy(texture_t *texture);
void text_render_init(text_render_t* render, const u8* fontbuffer, int fontbuffer_size);
void text_render_destroy(text_render_t* render);
u8 *texture_render_logo(char *logo_text, int font_size, int *ptr_img_size);
