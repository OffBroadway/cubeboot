// dol loading

#include <malloc.h>

#include "sd.h"

#include "crc32.h"
#include "print.h"
#include "halt.h"

#include "boot/sidestep.h"

char *boot_paths[] = {
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

bool check_load_program() {
    // check if we can even load files
    if (!is_device_mounted()) return false;

    bool found_file = false;
    for (int f = 0; f < (sizeof(boot_paths) / sizeof(char *)); f++) {
        char *path = boot_paths[f];
        int size = get_file_size(path);
        if (size == SD_FAIL) {
            iprintf("Failed to open file: %s\n", path);
            continue;
        } else {
            found_file = true;
            break;
        }
    }

    return found_file;
}

bool load_program(char *path) {
    iprintf("Reading %s\n", path);

    int size = get_file_size(path);
    if (size == SD_FAIL) {
        iprintf("Failed to open file: %s\n", path);
        return false;
    }

    dol_buf = memalign(32, size);
    if (!dol_buf) {
        dol_buf = (u8*)0x81300000;
    }

    if (load_file_buffer(path, dol_buf) != SD_OK) {
        iprintf("Failed to DOL read file: %s\n", path);
        dol_buf = NULL;
        return false;
    }

    iprintf("Loaded DOL into %p\n", dol_buf);
    return true;
}

void boot_program(char *alternative_path) {
    if (alternative_path != NULL) {
        load_program(alternative_path);
    } else {
        for (int f = 0; f < (sizeof(boot_paths) / sizeof(char *)); f++) {
            char *path = boot_paths[f];
            if (load_program(path)) {
                break;
            }
        }
    }

    if (dol_buf == NULL) {
        prog_halt("No program loaded!\n");
        return;
    }

    iprintf("Program loaded...\n");
    *bs2done = 0x0;

    iprintf("BOOTING\n");
#ifdef VIDEO_ENABLE
    VIDEO_WaitVSync();
#endif

    DOLtoARAM(dol_buf, 0, NULL);
}
