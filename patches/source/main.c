#include <math.h>
#include <stddef.h>

#include "picolibc.h"
#include "structs.h"
#include "util.h"
#include "os.h"

#include "usbgecko.h"

#define __attribute_used__ __attribute__((used))
// #define __attribute_himem__ __attribute__((used, section(".himem")))
#define __attribute_data__ __attribute__((section(".data")))
#define __attribute_reloc__ __attribute__((section(".reloc")))
#define __attribute_aligned_data__ __attribute__((aligned(32), section(".data"))) 
#define countof(a) (sizeof(a)/sizeof(a[0]))

#define CUBE_TEX_WIDTH 84
#define CUBE_TEX_HEIGHT 84

__attribute_used__ u32 bs2tick() {
    return 0x10; // end state is 0x10?? (start game)
}

__attribute_aligned_data__ u8 img_data[CUBE_TEX_WIDTH * CUBE_TEX_HEIGHT * 4];

__attribute_data__ u32 prog_entrypoint;
__attribute_data__ u32 prog_dst;
__attribute_data__ u32 prog_src;
__attribute_data__ u32 prog_len;

// used to start game
__attribute_reloc__ u32 (*PADSync)();
__attribute_reloc__ u32 (*OSDisableInterrupts)();
__attribute_reloc__ void (*__OSStopAudioSystem)();
__attribute_reloc__ void (*run)(register void* entry_point, register u32 clear_start, register u32 clear_size);

// This is actually BS2Report on IPL rev 1.2
__attribute_reloc__ void (*OSReport)(const char* text, ...);
__attribute_reloc__ void (*cube_init)();
__attribute_reloc__ void (*main)();

__attribute_reloc__ model *bg_outer_model;
__attribute_reloc__ model *bg_inner_model;
__attribute_reloc__ model *gc_text_model;
__attribute_reloc__ model *logo_model;
__attribute_reloc__ model *cube_model;
__attribute_reloc__ state *cube_state;

__attribute_data__ static GXColorS10 color_cube;
__attribute_data__ static GXColorS10 color_cube_low;
__attribute_data__ static GXColorS10 color_bg_inner;
__attribute_data__ static GXColorS10 color_bg_outer_0;
__attribute_data__ static GXColorS10 color_bg_outer_1;
__attribute_used__ void pre_cube_init() {
    cube_init();

    rgb_color target_color;
    target_color.color = (0xe11915 << 8) | 0xFF;

    // tough colors: 252850 A18594 763C28

    u32 target_hsl = GRRLIB_RGBToHSL(target_color.color);
    u32 target_hue = H(target_hsl);
    u32 target_sat = S(target_hsl);
    u32 target_lum = L(target_hsl);
    float sat_mult = (float)target_sat / 40.0; //* 1.5;
    if (sat_mult > 2.0) sat_mult = sat_mult * 0.5;
    if (sat_mult > 1.5) sat_mult = sat_mult * 0.5;
    // sat_mult = 0.35; // temp for light colors
    // sat_mult = 1.0;
    float lum_mult = (float)target_lum / 135.0; //* 0.75;
    if (lum_mult < 0.75) lum_mult = lum_mult * 1.5;
    OSReport("SAT MULT = %f\n", sat_mult);
    OSReport("LUM MULT = %f\n", lum_mult);
    OSReport("TARGET_HI: %02x%02x%02x = (%d, %d, %d)\n", target_color.parts.r, target_color.parts.g, target_color.parts.b, H(target_hsl), S(target_hsl), L(target_hsl));

    u8 low_hue = (u8)round((float)H(target_hsl) * 1.02857143);
    u8 low_sat = (u8)round((float)S(target_hsl) * 1.09482759);
    u8 low_lum = (u8)round((float)L(target_hsl) * 0.296296296);
    u32 low_hsl = HSLA(low_hue, low_sat, low_lum, A(target_hsl));
    rgb_color target_low;
    target_low.color = GRRLIB_HSLToRGB(low_hsl);
    OSReport("TARGET_LO: %02x%02x%02x = (%d, %d, %d)\n", target_low.parts.r, target_low.parts.g, target_low.parts.b, H(low_hsl), S(low_hsl), L(low_hsl));

    u8 bg_inner_hue = (u8)round((float)H(target_hsl) * 1.00574712);
    u8 bg_inner_sat = (u8)round((float)S(target_hsl) * 0.95867768);
    u8 bg_inner_lum = (u8)round((float)L(target_hsl) * 0.9);
    u32 bg_inner_hsl = HSLA(bg_inner_hue, bg_inner_sat, bg_inner_lum, bg_inner_model->data->mat[0].tev_color[0]->a);
    rgb_color bg_inner;
    bg_inner.color = GRRLIB_HSLToRGB(bg_inner_hsl);

    u8 bg_outer_hue_0 = (u8)round((float)H(target_hsl) * 1.02941176);
    u8 bg_outer_sat_0 = 0xFF;
    u8 bg_outer_lum_0 = (u8)round((float)L(target_hsl) * 1.31111111);
    u32 bg_outer_hsl_0 = HSLA(bg_outer_hue_0, bg_outer_sat_0, bg_outer_lum_0, bg_outer_model->data->mat[0].tev_color[0]->a);
    rgb_color bg_outer_0;
    bg_outer_0.color = GRRLIB_HSLToRGB(bg_outer_hsl_0);

    u8 bg_outer_hue_1 = (u8)round((float)H(target_hsl) * 1.07428571);
    u8 bg_outer_sat_1 = (u8)round((float)S(target_hsl) * 0.61206896);
    u8 bg_outer_lum_1 = (u8)round((float)L(target_hsl) * 0.92592592);
    u32 bg_outer_hsl_1 = HSLA(bg_outer_hue_1, bg_outer_sat_1, bg_outer_lum_1, bg_outer_model->data->mat[1].tev_color[0]->a);
    rgb_color bg_outer_1;
    bg_outer_1.color = GRRLIB_HSLToRGB(bg_outer_hsl_1);

    // logo

    DUMP_COLOR(logo_model->data->mat[0].tev_color[0]);

    tex_data *base = logo_model->data->tex->dat;
    for (int i = 0; i < 8; i++) {
        tex_data *p = base + i;
        if (p->width != 84) break; // early exit
        u16 wd = p->width;
        u16 ht = p->height;
        void *img_ptr = (void*)((u8*)base + p->offset + (i * 0x20));
        OSReport("FOUND TEX: %dx%d @ %p\n", wd, ht, img_ptr);

#if 1
        for (int y = 0; y < ht; y++) {
            for (int x = 0; x < wd; x++) {
                u32 color = GRRLIB_GetPixelFromtexImg(x, y, img_ptr, wd);

                // hsl
                {
                    u32 hsl = GRRLIB_RGBToHSL(color);
                    u32 sat = round((float)L(hsl) * sat_mult);
                    if (sat > 0xFF) sat = 0xFF;
                    u32 lum = round((float)L(hsl) * lum_mult);
                    if (lum > 0xFF) lum = 0xFF;
                    color = GRRLIB_HSLToRGB(HSLA(target_hue, (u8)sat, (u8)lum, A(hsl)));
                }

                GRRLIB_SetPixelTotexImg(x, y, img_ptr, wd, color);
            }
        }

        uint32_t buffer_size = (wd * ht) << 2;
        DCFlushRange(img_ptr, buffer_size);
#else
        // decode_tex_data((u32*)img_data, (u32*)img_ptr, wd, ht);

        u32 *pix_data = (u32*)img_data;
        // for (int i = 0; i < wd * ht; i++) {
        //     u32 color = pix_data[i];
        //     // ...
        //     pix_data[i] = color;
        // }

        for (int y = 0; y < ht; y++) {
            for (int x = 0; x < wd; x++) {
                u32 color = GRRLIB_GetPixelFromtexImg(x, y, img_ptr, wd);

                // hsl
                {
                    u32 hsl = GRRLIB_RGBToHSL(color);
                    u32 sat = round((float)L(hsl) * sat_mult);
                    if (sat > 0xFF) sat = 0xFF;
                    u32 lum = round((float)L(hsl) * lum_mult);
                    if (lum > 0xFF) lum = 0xFF;
                    color = GRRLIB_HSLToRGB(HSLA(target_hue, (u8)sat, (u8)lum, A(hsl)));
                }

                pix_data[(y * wd) + x] = color;
            }
        }

        Metaphrasis_convertBufferToRGBA8(img_data, img_ptr, wd, ht);
#endif
    }

    // copy over colors

    copy_color(target_color, &cube_state->color);
    copy_color10(target_color, &color_cube);
    copy_color10(target_low, &color_cube_low);

    copy_color10(bg_inner, &color_bg_inner);
    copy_color10(bg_outer_0, &color_bg_outer_0);
    copy_color10(bg_outer_1, &color_bg_outer_1);

    cube_model->data->mat[0].tev_color[0] = &color_cube;
    cube_model->data->mat[0].tev_color[1] = &color_cube_low;

    logo_model->data->mat[0].tev_color[0] = &color_cube_low;
    logo_model->data->mat[0].tev_color[1] = &color_cube_low; // TODO: use different shades

    bg_inner_model->data->mat[0].tev_color[0] = &color_bg_inner;
    bg_inner_model->data->mat[1].tev_color[0] = &color_bg_inner;

    bg_outer_model->data->mat[0].tev_color[0] = &color_bg_outer_0;
    bg_outer_model->data->mat[1].tev_color[0] = &color_bg_outer_1;

    return;
}

void pre_main() {
    OSReport("RUNNING BEFORE MAIN\n");

    // extern GXRModeObj TVNtsc480ProgAa;
    // memcpy((void*)0x8135dde0, &TVNtsc480ProgAa, sizeof(GXRModeObj));

    OSReport("LOADCMD %x, %x, %x, %x\n", prog_entrypoint, prog_dst, prog_src, prog_len);
    memmove((void*)prog_dst, (void*)prog_src, prog_len);

    main();

    __builtin_unreachable();
}

__attribute_used__ void bs2start() {
    OSReport("DONE\n");
    *(u32*)0x81700000 = 0xCAFEBEEF;

    while (!PADSync());
    OSDisableInterrupts();
    __OSStopAudioSystem();

    if (prog_entrypoint == 0) {
        OSReport("HALT: No program\n");
        while(1); // block forever
    }

    void (*entry)(void) = (void(*)(void))prog_entrypoint;
    run(entry, 0x81300000, 0x20000);

    __builtin_unreachable();
}

// unused
void _start() {}
