/* Copyright (C) 2026 Nigzel / Bjerreman. SPDX-License-Identifier: GPL-2.0-only */
#ifndef CUBEBOOT_VMC_FLIPPY_API_H
#define CUBEBOOT_VMC_FLIPPY_API_H

/*
 * Raw FlippyDrive file API over an abstract DI transport.
 *
 * Every call returns the drive's own result code (0 = OK, see below) or
 * CUBEBOOT_FLIPPY_IO (-1) if the transport itself failed, so callers (the
 * virtual-card store, the hardware probe) can tell "drive said no" from "bus
 * failed". Wire facts and provenance: docs/FLIPPY_FILE_API_NOTES.md.
 *
 * Deliberately NOT implemented: flash files, reset, bypass, boot/update and
 * disc mount/queue. The one non-0xB5 command is the standard DI drive
 * inquiry, whose command word is fixed here; it must never be given the
 * arguments of the drive's boot/update command, which shares the opcode.
 */

#include <stddef.h>
#include <stdint.h>

#define CUBEBOOT_FLIPPY_IO              (-1)

#define CUBEBOOT_FLIPPY_CMD_PREFIX      0xb5u
#define CUBEBOOT_FLIPPY_MAX_HANDLE      31u
#define CUBEBOOT_FLIPPY_PATH_MAX        256u
#define CUBEBOOT_FLIPPY_STAT_BYTES      288u
#define CUBEBOOT_FLIPPY_STATUS_BYTES    32u
#ifndef CUBEBOOT_FLIPPY_CHUNK_BYTES
#define CUBEBOOT_FLIPPY_CHUNK_BYTES     8192u   /* < the drive's 16 KiB DMA cap; a resident build may shrink it (RAM) at the price of more commands per transfer */
#endif

/* Open-file flags (same bit values the drive's file API defines). */
#define CUBEBOOT_FLIPPY_FLAG_DISABLE_CACHE    0x01u
#define CUBEBOOT_FLIPPY_FLAG_DISABLE_FASTSEEK 0x02u
#define CUBEBOOT_FLIPPY_FLAG_DISABLE_DVDSPEED 0x04u
#define CUBEBOOT_FLIPPY_FLAG_WRITE            0x08u
#define CUBEBOOT_FLIPPY_FLAGS_DEFAULT \
    (CUBEBOOT_FLIPPY_FLAG_DISABLE_CACHE | CUBEBOOT_FLIPPY_FLAG_DISABLE_FASTSEEK | \
     CUBEBOOT_FLIPPY_FLAG_DISABLE_DVDSPEED)

/* Result codes in the status record (subset we interpret). */
#define CUBEBOOT_FLIPPY_RESULT_OK        0u
#define CUBEBOOT_FLIPPY_RESULT_DISK_ERR  1u
#define CUBEBOOT_FLIPPY_RESULT_NO_FILE   4u
#define CUBEBOOT_FLIPPY_RESULT_NO_PATH   5u
#define CUBEBOOT_FLIPPY_RESULT_DENIED    7u
#define CUBEBOOT_FLIPPY_RESULT_EXIST     8u

/* Drive identification as reported by DI inquiry. */
#define CUBEBOOT_FLIPPY_REL_DATE_NORMAL  0x20220426u   /* drive in normal mode */

enum cubeboot_flippy_dir {
    CUBEBOOT_FLIPPY_NO_DATA = 0,
    CUBEBOOT_FLIPPY_FROM_DRIVE,   /* DMA read: drive -> buf */
    CUBEBOOT_FLIPPY_TO_DRIVE,     /* DMA write: buf -> drive */
};

/*
 * Issue DI command words cmd[0..2]. If dir != NO_DATA, buf is 32-byte aligned
 * and length is a non-zero multiple of 32. Must not return until the transfer
 * has completed or failed. Returns 0 on success, non-zero on a bus/DI error.
 */
struct cubeboot_flippy_transport {
    void *context;
    int (*command)(void *context, const uint32_t cmd[3],
                   enum cubeboot_flippy_dir dir, void *buf, uint32_t length);
    /* Optional (may be NULL). The API brackets every multi-command operation
     * (a command plus its status read) in begin/end so a shared bus can keep
     * the pair together; they nest and carry no result. */
    void (*begin)(void *context);
    void (*end)(void *context);
};

struct cubeboot_flippy_drive_info {
    uint16_t revision;
    uint16_t device;
    uint32_t rel_date;
    uint32_t flags;          /* firmware version block, valid if is_flippy */
    uint8_t major, minor;
    uint16_t build;
    uint8_t dirty;
    uint8_t is_flippy;       /* rel_date == CUBEBOOT_FLIPPY_REL_DATE_NORMAL */
};

/* DMA buffers live here, aligned for the drive, so callers never have to. */
struct cubeboot_flippy_api {
    struct cubeboot_flippy_transport transport;
    _Alignas(32) uint8_t scratch[CUBEBOOT_FLIPPY_CHUNK_BYTES];
    _Alignas(32) uint8_t request[2 * CUBEBOOT_FLIPPY_STAT_BYTES];
    _Alignas(32) uint8_t status[CUBEBOOT_FLIPPY_STATUS_BYTES];
};

void cubeboot_flippy_api_init(struct cubeboot_flippy_api *api,
                              const struct cubeboot_flippy_transport *transport);

/* Standard DI inquiry (read-only). 0 or CUBEBOOT_FLIPPY_IO. */
int cubeboot_flippy_inquire(struct cubeboot_flippy_api *api,
                            struct cubeboot_flippy_drive_info *out);

/* Return the drive's result code (0 = OK) or CUBEBOOT_FLIPPY_IO. */
int cubeboot_flippy_open(struct cubeboot_flippy_api *api, const char *path,
                         unsigned flags, uint8_t *handle, uint64_t *size);
int cubeboot_flippy_close(struct cubeboot_flippy_api *api, uint8_t handle);
/* Directory listing (IPC OPEN of a directory record, then READDIR per entry until the name is empty). 0 or the drive's result / CUBEBOOT_FLIPPY_IO. */
int cubeboot_flippy_opendir(struct cubeboot_flippy_api *api, const char *path, uint8_t *handle);
int cubeboot_flippy_readdir(struct cubeboot_flippy_api *api, uint8_t handle, char *name, unsigned name_cap, unsigned *type);
int cubeboot_flippy_mkdir(struct cubeboot_flippy_api *api, const char *path);
int cubeboot_flippy_unlink(struct cubeboot_flippy_api *api, const char *path);
int cubeboot_flippy_rename(struct cubeboot_flippy_api *api, const char *from,
                           const char *to);

/*
 * Data transfer: 0 on success, CUBEBOOT_FLIPPY_IO on any failure or
 * out-of-range request. Reads are issued only as windows of whole 32-byte
 * units lying entirely inside the file (hence file_size), so the drive is
 * never asked to read past EOF. Writes send the exact byte count; only the
 * DMA payload is padded.
 */
int cubeboot_flippy_pread(struct cubeboot_flippy_api *api, uint8_t handle,
                          uint64_t file_size, uint32_t offset, void *buf,
                          uint32_t length);
int cubeboot_flippy_pwrite(struct cubeboot_flippy_api *api, uint8_t handle,
                           uint32_t offset, const void *buf, uint32_t length);

/*
 * Make the drive serve the open file `fd1` (and optionally a second disc file `fd2`, 0 = none) as the GameCube disc: after this the game's own
 * DVD reads (command 0xA8) are answered from the file.  Command word 0xB5 <fd1> <fd2> 0x01, no data, no status read.  Taken from the public
 * Cubeboot beta-rc15 source (patches/source/flippy_sync.c dvd_set_default_fd), GPL-2.0; see docs/virtual_memory_card.md.  Only the
 * loader DOL (offline/cubeboot_vmc/loader) uses it; the resident store never does.  0 or CUBEBOOT_FLIPPY_IO.
 */
int cubeboot_flippy_set_disc(struct cubeboot_flippy_api *api, uint8_t fd1, uint8_t fd2);

#endif
