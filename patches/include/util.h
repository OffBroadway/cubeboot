#include <stdint.h>
#include <ogc/gx.h>

#include "GRRLIB_pixel.h"
#include "os.h"

typedef union rgb_color {
    u32 color;
    struct {
        u8 r, g, b, a;
    } parts;
} rgb_color;

typedef struct hsv_color {
    u32 color;
    struct {
        u8 h, s, v, a;
    } parts;
} hsv_color;

inline void copy_color10(rgb_color src, GXColorS10* dst) {
    dst->r = src.parts.r;
    dst->g = src.parts.g;
    dst->b = src.parts.b;
    dst->a = src.parts.a;
}

inline void copy_color(rgb_color src, GXColor* dst) {
    dst->r = src.parts.r;
    dst->g = src.parts.g;
    dst->b = src.parts.b;
    dst->a = src.parts.a;
}

inline void dump_color(GXColorS10 *input) {
    GXColorS10 cs10_temp;
    rgb_color rgb_temp;
    u32 hsl_temp;

    if (input == NULL) {
        OSReport("COLOR: NULL\n");
        return;
    }

    cs10_temp = *input;
    rgb_temp.parts.r = cs10_temp.r;
    rgb_temp.parts.g = cs10_temp.g;
    rgb_temp.parts.b = cs10_temp.b;
    rgb_temp.parts.a = cs10_temp.a;
    hsl_temp = GRRLIB_RGBToHSL(rgb_temp.color);
    OSReport("COLOR: %02x%02x%02x = (%d, %d, %d)\n", cs10_temp.r, cs10_temp.g, cs10_temp.b, H(hsl_temp), S(hsl_temp), L(hsl_temp));
}

inline void override_texture(tex_data *tex) {
    u16 wd = tex->width;
    u16 ht = tex->height;
    void *img_ptr = (void*)((u8*)tex + tex->offset);
    OSReport("TEX: %dx%d[%d] @ %p\n", wd, ht, tex->format, img_ptr);
    for (int i = 0; i < wd*ht; i++) // only works on I8 format. Change to u32 for RGBA8
        ((u8*)img_ptr)[i] = 0xFF;
}

inline double u64toDouble(u64 val) {
    //this is necessary to make gcc not try to use soft float.
    u32 hi = val >> 32LL;
    u32 lo = val & 0xFFFFFFFFLL;
    return (double)lo + (double)(hi * 4294967296.0);
}

inline double ticksToSecs(u64 ticks) {
    return u64toDouble(ticks) / (__OSBusClock / 4);
}

// // count time taken
// u64 before_ticks = __OSGetSystemTime();

// ... do some work

// double time_delta = ticksToSecs(__OSGetSystemTime() - before_ticks);
// OSReport("TIME DELTA: %f\n", time_delta * 1000.0);
