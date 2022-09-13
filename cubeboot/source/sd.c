#include <sdcard/card_cmn.h>
#include <sdcard/card_io.h>
#include <sdcard/gcsd.h>

#include <fat.h>
#include "fatfs/pff.h"

// #include "fatfs/diskio.h"
// #include "fatfs/ff.h"
// #include "ffutils.h"

#include "sd.h"
#include "print.h"

#define countof(a) (sizeof(a)/sizeof(a[0]))

// extern bool disk_isInit[FF_VOLUMES];

const char *dev_names[] = {"sda", "sdb", "sd2"};
const DISC_INTERFACE *drivers[] = {&__io_gcsda, &__io_gcsdb, &__io_gcsd2};
static FATFS current_fs;
// FATFS *fs[] = {NULL, NULL, NULL};

// swiss
int fatFs_Mount(u8 devNum, char *path);
void setSDGeckoSpeed(int slot, bool fast);

static const char *current_dev_name = NULL;
static const DISC_INTERFACE *current_device = NULL;
static int current_device_index = -1;

// check for inserted
static int check_available_devices() {
    for (int i = 0; i < countof(dev_names); i++) {
        const DISC_INTERFACE *driver = drivers[i];
        const char *dev_name = dev_names[i];

        iprintf("Trying mount %s\n", dev_name);

        // if (fatFs_Mount(i, (char*)dev_name) == SD_OK) {
        //     current_device_index = i;
        //     current_dev_name = dev_name;
        // } else {
        //     iprintf("Could not find device (%s)\n", dev_name);
        // }

        sdgecko_setSpeed(i, EXI_SPEED32MHZ);

        if (driver->startup() && driver->isInserted()) {
            // set driver for fatfs
            current_device_index = i;
            // iface = 
            current_device = driver;
            current_dev_name = dev_name;

            // disk_isInit[i] = true;

            iprintf("Using device (%s)\n", dev_name);
            return SD_OK;
        } else {
            iprintf("Could not find device (%s)\n", dev_name);
        }
    }

    return SD_FAIL;
}

int mount_available_device() {
    if (current_device_index < 0) {
        int res = check_available_devices();
        if (res != SD_OK) return res;
    }

    FRESULT mount_result = pf_mount(&current_fs);
    if (mount_result != FR_OK) {
        iprintf("Couldn't mount %s\n", current_dev_name);
        return SD_FAIL;
    }

    // if (!fatMountSimple("sd", current_device)) {
    //     iprintf("Couldn't mount %s\n", current_dev_name);
    //     return SD_FAIL;
    // }

    // char name[256];
    // f_getlabel(current_dev_name, name, NULL);
    // iprintf("Mounted '%s' as %s\n", name, current_dev_name);

    return SD_OK;
}

// unmount current
int unmount_current_device() {
    iprintf("Unmounting %s\n", current_dev_name);
    if (current_device != NULL && !current_device->shutdown()) {
        iprintf("Failed to unmount %s\n", current_dev_name);
    }
    current_device = NULL;
    current_device_index = -1;
    current_dev_name = NULL;

    return SD_OK;
}

const char *get_current_dev_name() {
    return current_dev_name;
}

int get_file_size(char *path) {
    for (char *p = path; *p; ++p)
        *p = (*p >= 'a' && *p <= 'z') ? *p - 0x20 : *p;

    iprintf("Checking %s\n", path);

    // DIR pdir;
    // FRESULT dir_result = pf_opendir(&pdir, "BIOS-SFN");
    // if (dir_result != FR_OK) {
    //     iprintf("Could not dir\n");
    //     return SD_FAIL;
    // }

    // FILINFO entry;
    // while(pf_readdir(&pdir, &entry) == FR_OK && entry.fname[0] != '\0') {
    //     iprintf("file %s\n", entry.fname);
    // }

    // size_t size = 0; //temp

    // char path_buf[255];
    // strcpy(path_buf, "sd:/");
    // strcat(path_buf, path);
    // FILE *file = fopen(path_buf, "rb");
    // if (file == NULL) {
    //     iprintf("Could not open %s\n", path);
    //     return SD_FAIL;   
    // }

    // fseek(file, 0, SEEK_END);
    // long size = ftell(file);

    FRESULT open_result = pf_open(path);
    if (open_result != FR_OK) {
        iprintf("Could not open %s\n", path);
        return SD_FAIL;
    }

    size_t size = pf_size();
    return size;
}

int load_file_dynamic(char *path, void **buf_ptr) {
    for (char *p = path; *p; ++p)
        *p = (*p >= 'a' && *p <= 'z') ? *p - 0x20 : *p;

    iprintf("Reading %s\n", path);
    FRESULT open_result = pf_open(path);
    if (open_result != FR_OK) {
        iprintf("Could not open %s\n", path);
        return SD_FAIL;
    }

    size_t size = pf_size();
    void *buf = malloc(size);
    if (buf == NULL) {
        iprintf("Could not allocate space to read %s (len=%d)\n", path, size);
        return SD_FAIL;
    }
    *buf_ptr = buf;

    u32 _unused;
    FRESULT read_result = pf_read(buf, size, &_unused);
    if (read_result != FR_OK) {
        iprintf("Could not read %s\n", path);
        return SD_FAIL;
    }

    return SD_OK;
}

int load_file_buffer(char *path, void* buf) {
    for (char *p = path; *p; ++p)
        *p = (*p >= 'a' && *p <= 'z') ? *p - 0x20 : *p;

    iprintf("Reading %s\n", path);

    FRESULT open_result = pf_open(path);
    if (open_result != FR_OK) {
        iprintf("Could not open %s\n", path);
        return SD_FAIL;
    }

    size_t size = pf_size();

    u32 _unused;
    FRESULT read_result = pf_read(buf, size, &_unused);
    if (read_result != FR_OK) {
        iprintf("Could not read %s\n", path);
        return SD_FAIL;
    }

    return SD_OK;
}

// from swiss-gc
int fatFs_Mount(u8 devNum, char *path) {
	// if(fs[devNum] != NULL) {
	// 	iprintf("Unmount %i devnum, %s path\n", devNum, path);
	// 	f_unmount(path);
	// 	free(fs[devNum]);
	// 	fs[devNum] = NULL;
	// 	disk_shutdown(devNum);
	// }
	// fs[devNum] = (FATFS*)malloc(sizeof(FATFS));
    // FRESULT mount_res = f_mount(fs[devNum], path, 1);
    // if (mount_res != FR_OK) {
    //     iprintf("Could not mount %s: %s\n", path, get_fresult_message(mount_res));
    //     // iprintf("disk_initialize = %u\n", disk_initialize(fs[current_device_index]->pdrv));
    //     return SD_FAIL;
    // }

    return SD_OK;
}

void setSDGeckoSpeed(int slot, bool fast) {
	sdgecko_setSpeed(slot, fast ? EXI_SPEED32MHZ:EXI_SPEED16MHZ);
	iprintf("SD speed set to %s\n", (fast ? "32MHz":"16MHz"));
}
