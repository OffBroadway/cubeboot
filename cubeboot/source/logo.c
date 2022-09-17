#include <gctypes.h>
#include <ogc/gx.h>
#include <ogc/cache.h>

#include <stdio.h>
#include <stdint.h>
#include <malloc.h>

#include "GameCube_ttf.h"
#include "logo.h"

#include "halt.h"

static u8 image_buffer[0x4000];

// credit to https://github.com/ArminTamzarian/metaphrasis
uint32_t* Metaphrasis_convertBufferToI8(uint32_t* rgbaBuffer, uint16_t bufferWidth, uint16_t bufferHeight) {
	uint32_t bufferSize = bufferWidth * bufferHeight;
    if (bufferSize > sizeof(image_buffer)) {
        prog_halt("Could not allocate logo buffer\n");
    }
	uint32_t *dataBufferI8 = (uint32_t*)image_buffer; // (uint32_t *)memalign(32, bufferSize);
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

// TODO: mono alpha?
void texture_alloc_data(texture_t* texture) {
    if (texture->data) {
        iprintf("bebopX\n");
        free(texture->data);
    }

    if (texture->alpha) {
        iprintf("bebopY %dx%d\n", texture->width, texture->height);
        texture->data = malloc(texture->width*texture->height*4);
        iprintf("bebop8 %p[%x]\n", (void*)texture->data, texture->width*texture->height*4);
    }
    else if (texture->colortype == COLOR_MONO) {
        iprintf("bebopZ\n");
        texture->data = malloc(texture->width*texture->height);
    }
    else {
        iprintf("bebopZIP\n");
        texture->data = malloc(texture->width*texture->height*3);
    }
}

void texture_init(texture_t *texture, int width, int height, int alpha) {
    texture->data = NULL;
    texture->width = width;
    texture->height = height;
    texture->alpha = alpha;
    texture->colortype = COLOR_BGR;
    texture_alloc_data(texture);
}

void texture_destroy(texture_t *texture) {
    if (texture->data) {
        free(texture->data);
    }
    free(texture);
}

void text_render_init(text_render_t* render, const u8* fontbuffer, int fontbuffer_size) {
    memset(render, 0, sizeof(text_render_t));
    FT_Init_FreeType(&render->library);
    FT_New_Memory_Face(render->library, fontbuffer, fontbuffer_size, 0, &render->face);
}

void text_render_destroy(text_render_t* render)
{
    FT_Done_Face(render->face);
    // FT_Done_FreeType(render->library);
}

texture_t* render_text(text_render_t* render, char* str, int size, int font_size, color_t color) {
    int width = 0;
    {
        FT_Set_Pixel_Sizes(render->face, 0, size - 10 + font_size);
    
        iprintf("bebopD, %s\n", str);

        
        for(int i = 0; i < strlen(str); i++) {
            iprintf("bebopG, %c\n", str[i]);
            u32 glyphindex = FT_Get_Char_Index(render->face, str[i]); 
            FT_Load_Glyph(render->face, glyphindex, FT_LOAD_DEFAULT);
            width += (render->face->glyph->advance.x >> 6) - 4;
        }

        iprintf("bebopA\n");
    }

    int left_margin = 0;
    if (width > GAMECUBE_LOGO_WIDTH) {
        printf("Texture error: text too wide!\n");
        width = GAMECUBE_LOGO_WIDTH;
    }

    iprintf("bebopB\n");
    
    if (width != GAMECUBE_LOGO_WIDTH) {
        left_margin = (GAMECUBE_LOGO_WIDTH - width) >> 1;
        left_margin += 2;
    }
    width = GAMECUBE_LOGO_WIDTH;

    iprintf("bebopV, %d\n", font_size);

    texture_t* tex = malloc(sizeof(texture_t));
    iprintf("bebopW %p, %dx%ld\n", (void*)tex, width, (render->face->size->metrics.height >> 6) - font_size + (font_size >> 2));
    texture_init(tex, width, (render->face->size->metrics.height >> 6) - font_size + (font_size >> 2), 1);
    printf("Texture init at %dx%d\n", tex->width, tex->height);
    
    int xoffset = left_margin;
    for(int i = 0; i < strlen(str); i++) {
        u32 glyphindex = FT_Get_Char_Index(render->face, str[i]); 
        FT_Load_Glyph(render->face, glyphindex, FT_LOAD_DEFAULT);
        FT_Render_Glyph(render->face->glyph, FT_RENDER_MODE_NORMAL);
        FT_GlyphSlot glyph = render->face->glyph;

        for(int gy = 0; gy < glyph->bitmap.rows; gy++) {
            for(int gx = 0; gx < glyph->bitmap.width; gx++) {
                int dst = ((gy + (tex->height-glyph->bitmap.rows) - 8) * tex->width + gx + xoffset) * 4;
                int src = (gy * glyph->bitmap.width + gx);
                tex->data[dst + 0] = color.b; //glyph->bitmap.buffer[src];
                tex->data[dst + 1] = color.r; //glyph->bitmap.buffer[src];
                tex->data[dst + 2] = color.g; //glyph->bitmap.buffer[src];
                tex->data[dst + 3] = glyph->bitmap.buffer[src];
            }
        }
        xoffset += (glyph->advance.x >> 6) - 4;
    }

    return tex;
}

u8 *texture_render_logo(char *logo_text, int font_size, int *ptr_img_size) {
    int width = GAMECUBE_LOGO_WIDTH;
    int height = GAMECUBE_LOGO_HEIGHT;
    // int format = GX_TF_I8;

    text_render_t render;
    text_render_init(&render, GameCube_ttf, GameCube_ttf_size);

    iprintf("bopA?, %p\n", (void*)&render);

    color_t white = {0xFF, 0xFF, 0xFF};
    texture_t *text = render_text(&render, logo_text, height, font_size, white);
    
    iprintf("bopB?\n");

    // clamp height
    text->height = height;
    (void)width;

    u32 *image = Metaphrasis_convertBufferToI8((u32*)text->data, text->width, text->height);
    u32 image_size = text->width * text->height;

    iprintf("bopC\n");

    texture_destroy(text);
    // text_render_destroy(&render);

    iprintf("bopD?\n");

    // u32 *image = text->data;
    // u32 image_size = 0;

    if (ptr_img_size != NULL) {
        *ptr_img_size = image_size;
    }
    return (u8*)image;
}
