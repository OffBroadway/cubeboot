// dol loading

#include <malloc.h>

#include <sdcard/gcsd.h>
#include "ffshim.h"
#include "fatfs/ff.h"
#include "utils.h"

#include "crc32.h"
#include "print.h"

#include "boot/sidestep.h"

char *swiss_paths[] = {
    "/BOOT.DOL",
    "/BOOT2.DOL",
    "/IGR.DOL", // used by swiss-gc
    "/IPL.DOL", // used by iplboot
    "/AUTOEXEC.DOL", // used by ActionReplay
};

extern const void _start;
extern const void _edata;

u8 *dol_buf;
u32 *bs2done = (u32*)0x81700000;

int load_fat_swiss(const char *slot_name, const DISC_INTERFACE *iface_);

void load_program() {
    // load program
    if (load_fat_swiss("sdb", &__io_gcsdb)) goto load;
    if (load_fat_swiss("sda", &__io_gcsda)) goto load;
    if (load_fat_swiss("sd2", &__io_gcsd2)) goto load;

load:
    iprintf("Program loaded...\n");
    *bs2done = 0x0;

    iprintf("BOOTING\n");
#ifdef VIDEO_ENABLE
    VIDEO_WaitVSync();
#endif
    DOLtoARAM(dol_buf, 0, NULL);
}

int load_fat_swiss(const char *slot_name, const DISC_INTERFACE *iface_) {
    int res = 0;

    iprintf("Trying %s\n", slot_name);

    FATFS fs;
    iface = iface_;
    FRESULT mount_result = f_mount(&fs, "", 1);
    if (mount_result != FR_OK) {
        iprintf("Couldn't mount %s: %s\n", slot_name, get_fresult_message(mount_result));
        goto end;
    }

    char name[256];
    f_getlabel(slot_name, name, NULL);
    iprintf("Mounted %s as %s\n", name, slot_name);

    for (int f = 0; f < (sizeof(swiss_paths) / sizeof(char *)); f++) {
        char *path = swiss_paths[f];

        iprintf("Reading %s\n", path);
        FIL file;
        FRESULT open_result = f_open(&file, path, FA_READ);
        if (open_result != FR_OK)
        {
            iprintf("Failed to open file: %s\n", get_fresult_message(open_result));
            continue;
        }

        size_t size = f_size(&file);
        dol_buf = memalign(32, size);
        if (!dol_buf) {
            iprintf("Failed to allocate memory\n");
            goto unmount;
        }

        u32 unused;
        f_read(&file, dol_buf, size, &unused);
        f_close(&file);

        iprintf("Loaded DOL into %p\n", dol_buf);

        // No stack - we need it all
        AR_Init(NULL, 0);

        res = 1;
        break;
    }

unmount:
    iprintf("Unmounting %s\n", slot_name);
    iface->shutdown();
    iface = NULL;

end:
    return res;
}
