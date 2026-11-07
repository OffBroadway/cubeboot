/* Copyright (C) 2026 Nigzel / Bjerreman. SPDX-License-Identifier: GPL-2.0-only */
#ifndef CUBEBOOT_VMC_VMC_FATFS_H
#define CUBEBOOT_VMC_VMC_FATFS_H

/*
 * FatFS implementation of struct cubeboot_vmc_store.
 *
 * The caller mounts the volume (f_mount) before use and owns the FATFS
 * object. The pinned Cubeboot build of FatFS is READ-ONLY, so the resident
 * stage must be built against its own ffconf.h with at least:
 *
 *   FF_FS_READONLY 0    FF_FS_MINIMIZE 0    (f_write, f_sync, f_rename,
 *                                            f_unlink, f_mkdir, f_stat)
 *   FF_FS_NORTC 1 or a get_fattime()
 *   FF_USE_LFN >= 1     (the image names exceed 8.3)
 *
 * and a disk driver with disk_write()/disk_ioctl(CTRL_SYNC).
 *
 * Open handles are cached (a 128-byte page program must not cost an
 * f_open), so every handle is closed before rename/remove and by _close().
 */

#include "ff.h"
#include "cubeboot_vmc.h"

#define CUBEBOOT_VMC_FATFS_HANDLES 3u   /* slot A, slot B, and a .tmp */

struct cubeboot_vmc_fatfs_handle {
    FIL file;
    char path[CUBEBOOT_VMC_PATH_MAX];
    uint8_t open;
    uint8_t dirty;
};

struct cubeboot_vmc_fatfs {
    struct cubeboot_vmc_fatfs_handle handles[CUBEBOOT_VMC_FATFS_HANDLES];
    unsigned next_victim;
};

/* Fills *out with the FatFS-backed operations bound to ctx. */
void cubeboot_vmc_fatfs_init(struct cubeboot_vmc_fatfs *ctx,
                             struct cubeboot_vmc_store *out);

/* Syncs and closes every cached handle (call before unmount/handoff). */
int cubeboot_vmc_fatfs_close(struct cubeboot_vmc_fatfs *ctx);

#endif
