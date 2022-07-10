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

float max(float a, float b){
	if (a > b) return a;
	else return b;
}
 
float min(float a, float b){
	if (a < b) return a;
	else return b;
}
 
u32 GRRLIB_RGBToHSV(u32 cRGB){
	float h = 0,s = 0,v = 0;
 
	float r = R(cRGB/0xFF);
	float g = G(cRGB/0xFF);
	float b = B(cRGB/0xFF);
	float maximum = max(r,max(g,b));
	float minimum = min(r,min(g,b));
	float delta = maximum-minimum;
 
	if (s==0) h = -1;
	else if (r==maximum) h = (g-b)/delta;
	else if (g==maximum) h = 2+(b-r)/delta;
	else h = 4+(r-g)/delta;
	h = h * 60;
	if (h<0) h=h/360;
 
	if (maximum != 0) s = delta/maximum;
	else s = 0;
 
	v = maximum;
	return RGBA(h*0xFF,s*0xFF,v*0xFF,A(cRGB));
}
 
u32 GRRLIB_HSVToRGB(u32 cHSV){
	float r,g,b;
 
	float h = H(cHSV/0xFF);
	float s = S(cHSV/0xFF);
	float v = V(cHSV/0xFF);
 
	if (s == 0) {                      //HSV from 0 to 1
		r = v;
		g = v;
		b = v;
	} else {
		int var_h = h * 6;
		if (var_h == 6) var_h = 0;		//H must be < 1
		int var_i = var_h;		//Or ... var_i = floor( var_h )
		float var_1 = v*(1-s);
		float var_2 = v*(1-s*(var_h-var_i));
		float var_3 = v*(1-s*(1-(var_h-var_i)));
		if (var_i == 0) { r = v; g = var_3; b = var_1;}
		else if (var_i == 1) {r = var_2; g = v; b = var_1;}
		else if (var_i == 2) {r = var_1; g = v; b = var_3;}
		else if (var_i == 3) {r = var_1; g = var_2; b = v;}
		else if (var_i == 4) {r = var_3; g = var_1; b = v;}
		else { r = v; g = var_1; b = var_2;}
	}
	return RGBA(r*0xFF,g*0xFF,b*0xFF,A(cHSV));
}
 
u32 GRRLIB_RGBToHSL(u32 cRGB) {
	float r= (float) R(cRGB)/0xFF;    //RGB from 0 to 255
	float g= (float) G(cRGB)/0xFF;
	float b= (float) B(cRGB)/0xFF;
 
	float maximum = max(r,max(g,b));
	float minimum = min(r,min(g,b));
	float delta = maximum-minimum;
 
	float h=0,s=0;
	float l = (maximum+minimum)/2;
 
	if (delta == 0){	//This is a gray, no chroma...
		h = 0;			//HSL results from 0 to 
		s = 0;
	} else {			//Chromatic data...
		if (l < 0.5) 
			s = delta/(maximum + minimum);
		else
			s = delta/(2.0-maximum-minimum);
 
		float del_R=(((maximum-r)/6.0)+(delta/2.0))/delta;
		float del_G=(((maximum-g)/6.0)+(delta/2.0))/delta;
		float del_B=(((maximum-b)/6.0)+(delta/2.0))/delta;
		if(r == maximum) h = del_B - del_G;
		else if (g == maximum) h = (1.0/3.0)+del_R-del_B;
		else if (b == maximum) h = (2.0/3.0)+del_G-del_R;
 
		if ( h < 0.0 ) h += 1.0;
		if ( h > 1.0 ) h -= 1.0;
	}
	return HSLA(h*0xFF,s*0xFF,l*0xFF,A(cRGB));
}
 
float Hue_2_RGB(float v1, float v2, float vH){             //Function Hue_2_RGB
   if ( vH < 0 ) vH += 1.0;
   if ( vH > 1 ) vH -= 1.0;
   if ( ( 6.0 * vH ) < 1 ) return ( v1 + ( v2 - v1 ) * 6.0 * vH );
   if ( ( 2.0 * vH ) < 1 ) return ( v2 );
   if ( ( 3.0 * vH ) < 2 ) return ( v1 + ( v2 - v1 ) * ( ( 2.0 / 3.0 ) - vH ) * 6.0 );
   return ( v1 );
}
 
u32 GRRLIB_HSLToRGB(u32 cHSL){
	float h= (float) H(cHSL)/0xFF;    //RGB from 0 to 255
	float s= (float) S(cHSL)/0xFF;
	float l= (float) L(cHSL)/0xFF;
	float var_1 = 0, var_2 = 0;
	float r,g,b;
 
	if (s == 0 ){                       //HSL from 0 to 1
		r = l;
		g = l;
		b = l;
	} else {
		if (l < 0.5) var_2 = l*(1.0+s);
		else var_2 = (l+s)-(s*l);
		var_1 = 2.0*l-var_2;
 
		r = Hue_2_RGB(var_1, var_2, h+(1.0/3.0)); 
		g = Hue_2_RGB(var_1, var_2, h);
		b = Hue_2_RGB(var_1, var_2, h-(1.0/3.0));
	}	
	return RGBA(r*0xFF,g*0xFF,b*0xFF,A(cHSL));
}
