#include "dolphin_os.h"

#include <ogc/gx.h>

void* (*ogc_malloc)(u32 size) = 0;
void (*ogc_free)(void* ptr) = 0;

void (*ogc_VIDEO_Configure)(GXRModeObj *rmode);
void (*ogc_VIDEO_SetNextFramebuffer)(void* fb);
void (*ogc_VIDEO_WaitVSync)();
u32 (*ogc_VIDEO_GetNextField)();
void (*ogc_VIDEO_SetBlack)(BOOL black);
void (*ogc_VIDEO_Flush)();

void mega_trap(u32 r3, u32 r4, u32 r5, u32 r6) {
    u32 caller = (u32)__builtin_return_address(0);
    OSReport("[%08x] (r3=%08x r4=%08x r5=%08x, r6=%08x) You hit the mega trap dog\n", caller);
    while(1);
}

void* bs2alloc_wrapper(u32 size) {
    // OSReport("CALLER [%08x]\n", (u32)__builtin_return_address(0));
    // OSReport("CALL bs2alloc(%x) @ %08x\n", size, (u32)ogc_malloc);
    void* ptr = ogc_malloc(size);
    // OSReport("CALL bs2alloc ret = %08x\n", (u32)ptr);
    // while(1);
    return ptr;
}

void bs2free_wrapper(void *ptr) {
    ogc_free(ptr);
}

void VIConfigure_wrapper(GXRModeObj *rmode) {
    ogc_VIDEO_Configure(rmode);
}


void VISetNextFrameBuffer_wrapper(void* fb) {
    ogc_VIDEO_SetNextFramebuffer(fb);
}

void VIWaitForRetrace_wrapper() {
    ogc_VIDEO_WaitVSync();
}

u32 VIGetNextField_wrapper() {
    return ogc_VIDEO_GetNextField();
}

void VISetBlack_wrapper(BOOL black) {
    ogc_VIDEO_SetBlack(black);
}

void VIFlush_wrapper() {
    ogc_VIDEO_Flush();
}
