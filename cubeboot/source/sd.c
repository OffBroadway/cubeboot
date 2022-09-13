#include <sdcard/gcsd.h>
#include "ffshim.h"
#include "fatfs/ff.h"
#include "ffutils.h"

#include "sd.h"
#include "print.h"

#define countof(a) (sizeof(a)/sizeof(a[0]))

const char *dev_names[] = {"sda", "sdb", "sd2"};
const DISC_INTERFACE *drivers[] = {&__io_gcsda, &__io_gcsdb, &__io_gcsd2};

static const char *current_dev_name = NULL;
static const DISC_INTERFACE *current_device = NULL;

// check for inserted
static int check_available_devices() {
    for (int i = 0; i < countof(drivers); i++) {
        const DISC_INTERFACE *driver = drivers[i];
        const char *dev_name = dev_names[i];

        if (driver->startup() && driver->isInserted()) {
            // set driver for fatfs
            iface = current_device = driver;
            current_dev_name = dev_name;

            iprintf("Using device (%s)\n", dev_name);
            return SD_OK;
        } else {
            iprintf("Could not find device (%s)\n", dev_name);
        }
    }

    return SD_FAIL;
}

int mount_available_device() {
    if (current_device == NULL) {
        int res = check_available_devices();
        if (res != SD_OK) return res;
    }

    FATFS fs;
    FRESULT mount_result = f_mount(&fs, "", 1);
    if (mount_result != FR_OK) {
        iprintf("Couldn't mount %s: %s\n", current_dev_name, get_fresult_message(mount_result));
        return SD_FAIL;
    }

    char name[256];
    f_getlabel(current_dev_name, name, NULL);
    iprintf("Mounted '%s' as %s\n", name, current_dev_name);

    return SD_OK;
}

// unmount current
int unmount_current_device() {
    iprintf("Unmounting %s\n", current_dev_name);
    if (iface != NULL && !iface->shutdown()) {
        iprintf("Failed to unmount %s\n", current_dev_name);
    }
    iface = NULL;
    current_device = NULL;
    current_dev_name = NULL;

    return SD_OK;
}

const char *get_current_dev_name() {
    return current_dev_name;
}

int get_file_size(char *path) {
    iprintf("Checking %s\n", path);
    FIL file;
    FRESULT open_result = f_open(&file, path, FA_READ);
    if (open_result != FR_OK) {
        return SD_FAIL;
    }

    size_t size = f_size(&file);
    f_close(&file);

    return size;
}

int load_file_dynamic(char *path, void **buf_ptr) {
    iprintf("Reading %s\n", path);
    FIL file;
    FRESULT open_result = f_open(&file, path, FA_READ);
    if (open_result != FR_OK) {
        iprintf("Could not open %s: %s\n", path, get_fresult_message(open_result));
        return SD_FAIL;
    }

    size_t size = f_size(&file);
    void *buf = malloc(size);
    if (buf == NULL) {
        iprintf("Could not allocate space to read %s (len=%d)\n", path, size);
        return SD_FAIL;
    }
    *buf_ptr = buf;

    u32 _unused;
    FRESULT read_result = f_read(&file, buf, size, &_unused);
    if (read_result != FR_OK) {
        iprintf("Could not read %s\n", path);
        return SD_FAIL;
    }

    f_close(&file);
    return SD_OK;
}

int load_file_buffer(char *path, void* buf) {
    iprintf("Reading %s\n", path);
    FIL file;
    FRESULT open_result = f_open(&file, path, FA_READ);
    if (open_result != FR_OK) {
        iprintf("Could not open %s: %s\n", path, get_fresult_message(open_result));
        return SD_FAIL;
    }

    size_t size = f_size(&file);

    u32 _unused;
    FRESULT read_result = f_read(&file, buf, size, &_unused);
    if (read_result != FR_OK) {
        iprintf("Could not read %s\n", path);
        return SD_FAIL;
    }

    f_close(&file);
    return SD_OK;
}
