// dol loading

#include <malloc.h>

#include <sdcard/gcsd.h>
#include "ffshim.h"
#include "fatfs/ff.h"
#include "utils.h"

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

DOLHEADER *dolhdr;
u32 minaddress = 0;
u32 maxaddress = 0;
u32 _entrypoint, _dst, _src, _len;

u8 dol_buf[6 * 1024 * 1024];

// u8 *dol_buf = NULL;

int load_fat_swiss(const char *slot_name, const DISC_INTERFACE *iface_);
int DOLtoMRAM(unsigned char *dol);
void DOLMinMax(DOLHEADER * dol);
// void dol_alloc(int size);

void load_current_program() {
    _entrypoint = (u32)&_start;
    iprintf("Current program start = %08x\n", _entrypoint);
}

void load_program() {
    // load program
    if (load_fat_swiss("sdb", &__io_gcsdb)) goto load;
    if (load_fat_swiss("sda", &__io_gcsda)) goto load;
    if (load_fat_swiss("sd2", &__io_gcsd2)) goto load;

load:
    iprintf("Program loaded...\n");

    // iprintf("maxaddress = 0x%08x\n", maxaddress);
    // iprintf("minaddress = 0x%08x\n", minaddress);
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
        // dol_alloc(size);
        // if (!dol_buf) {
        //     iprintf("Failed to allocate memory\n");
        //     goto unmount;
        // }
        if (size > sizeof(dol_buf)) {
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

    iprintf("BOOTING\n");
    VIDEO_WaitVSync();

    DOLtoARAM(dol_buf, 0, NULL);

end:
    return res;
}

// void dol_alloc(int size) {
//     int mram_size = (SYS_GetArenaHi() - SYS_GetArenaLo());
//     iprintf("Memory available: %iB\n", mram_size);
//     iprintf("arenaLO = %08x, arenaHI  = %08x\n", SYS_GetArenaLo(), SYS_GetArenaHi());

//     iprintf("DOL size is %iB\n", size);

//     if (size <= 0) {
//         iprintf("Empty DOL\n");
//         return;
//     }

//     dol_buf = (u8*)memalign(32, size);

//     if (!dol_buf) {
//         iprintf("Couldn't allocate memory\n");
//     }
// }


// /****************************************************************************
// * DOLtoMRAM
// *
// * Moves the DOL from main memory to MRAM
// *
// * Pass in a memory pointer to a previously loaded DOL
// ****************************************************************************/
// int DOLtoMRAM(unsigned char *dol) {
//     u32 sizeinbytes;
//     int i;

//     /*** Get DOL header ***/
//     dolhdr = (DOLHEADER *) dol;

//     /*** First, does this look like a DOL? ***/
//     if (dolhdr->textOffset[0] != DOLHDRLENGTH && dolhdr->textOffset[0] != 0x0620)	// DOLX style 
//         return 0;

//     /*** Get DOL stats ***/
//     DOLMinMax(dolhdr);
//     sizeinbytes = maxaddress - minaddress;

//     /*** Move all DOL sections into ARAM ***/
//     /*** Move text sections ***/
//     for (i = 0; i < MAXTEXTSECTION; i++) {
//         /*** This may seem strange, but in developing d0lLZ we found some with section addresses with zero length ***/
//         if (dolhdr->textAddress[i] && dolhdr->textLength[i]) {
//             memcpy((char *) ((dolhdr->textAddress[i] - minaddress) + PROG_ADDR), dol + dolhdr->textOffset[i], dolhdr->textLength[i]);
//         }
//     }

//     /*** Move data sections ***/
//     for (i = 0; i < MAXDATASECTION; i++) {
//         if (dolhdr->dataAddress[i] && dolhdr->dataLength[i]) {
//             memcpy((char *) ((dolhdr->dataAddress[i] - minaddress) + PROG_ADDR), dol + dolhdr->dataOffset[i], dolhdr->dataLength[i]);
//         }
//     }

//     iprintf("LOADCMD entry=%08x, minaddr=%08x, size=%08x\n", dolhdr->entryPoint, minaddress, sizeinbytes);

// 	_entrypoint = dolhdr->entryPoint;
// 	_dst = minaddress;
// 	_src = PROG_ADDR;
// 	_len = sizeinbytes;

//     return 0;
// }
