#include "uff.h"

#ifdef USE_FAT_LIBFAT
#include <stdio.h>
#include <string.h>

#ifndef __gamecube__
#define __gamecube__ // fix ide issue
#endif
#include <fat.h>

#include "sd.h"

static char *__current_file_path = NULL;

static char *abs_path(char *path) {
    static char path_buf[255];
    strcpy(path_buf, "sd:/");
    strcat(path_buf, path);
    return path_buf;
}

FRESULT uf_mount(FATFS* fs) {
    iprintf("Device ptr = %08x\n", (u32)get_current_device());
    if (!fatMountSimple("sd", get_current_device())) {
        return FR_DISK_ERR;
    }

    return FR_OK;
}

FRESULT uf_open(const char* path) {
    __current_file_path = abs_path((char*)path);
    FILE *file = fopen(__current_file_path, "r");
    if (file == NULL) {
        __current_file_path = NULL;
        return FR_NOT_OPENED;   
    }

    fclose(file);
    return FR_OK;
}

FRESULT uf_read(void* buff, UINT btr, UINT* br) {
    FILE *file = fopen(__current_file_path, "r");
    size_t n = fread(buff, 1, btr, file);
    *br = n;

    fclose(file);
    return FR_OK;
}

FRESULT uf_write(const void* buff, UINT btw, UINT* bw) {
    return FR_DISK_ERR;
}

FRESULT uf_lseek(DWORD ofs) {
    return FR_DISK_ERR;
}

DWORD uf_size() {
    FILE *file = fopen(__current_file_path, "r");
    fseek(file, 0, SEEK_END);
    long size = ftell(file);

    fclose(file);
    return size;
}

FRESULT uf_unmount() {
    __current_file_path = NULL;
    return FR_OK;
}

#endif

#ifdef USE_FAT_FATFS
#include "fatfs/ff.h"
#include "ffshim.h"

#include "sd.h"

ATTRIBUTE_ALIGN(32) static FIL __current_file = {};

FRESULT uf_mount(FATFS* fs) {
    iface = get_current_device();
	return f_mount(fs, "", 1);
}

FRESULT uf_open(const char* path) {
    if (__current_file.flag == 0x00) f_close(&__current_file);
    return f_open(&__current_file, path, FA_READ);
}

FRESULT uf_read(void* buff, UINT btr, UINT* br) {
    return f_read(&__current_file, buff, btr, br);
}

FRESULT uf_write(const void* buff, UINT btw, UINT* bw) {
    return FR_DISK_ERR;
}

FRESULT uf_lseek(DWORD ofs) {
    return FR_DISK_ERR;
}

DWORD uf_size() {
    return f_size(&__current_file);
}

FRESULT uf_unmount() {
    if (__current_file.flag == 0x00) f_close(&__current_file);
    return f_unmount("");
}

#endif
