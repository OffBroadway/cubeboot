#include <malloc.h>

#include <sdcard/card_cmn.h>
#include <sdcard/card_io.h>
#include <sdcard/gcsd.h>
#include "gcode.h"
#include "gcm.h"

#include "uff.h"

#include "sd.h"
#include "print.h"

#ifndef USE_FAT_PFF
#include "fatfs/diskio.h"
#endif

#define countof(a) (sizeof(a)/sizeof(a[0]))

const char *dev_names[] = {"sda", "sdb", "sd2", "gcldr"};
const DISC_INTERFACE *drivers[] = {&__io_gcsda, &__io_gcsdb, &__io_gcsd2, &__io_gcode};

static const char *current_dev_name = NULL;
static const DISC_INTERFACE *current_device = NULL;
static int current_device_index = -1;
static bool is_mounted = FALSE;

static struct gcm_system_area *low_mem = (struct gcm_system_area*)0x80000000;

// check for inserted
static int check_available_devices() {
    for (int i = countof(drivers) - 1; i >= 0; i--) {
        const DISC_INTERFACE *driver = drivers[i];
        const char *dev_name = dev_names[i];

        // skip GCLoader if we did not boot from ODE
        if (driver->ioType == DEVICE_TYPE_GAMECUBE_GCODE && low_mem->dhi.country_code != 0x03)
            continue;

        iprintf("Trying mount %s\n", dev_name);
        if (i < MAX_DRIVE)
            sdgecko_setSpeed(i, EXI_SPEED32MHZ);

        if (driver->startup() && driver->isInserted()) {
            // set driver for fatfs
            current_device_index = i;
            current_device = driver;
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
    if (current_device_index < 0) {
        int res = check_available_devices();
        if (res != SD_OK) return res;
    }

    static FATFS fs; // don't leave this on the stack kids
    FRESULT mount_result = uf_mount(&fs);
    if (mount_result != FR_OK) {
        iprintf("Couldn't mount %s (err=%d)\n", current_dev_name, mount_result);
        return SD_FAIL;
    }

    is_mounted = TRUE;
    iprintf("Mounted %s\n", current_dev_name);

    return SD_OK;
}

// unmount current
int unmount_current_device() {
    iprintf("Unmounting %s\n", current_dev_name);
    if (is_device_mounted()) {
        FRESULT unmount_result = uf_unmount();
        if (unmount_result != FR_OK) {
            iprintf("Couldn't unmount %s\n", current_dev_name);
            return SD_FAIL;
        }
    }
    if (current_device != NULL && !current_device->shutdown()) {
        iprintf("Failed to unmount %s\n", current_dev_name);
    }
    current_device = NULL;
    current_device_index = -1;
    current_dev_name = NULL;
    is_mounted = FALSE;

    return SD_OK;
}

bool is_device_mounted() {
    return is_mounted;
}

const char *get_current_dev_name() {
    return current_dev_name;
}

const DISC_INTERFACE *get_current_device() {
    return current_device;
}

int get_file_size(char *path) {
    PATH_FIX(path);
    iprintf("Checking %s\n", path);

    FRESULT open_result = uf_open(path);
    if (open_result != FR_OK) {
        iprintf("Could not open %s\n", path);
        return SD_FAIL;
    }

    size_t size = uf_size();
    return size;
}

int load_file_dynamic(char *path, void **buf_ptr) {
    PATH_FIX(path);
    iprintf("Reading %s\n", path);
    FRESULT open_result = uf_open(path);
    if (open_result != FR_OK) {
        iprintf("Could not open %s\n", path);
        return SD_FAIL;
    }

    size_t size = uf_size();
    void *buf = memalign(32, size);
    if (buf == NULL) {
        iprintf("Could not allocate space to read %s (len=%d)\n", path, size);
        return SD_FAIL;
    }
    *buf_ptr = buf;

    u32 _unused;
    FRESULT read_result = uf_read(buf, size, &_unused);
    if (read_result != FR_OK) {
        iprintf("[D] Could not read %s\n", path);
        return SD_FAIL;
    }

    return SD_OK;
}

int load_file_buffer(char *path, void* buf) {
    iprintf("Reading %s\n", path);

    FRESULT open_result = uf_open(path);
    if (open_result != FR_OK) {
        iprintf("Could not open %s\n", path);
        return SD_FAIL;
    }

    size_t size = uf_size();

    u32 _unused;
    FRESULT read_result = uf_read(buf, size, &_unused);
    if (read_result != FR_OK) {
        iprintf("Could not read %s (err=%d)\n", path, read_result);
        return SD_FAIL;
    }

    return SD_OK;
}
