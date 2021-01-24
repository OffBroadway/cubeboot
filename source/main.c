#include <stdio.h>
#include <stdlib.h>
#include <sdcard/gcsd.h>
#include <malloc.h>
#include <unistd.h>
#include <ogc/lwp_watchdog.h>
#include <ogc/video_types.h>
#include <ogc/system.h>
#include <ogc/pad.h>
#include <fcntl.h>
#include <string.h>
#include "sidestep.h"
#include "ffshim.h"
#include "fatfs/ff.h"

int bs2main();

u8 *dol = NULL;

void dol_alloc(int size)
{
    int mram_size = (SYS_GetArenaHi() - SYS_GetArenaLo());
    kprintf("Memory available: %iB\n", mram_size);

    kprintf("DOL size is %iB\n", size);

    if (size <= 0)
    {
        kprintf("Empty DOL\n");
        return;
    }

    if (size >= (AR_GetSize() - (64 * 1024)))
    {
        kprintf("DOL too big\n");
        return;
    }

    dol = (u8 *) memalign(32, size);

    if (!dol)
    {
        kprintf("Couldn't allocate memory\n");
    }
}

int load_fat(const char *slot_name, const DISC_INTERFACE *iface_)
{
    int res = 1;

    kprintf("Trying %s\n", slot_name);

    FATFS fs;
    iface = iface_;
    if (f_mount(&fs, "", 1) != FR_OK)
    {
        kprintf("Couldn't mount %s\n", slot_name);
        res = 0;
        goto end;
    }

    char name[256];
    f_getlabel(slot_name, name, NULL);
    kprintf("Mounted %s as %s\n", name, slot_name);

    kprintf("Reading ipl.dol\n");
    FIL file;
    if (f_open(&file, "/ipl.dol", FA_READ) != FR_OK)
    {
        kprintf("Failed to open file\n");
        res = 0;
        goto unmount;
    }

    size_t size = f_size(&file);
    dol_alloc(size);
    if (!dol)
    {
        res = 0;
        goto unmount;
    }
    UINT _;
    f_read(&file, dol, size, &_);
    f_close(&file);

unmount:
    kprintf("Unmounting %s\n", slot_name);
    iface->shutdown();
    iface = NULL;

end:
    return res;
}

unsigned int convert_int(unsigned int in)
{
    unsigned int out;
    char *p_in = (char *) &in;
    char *p_out = (char *) &out;
    p_out[0] = p_in[3];
    p_out[1] = p_in[2];
    p_out[2] = p_in[1];
    p_out[3] = p_in[0];
    return out;
}

#define PC_READY 0x80
#define PC_OK    0x81
#define GC_READY 0x88
#define GC_OK    0x89

int load_usb(char slot)
{
    kprintf("Trying USB Gecko in slot %c\n", slot);

    int channel, res = 1;

    switch (slot)
    {
    case 'B':
        channel = 1;
        break;

    case 'A':
    default:
        channel = 0;
        break;
    }

    if (!usb_isgeckoalive(channel))
    {
        kprintf("Not present\n");
        res = 0;
        goto end;
    }

    usb_flush(channel);

    char data;

    kprintf("Sending ready\n");
    data = GC_READY;
    usb_sendbuffer_safe(channel, &data, 1);

    kprintf("Waiting for ack...\n");
    while ((data != PC_READY) && (data != PC_OK))
        usb_recvbuffer_safe(channel, &data, 1);

    if(data == PC_READY)
    {
        kprintf("Respond with OK\n");
        // Sometimes the PC can fail to receive the byte, this helps
        usleep(100000);
        data = GC_OK;
        usb_sendbuffer_safe(channel, &data, 1);
    }

    kprintf("Getting DOL size\n");
    int size;
    usb_recvbuffer_safe(channel, &size, 4);
    size = convert_int(size);

    dol_alloc(size);
    unsigned char* pointer = dol;

    if(!dol)
    {
        res = 0;
        goto end;
    }

    kprintf("Receiving file...\n");
    while (size > 0xF7D8)
    {
        usb_recvbuffer_safe(channel, (void *) pointer, 0xF7D8);
        size -= 0xF7D8;
        pointer += 0xF7D8;
    }
    if(size)
        usb_recvbuffer_safe(channel, (void *) pointer, size);

end:
    return res;
}

//Entry point from crt
int main()
{
    bs2main();
    kprintf("BS2 ERROR >>> SHOULD NEVER REACH HERE");
    while(1);
    return 0;
}

int bs2main()
{
    VIDEO_Init();
    GXRModeObj *rmode = VIDEO_GetPreferredMode(NULL);
    void *xfb = MEM_K0_TO_K1(SYS_AllocateFramebuffer(rmode));
    VIDEO_Configure(rmode);
    VIDEO_SetNextFramebuffer(xfb);
    VIDEO_SetBlack(FALSE);
    VIDEO_Flush();
    VIDEO_WaitVSync();
    if (rmode->viTVMode & VI_NON_INTERLACE)
        VIDEO_WaitVSync();
    console_init(xfb, 20, 20, rmode->fbWidth, rmode->xfbHeight, rmode->fbWidth * 2);

    kprintf("\n\nIPLboot (FlippyBoot Edition)\n");

    *(u32*)0xCC00643C = 0x00000000; // Enable 32MHz EXI

    // Disable Qoob
    u32 val = 6 << 24;
    u32 addr = 0xC0000000;
    EXI_Lock(EXI_CHANNEL_0, EXI_DEVICE_1, NULL);
    EXI_Select(EXI_CHANNEL_0, EXI_DEVICE_1, EXI_SPEED8MHZ);
    EXI_Imm(EXI_CHANNEL_0, &addr, 4, EXI_WRITE, NULL);
    EXI_Sync(EXI_CHANNEL_0);
    EXI_Imm(EXI_CHANNEL_0, &val, 4, EXI_WRITE, NULL);
    EXI_Sync(EXI_CHANNEL_0);
    EXI_Deselect(EXI_CHANNEL_0);
    EXI_Unlock(EXI_CHANNEL_0);

    // Set the timebase properly for games
    // Note: fuck libogc and dkppc
    u32 t = ticks_to_secs(SYS_Time());
    settime(secs_to_ticks(t));

    if (load_usb('B')) goto load;

    if (load_fat("sdb", &__io_gcsdb)) goto load;

    if (load_usb('A')) goto load;

    if (load_fat("sda", &__io_gcsda)) goto load;

    if (load_fat("sd2", &__io_gcsd2)) goto load;

load:
    if (dol)
    {
        DOLtoARAM(dol, 0, NULL);
    }

    // If we reach here, all attempts to load a DOL failed
    // Since we've disabled the Qoob, we wil reboot to the Nintendo IPL
    return 0;
}
