#include <stdio.h>
#include <stdbool.h>
#include <string.h>
#include <unistd.h>

#include <ogcsys.h>
#include <gccore.h>
#include <ogc/machine/processor.h>

#include "print.h"

extern GXRModeObj *rmode;

static void *exception_xfb = (void*)0xC1700000;
extern void VIDEO_SetFramebuffer(void *);

void prog_halt(char *msg) {
	VIDEO_SetFramebuffer(exception_xfb);
    console_init(exception_xfb,20,20,rmode->fbWidth,rmode->xfbHeight,rmode->fbWidth*VI_DISPLAY_PIX_SZ);
    CON_EnableGecko(EXI_CHANNEL_1, false);
    printf(msg);
    VIDEO_WaitVSync();
    ppchalt();
}
