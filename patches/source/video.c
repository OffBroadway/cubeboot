#include "video.h"

#define GX_FALSE			0
#define GX_TRUE				1

GXRModeObj TVNtsc480ProgAa =
{
    VI_TVMODE_NTSC_PROG,     // viDisplayMode
    640,             // fbWidth
    242,             // efbHeight
    480,             // xfbHeight
    (VI_MAX_WIDTH_NTSC - 640)/2,        // viXOrigin
    (VI_MAX_HEIGHT_NTSC - 480)/2,       // viYOrigin
    640,             // viWidth
    480,             // viHeight
    VI_XFBMODE_SF,   // xFBmode
    GX_FALSE,        // field_rendering
    GX_TRUE,        // aa

    // sample points arranged in increasing Y order
    {
		{3,2},{9,6},{3,10},  // pix 0, 3 sample points, 1/12 units, 4 bits each
		{3,2},{9,6},{3,10},  // pix 1
		{9,2},{3,6},{9,10},  // pix 2
		{9,2},{3,6},{9,10}   // pix 3
    },

    // vertical filter[7], 1/64 units, 6 bits each
    {
          4,         // line n-1
          8,         // line n-1
         12,         // line n
         16,         // line n
         12,         // line n
          8,         // line n+1
          4          // line n+1
    }
};
