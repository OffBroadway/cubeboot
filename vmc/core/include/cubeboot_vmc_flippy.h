/* Copyright (C) 2026 Nigzel / Bjerreman. SPDX-License-Identifier: GPL-2.0-only */
#ifndef CUBEBOOT_VMC_VMC_FLIPPY_H
#define CUBEBOOT_VMC_VMC_FLIPPY_H

/*
 * FlippyDrive store for the virtual card: struct cubeboot_vmc_store on top of
 * the drive's DI file API (command byte 0xB5), reaching the SD card the drive
 * itself owns.
 *
 * Layers:
 *   transport - ONE function that issues a 12-byte DI command with an
 *               optional 32-byte-aligned DMA payload. The console build
 *               implements it with DI registers; the host tests implement it
 *               with a strict simulated drive.
 *   file API  - open/close/read/write/mkdir/unlink/rename encoded exactly as
 *               documented in docs/FLIPPY_FILE_API_NOTES.md (opcodes, 288-byte
 *               big-endian path records, 32-byte status record).
 *   store     - what cubeboot_vmc_* consumes.
 *
 * Deliberately NOT used: flash files, reset, bypass, boot/update and disc
 * mount/queue commands. The firmware recovery path is never touched.
 *
 * Behaviour the stock file API does not document is isolated and reported
 * rather than assumed (see "Unverified on hardware" in the notes): creating a
 * missing file, growing a file by writing at its end, and durability. A
 * drive that refuses any of them makes create/format fail cleanly, which
 * latches the port in ERROR; the images can then be provisioned from a PC
 * (tools/cubeboot_vmc_format) and everything else still works.
 */

#include "cubeboot_flippy_api.h"
#include "cubeboot_vmc.h"

#define CUBEBOOT_FLIPPY_HANDLES        3u

enum cubeboot_flippy_flush_policy {
    /* Close and lazily reopen the file so the drive commits it. Safest. */
    CUBEBOOT_FLIPPY_FLUSH_REOPEN = 0,
    /* Do nothing; data is committed at close/shutdown only. */
    CUBEBOOT_FLIPPY_FLUSH_NONE,
    /* No handle stays open between calls: every read/write opens, works and
     * closes (IPL menu rule: a handle kept open starves BS2's own file thread). */
    CUBEBOOT_FLIPPY_PER_ACCESS,
};

struct cubeboot_vmc_flippy_handle {
    char path[CUBEBOOT_VMC_PATH_MAX];
    uint64_t size;
    uint8_t open;
    uint8_t handle;
};

struct cubeboot_vmc_flippy {
    struct cubeboot_flippy_api api;
    enum cubeboot_flippy_flush_policy flush_policy;
    struct cubeboot_vmc_flippy_handle files[CUBEBOOT_FLIPPY_HANDLES];
    unsigned next_victim;
};

void cubeboot_vmc_flippy_init(struct cubeboot_vmc_flippy *ctx,
                              const struct cubeboot_flippy_transport *transport,
                              enum cubeboot_flippy_flush_policy policy,
                              struct cubeboot_vmc_store *out);

/* Closes every cached handle. Call before handing the drive back. */
int cubeboot_vmc_flippy_close(struct cubeboot_vmc_flippy *ctx);

#endif
