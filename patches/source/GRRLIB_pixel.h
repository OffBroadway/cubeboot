/*------------------------------------------------------------------------------
Copyright (c) 2009-2021 The GRRLIB Team

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in
all copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
THE SOFTWARE.
------------------------------------------------------------------------------*/

/*
 * @file GRRLIB_pixel.h
 * Inline functions for manipulating pixels in textures.
 */

#include <gctypes.h>
#define INLINE inline

/**
 * Return the color value of a pixel from a GRRLIB_texImg.
 * @param x Specifies the x-coordinate of the pixel in the texture.
 * @param y Specifies the y-coordinate of the pixel in the texture.
 * @param tex The texture to get the color from.
 * @return The color of a pixel in RGBA format.
 */
INLINE u32 GRRLIB_GetPixelFromtexImg(const int x, const int y, void *data, u32 width) {
    u32  offs;
    u32  ar;
    u8*  bp = (u8*)data;

    offs = (((y&(~3))<<2)*width) + ((x&(~3))<<4) + ((((y&3)<<2) + (x&3)) <<1);

    ar =                 (u32)(*((u16*)(bp+offs   )));
    return (ar<<24) | ( ((u32)(*((u16*)(bp+offs+32)))) <<8) | (ar>>8);  // Wii is big-endian
}

/**
 * Set the color value of a pixel to a GRRLIB_texImg.
 * @see GRRLIB_FlushTex
 * @param x Specifies the x-coordinate of the pixel in the texture.
 * @param y Specifies the y-coordinate of the pixel in the texture.
 * @param tex The texture to set the color to.
 * @param color The color of the pixel in RGBA format.
 */
INLINE void GRRLIB_SetPixelTotexImg(const int x, const int y, void *data, u32 width, const u32 color) {
    u32  offs;
    u8*  bp = (u8*)data;

    offs = (((y&(~3))<<2)*width) + ((x&(~3))<<4) + ((((y&3)<<2) + (x&3)) <<1);

    *((u16*)(bp+offs   )) = (u16)((color <<8) | (color >>24));
    *((u16*)(bp+offs+32)) = (u16) (color >>8);
}

// from https://sites.google.com/site/puebloelcoronil/entradas

#define H( c ) (((c) >> 24) &0xFF)
#define S( c ) (((c) >> 16) &0xFF)
#define V( c ) (((c) >> 8) &0xFF)
#define L( c ) (((c) >> 8) &0xFF)
 
#define HSLA(h,s,l,a) ( (u32)( ( ((u32)(h))        <<24) |  \
                               ((((u32)(s)) &0xFF) <<16) |  \
                               ((((u32)(l)) &0xFF) << 8) |  \
                               ( ((u32)(a)) &0xFF      ) ) )

#define R( c ) (((c) >> 24) &0xFF)
#define G( c ) (((c) >> 16) &0xFF)
#define B( c ) (((c) >> 8) &0xFF)
#define A( c ) ((c) &0xFF)
 
#define RGBA(r,g,b,a) ( (u32)( ( ((u32)(r))        <<24) |  \
                               ((((u32)(g)) &0xFF) <<16) |  \
                               ((((u32)(b)) &0xFF) << 8) |  \
                               ( ((u32)(a)) &0xFF      ) ) )

float max(float a, float b);
float min(float a, float b);

float Hue_2_RGB(float v1, float v2, float vH);

u32 GRRLIB_RGBToHSV(u32 cRGB);
u32 GRRLIB_HSVToRGB(u32 cHSV);
u32 GRRLIB_RGBToHSL(u32 cRGB);
u32 GRRLIB_HSLToRGB(u32 cHSL);
