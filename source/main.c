#include <stdio.h>
#include <stdlib.h>
#include <fat.h>
#include <sdcard/gcsd.h>
#include <malloc.h>
#include <unistd.h>
#include <ogc/lwp_watchdog.h>
#include <fcntl.h>
#include "sidestep.h"
#include "exi.h"

static void *xfb = NULL;

u32 first_frame = 1;
GXRModeObj *rmode;

u8 *dol = NULL;

#ifdef SX
#define printf(...) (void)(0)
#endif

void dol_alloc(int size)
{
    int mram_size = (SYS_GetArenaHi() - SYS_GetArenaLo());
    printf("Memory available: %iB\n", mram_size);

    printf("DOL size is %iB\n", size);

    if (size <= 0)
    {
        printf("Empty DOL\n");
        return;
    }

    if (size >= (AR_GetSize() - (64 * 1024)))
    {
        printf("DOL too big\n");
        return;
    }

    dol = (u8 *) memalign(32, size);

    if (!dol)
    {
        printf("Couldn't allocate memory\n");
    }
}

int load_fat(char *slot_name, const DISC_INTERFACE *iface)
{
    int res = 1;

    printf("Trying %s\n", slot_name);

    if (!fatMountSimple(slot_name, iface))
    {
        printf("Couldn't mount %s\n", slot_name);
        res = 0;
        goto end;
    }

    char name[256];
    fatGetVolumeLabel(slot_name, name);
    printf("Mounted %s as %s\n", name, slot_name);

    printf("Reading ipl.dol\n");
    // Avoiding snprintf
    char fn[] = "sd_:/ipl.dol";
    fn[2] = slot_name[2];
    int file = open(fn, O_RDONLY);
    if (file == -1)
    {
        printf("Failed to open file");
        res = 0;
        goto unmount;
    }

    struct stat st;
    fstat(file, &st);
    size_t size = st.st_size;
    dol_alloc(size);
    if (!dol)
    {
        res = 0;
        goto unmount;
    }
    read(file, dol, size);
    close(file);

unmount:
    printf("Unmounting %s\n", slot_name);
    fatUnmount(slot_name);

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
    printf("Trying USB Gecko in slot %c\n", slot);

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
        printf("Not present\n");
        res = 0;
        goto end;
    }

    usb_flush(channel);

    char data;

    printf("Sending ready\n");
    data = GC_READY;
    usb_sendbuffer_safe(channel, &data, 1);

    printf("Waiting for ack...\n");
    while ((data != PC_READY) && (data != PC_OK))
        usb_recvbuffer_safe(channel, &data, 1);

    if(data == PC_READY)
    {
        printf("Respond with OK\n");
        // Sometimes the PC can fail to receive the byte, this helps
        usleep(100000);
        data = GC_OK;
        usb_sendbuffer_safe(channel, &data, 1);
    }

    printf("Getting DOL size\n");
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

    printf("Receiving file...\n");
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

int main()
{
#ifndef SX
    VIDEO_Init();
    rmode = VIDEO_GetPreferredMode(NULL);
    xfb = MEM_K0_TO_K1(SYS_AllocateFramebuffer(rmode));
    VIDEO_Configure(rmode);
    VIDEO_SetNextFramebuffer(xfb);
    VIDEO_SetBlack(FALSE);
    VIDEO_Flush();
    VIDEO_WaitVSync();
    if (rmode->viTVMode & VI_NON_INTERLACE)
        VIDEO_WaitVSync();
    console_init(xfb, 20, 20, rmode->fbWidth, rmode->xfbHeight, rmode->fbWidth * 2);
#endif

    printf("\n\nIPLboot\n");

    *(volatile unsigned long *) 0xCC00643C = 0x00000000; // Enable 27MHz EXI
    ipl_set_config(6); // Disable Qoob

    // Set the timebase properly for games
    // Note: fuck libogc and dkppc
    u32 t = ticks_to_secs(SYS_Time());
    settime(secs_to_ticks(t));

    if (load_usb('B')) goto load;

    if (load_fat("sdb", &__io_gcsdb)) goto load;

    if (load_usb('A')) goto load;

    if (load_fat("sda", &__io_gcsda)) goto load;

load:
    if (dol)
    {
        DOLtoARAM(dol, 0, NULL);
    }

    // If we reach here, all attempts to load a DOL failed
    // Since we've disabled the Qoob, we wil reboot to the Nintendo IPL
    return 0;
}
