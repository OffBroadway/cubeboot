#include "util.h"

// credit https://stackoverflow.com/a/14733008/652487
rgb_color hsv2rgb(hsv_color hsv) {
    rgb_color rgb;
    unsigned char region, remainder, p, q, t;

    if (hsv.parts.s == 0)
    {
        rgb.parts.r = hsv.parts.v;
        rgb.parts.g = hsv.parts.v;
        rgb.parts.b = hsv.parts.v;
        return rgb;
    }

    region = hsv.parts.h / 43;
    remainder = (hsv.parts.h - (region * 43)) * 6; 

    p = (hsv.parts.v * (255 - hsv.parts.s)) >> 8;
    q = (hsv.parts.v * (255 - ((hsv.parts.s * remainder) >> 8))) >> 8;
    t = (hsv.parts.v * (255 - ((hsv.parts.s * (255 - remainder)) >> 8))) >> 8;

    switch (region)
    {
        case 0:
            rgb.parts.r = hsv.parts.v; rgb.parts.g = t; rgb.parts.b = p;
            break;
        case 1:
            rgb.parts.r = q; rgb.parts.g = hsv.parts.v; rgb.parts.b = p;
            break;
        case 2:
            rgb.parts.r = p; rgb.parts.g = hsv.parts.v; rgb.parts.b = t;
            break;
        case 3:
            rgb.parts.r = p; rgb.parts.g = q; rgb.parts.b = hsv.parts.v;
            break;
        case 4:
            rgb.parts.r = t; rgb.parts.g = p; rgb.parts.b = hsv.parts.v;
            break;
        default:
            rgb.parts.r = hsv.parts.v; rgb.parts.g = p; rgb.parts.b = q;
            break;
    }

    return rgb;
}

// credit https://stackoverflow.com/a/14733008/652487
hsv_color rgb2hsv(rgb_color rgb) {
    hsv_color hsv;
    unsigned char rgb_min, rgb_max;

    rgb_min = rgb.parts.r < rgb.parts.g ? (rgb.parts.r < rgb.parts.b ? rgb.parts.r : rgb.parts.b) : (rgb.parts.g < rgb.parts.b ? rgb.parts.g : rgb.parts.b);
    rgb_max = rgb.parts.r > rgb.parts.g ? (rgb.parts.r > rgb.parts.b ? rgb.parts.r : rgb.parts.b) : (rgb.parts.g > rgb.parts.b ? rgb.parts.g : rgb.parts.b);

    hsv.parts.v = rgb_max;
    if (hsv.parts.v == 0)
    {
        hsv.parts.h = 0;
        hsv.parts.s = 0;
        return hsv;
    }

    hsv.parts.s = 255 * (long)(rgb_max - rgb_min) / hsv.parts.v;
    if (hsv.parts.s == 0)
    {
        hsv.parts.h = 0;
        return hsv;
    }

    if (rgb_max == rgb.parts.r)
        hsv.parts.h = 0 + 43 * (rgb.parts.g - rgb.parts.b) / (rgb_max - rgb_min);
    else if (rgb_max == rgb.parts.g)
        hsv.parts.h = 85 + 43 * (rgb.parts.b - rgb.parts.r) / (rgb_max - rgb_min);
    else
        hsv.parts.h = 171 + 43 * (rgb.parts.r - rgb.parts.g) / (rgb_max - rgb_min);

    return hsv;
}