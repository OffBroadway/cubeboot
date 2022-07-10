#include <ft2build.h>
#include FT_FREETYPE_H
#include FT_CACHE_H
#include FT_BITMAP_H
#include FT_GLYPH_H

#include <gctypes.h>
#include <ogc/gx.h>
#include <ogc/cache.h>

#include <stdio.h>
#include <stdint.h>
#include <malloc.h>

#include "GameCube_ttf.h"

// credit to https://github.com/ArminTamzarian/metaphrasis
uint32_t* Metaphrasis_convertBufferToI8(uint32_t* rgbaBuffer, uint16_t bufferWidth, uint16_t bufferHeight) {
	uint32_t bufferSize = bufferWidth * bufferHeight;
	uint32_t* dataBufferI8 = (uint32_t *)memalign(32, bufferSize);
	memset(dataBufferI8, 0x00, bufferSize);

	uint32_t *src = (uint32_t *)rgbaBuffer;
	uint8_t *dst = (uint8_t *)dataBufferI8;

	for(uint16_t y = 0; y < bufferHeight; y += 4) {
		for(uint16_t x = 0; x < bufferWidth; x += 8) {
			for(uint16_t rows = 0; rows < 4; rows++) {
				*dst++ = src[((y + rows) * bufferWidth) + (x + 0)] & 0xff;
				*dst++ = src[((y + rows) * bufferWidth) + (x + 1)] & 0xff;
				*dst++ = src[((y + rows) * bufferWidth) + (x + 2)] & 0xff;
				*dst++ = src[((y + rows) * bufferWidth) + (x + 3)] & 0xff;
				*dst++ = src[((y + rows) * bufferWidth) + (x + 4)] & 0xff;
				*dst++ = src[((y + rows) * bufferWidth) + (x + 5)] & 0xff;
				*dst++ = src[((y + rows) * bufferWidth) + (x + 6)] & 0xff;
				*dst++ = src[((y + rows) * bufferWidth) + (x + 7)] & 0xff;
			}
		}
	}
	DCFlushRange(dataBufferI8, bufferSize);

	return dataBufferI8;
}

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

color_t color(u8 r, u8 g, u8 b);

typedef struct text_render_t {
    FT_Library library;
    FT_Face face;
} text_render_t;

// TODO: mono alpha?
void texture_alloc_data(texture_t* texture)
{
    if (texture->data)
        free(texture->data);
    if (texture->alpha) {
        texture->data = malloc(texture->width*texture->height*4);
    }
    else if (texture->colortype == COLOR_MONO) {
        texture->data = malloc(texture->width*texture->height);
    }
    else {
        texture->data = malloc(texture->width*texture->height*3);
    }
}

void texture_init(texture_t *texture, int width, int height, int alpha)
{
    texture->data = NULL;
    texture->width = width;
    texture->height = height;
    texture->alpha = alpha;
    texture->colortype = COLOR_BGR;
    texture_alloc_data(texture);
}

void texture_init2(texture_t *texture, int width, int height, int alpha, color_type_t type)
{
    texture->data = NULL;
    texture->width = width;
    texture->height = height;
    texture->alpha = alpha;
    texture->colortype = type;
    texture_alloc_data(texture);
}

void texture_destroy(texture_t *texture)
{
    if (texture->data) {
        free(texture->data);
    }
}

void text_render_init(text_render_t* textrender, const u8* fontbuffer, int fontbuffer_size)
{
    FT_Init_FreeType(&textrender->library);
    FT_New_Memory_Face(textrender->library, fontbuffer, fontbuffer_size, 0, &textrender->face);
}

void text_render_destroy(text_render_t* textrender)
{
    FT_Done_Face(textrender->face);
    FT_Done_FreeType(textrender->library);
}

texture_t* render_text(text_render_t* textrender, char* str, int size, color_t color)
{
    FT_Set_Pixel_Sizes(textrender->face, 0, size);
    
    int width = 0;
    for(int i = 0; i < strlen(str); i++) {
        u32 glyphindex = FT_Get_Char_Index(textrender->face, str[i]); 
        FT_Load_Glyph(textrender->face, glyphindex, FT_LOAD_DEFAULT);
        width += textrender->face->glyph->advance.x >> 6;
    }

    int left_margin = 0;
    if (width > 352) {
        printf("Texture error: text too wide!\n");
    } else if (width != 352) {
        left_margin = (352 - width) >> 1;
        left_margin -= 11;
    }
    width = 352;

    texture_t* tex = malloc(sizeof(texture_t));
    texture_init(tex, width, textrender->face->size->metrics.height >> 6, 1);
    printf("Texture init at %dx%d\n", width, textrender->face->size->metrics.height >> 6);
    
    int xoffset = left_margin;
    for(int i = 0; i < strlen(str); i++) {
        u32 glyphindex = FT_Get_Char_Index(textrender->face, str[i]); 
        FT_Load_Glyph(textrender->face, glyphindex, FT_LOAD_DEFAULT);
        FT_Render_Glyph(textrender->face->glyph, FT_RENDER_MODE_NORMAL);
        FT_GlyphSlot glyph = textrender->face->glyph;

        for(int gy = 0; gy < glyph->bitmap.rows; gy++) {
            for(int gx = 0; gx < glyph->bitmap.width; gx++) {
                int dst = ((gy + (tex->height-glyph->bitmap.rows)) * tex->width + gx + xoffset) * 4;
                int src = (gy * glyph->bitmap.width + gx);
                tex->data[dst + 0] = color.b; //glyph->bitmap.buffer[src];
                tex->data[dst + 1] = color.r; //glyph->bitmap.buffer[src];
                tex->data[dst + 2] = color.g; //glyph->bitmap.buffer[src];
                tex->data[dst + 3] = glyph->bitmap.buffer[src];
            }
        }
        xoffset += glyph->advance.x >> 6;
    }

    return tex;
}

// now render

extern void *gc_text_tex_data_ptr;
void render_logo() {
    int width = 352;
    int height = 40 - 8;
    // int format = GX_TF_I8;

    text_render_t textrender;
    text_render_init(&textrender, GameCube_ttf, GameCube_ttf_size);

    color_t white = {0xFF, 0xFF, 0xFF};
    texture_t *text = render_text(&textrender, "CUBEGAME", height, white);

    u32 *image = Metaphrasis_convertBufferToI8((u32*)text->data, text->width, text->height);
    u32 image_size = text->width * text->height;

    printf("Copying to %p size = %04x\n", gc_text_tex_data_ptr, image_size);
    memcpy(gc_text_tex_data_ptr, image, image_size);
}
