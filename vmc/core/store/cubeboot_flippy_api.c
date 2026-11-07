/* Copyright (C) 2026 Nigzel / Bjerreman. SPDX-License-Identifier: GPL-2.0-only */
/* Raw FlippyDrive file API. See include/cubeboot_flippy_api.h. */
#include <string.h>

#include "cubeboot_flippy_api.h"

typedef struct cubeboot_flippy_api api_t;

enum {
    SUB_STATUS = 0x00, SUB_SET_DISC = 0x01, SUB_MKDIR = 0x07, SUB_READ = 0x08, SUB_WRITE = 0x09,
    SUB_OPEN = 0x0a, SUB_CLOSE = 0x0b, SUB_UNLINK = 0x0e, SUB_READDIR = 0x0f, SUB_RENAME = 0x13,
};

/* Path record (288 bytes, big-endian) and status record (32 bytes) layouts. */
enum { REC_NAME = 0, REC_TYPE = 256, REC_FLAGS = 257 };
enum { ST_RESULT = 0, ST_SIZE = 4, ST_HANDLE = 12 };

#define IO CUBEBOOT_FLIPPY_IO

static uint32_t be32(const uint8_t *p)
{
    return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) |
           ((uint32_t)p[2] << 8) | p[3];
}
static uint16_t be16(const uint8_t *p) { return (uint16_t)((p[0] << 8) | p[1]); }
/* The status record's multi-byte fields are little-endian (RP2040 native). Evidence: upstream Cubeboot byte-swaps fsize (flippy_sync.c load_stub),
   and the console probe run 1 read a 16384-byte file as 0x0040000000000000 big-endian = 0x4000 little-endian. */
static uint32_t le32(const uint8_t *p)
{
    return ((uint32_t)p[3] << 24) | ((uint32_t)p[2] << 16) | ((uint32_t)p[1] << 8) | p[0];
}
static uint64_t le64(const uint8_t *p)
{
    return ((uint64_t)le32(p + 4) << 32) | le32(p);
}
static uint32_t round32(uint32_t n) { return (n + 31u) & ~31u; }

void cubeboot_flippy_api_init(api_t *api,
                              const struct cubeboot_flippy_transport *t)
{
    memset(api, 0, sizeof(*api));
    api->transport = *t;
}

static int xfer(api_t *a, uint32_t w0, uint32_t arg1, uint32_t arg2,
                enum cubeboot_flippy_dir dir, void *buf, uint32_t len)
{
    uint32_t cmd[3];
    cmd[0] = w0;
    cmd[1] = arg1;
    cmd[2] = arg2;
    return a->transport.command(a->transport.context, cmd, dir, buf, len) == 0
        ? 0 : IO;
}

static int ipc(api_t *a, uint32_t sub_word, uint32_t arg1, uint32_t arg2,
               enum cubeboot_flippy_dir dir, void *buf, uint32_t len)
{
    return xfer(a, (CUBEBOOT_FLIPPY_CMD_PREFIX << 24) | sub_word, arg1, arg2,
                dir, buf, len);
}

int cubeboot_flippy_inquire(api_t *a, struct cubeboot_flippy_drive_info *out)
{
    /*
     * Standard DI inquiry: opcode 0x12, both arguments fixed. The drive's
     * boot/update command uses the same opcode with a magic in the second
     * word, so this word is a constant and takes no caller input.
     */
    static const uint32_t INQUIRY_OPCODE = 0x12000000u;
    const uint8_t *p = a->status;
    int r = xfer(a, INQUIRY_OPCODE, 0, CUBEBOOT_FLIPPY_STATUS_BYTES,
                 CUBEBOOT_FLIPPY_FROM_DRIVE, a->status,
                 CUBEBOOT_FLIPPY_STATUS_BYTES);
    if (r != 0) return r;
    memset(out, 0, sizeof(*out));
    out->revision = be16(p);
    out->device = be16(p + 2);
    out->rel_date = be32(p + 4);
    out->is_flippy = out->rel_date == CUBEBOOT_FLIPPY_REL_DATE_NORMAL;
    out->flags = be32(p + 8);
    out->major = p[12];
    out->minor = p[13];
    out->build = (uint16_t)(be16(p + 14) >> 1);
    out->dirty = (uint8_t)(p[15] & 1u);
    return 0;
}

static int fetch_status(api_t *a, uint32_t *result, uint64_t *size,
                        uint8_t *handle)
{
    if (ipc(a, SUB_STATUS, 0, 0, CUBEBOOT_FLIPPY_FROM_DRIVE, a->status,
            CUBEBOOT_FLIPPY_STATUS_BYTES) != 0)
        return IO;
    /* result codes are small: take the little-endian reading, and the big-endian one only if that is the only one in range */
    *result = le32(a->status + ST_RESULT);
    if (*result > 0xffffu && be32(a->status + ST_RESULT) <= 0xffffu) *result = be32(a->status + ST_RESULT);
    if (size) {
        *size = le64(a->status + ST_SIZE);
        if (*result == 0 && *size > (1ull << 40)) return IO;      /* nonsense: refuse rather than trust it */
    }
    if (handle) *handle = a->status[ST_HANDLE];
    return 0;
}

static int fill_record(uint8_t *rec, const char *path, unsigned type,
                       unsigned flags)
{
    size_t n = strlen(path);
    if (n == 0 || n >= CUBEBOOT_FLIPPY_PATH_MAX) return -1;
    memset(rec, 0, CUBEBOOT_FLIPPY_STAT_BYTES);
    memcpy(rec + REC_NAME, path, n);
    rec[REC_TYPE] = (uint8_t)type;
    rec[REC_FLAGS] = (uint8_t)flags;
    return 0;
}

static void begin(api_t *a)
{
    if (a->transport.begin) a->transport.begin(a->transport.context);
}
static void end(api_t *a)
{
    if (a->transport.end) a->transport.end(a->transport.context);
}

/* One command carrying n_records path records, then the status read, kept
 * together on the bus. */
static int path_command(api_t *a, uint32_t sub, unsigned n_records,
                        uint8_t *handle, uint64_t *size)
{
    uint32_t result;
    int r = IO;
    begin(a);
    if (ipc(a, sub, 0, 0, CUBEBOOT_FLIPPY_TO_DRIVE, a->request,
            n_records * CUBEBOOT_FLIPPY_STAT_BYTES) == 0 &&
        fetch_status(a, &result, size, handle) == 0)
        r = (int)result;
    end(a);
    return r;
}

int cubeboot_flippy_open(api_t *a, const char *path, unsigned flags,
                         uint8_t *handle, uint64_t *size)
{
    int r;
    if (fill_record(a->request, path, 0 /* file */, flags) != 0) return IO;
    r = path_command(a, SUB_OPEN, 1, handle, size);
    if (r == 0 && (*handle == 0 || *handle > CUBEBOOT_FLIPPY_MAX_HANDLE))
        return IO;
    return r;
}

int cubeboot_flippy_opendir(api_t *a, const char *path, uint8_t *handle)
{
    uint64_t size;
    int r;
    if (fill_record(a->request, path, 1 /* directory */, 0) != 0) return IO;
    r = path_command(a, SUB_OPEN, 1, handle, &size);
    if (r == 0 && (*handle == 0 || *handle > CUBEBOOT_FLIPPY_MAX_HANDLE))
        return IO;
    return r;
}

/* One directory entry of an open directory: the drive answers a 288-byte record (name[256], type at 256, the rest unused here); an empty
   name is the end.  Only the name and the type are taken; the record is read into the request buffer (aligned, ours). */
int cubeboot_flippy_readdir(api_t *a, uint8_t handle, char *name, unsigned name_cap, unsigned *type)
{
    unsigned i;
    if (name_cap == 0) return IO;
    begin(a);
    if (ipc(a, ((uint32_t)handle << 16) | SUB_READDIR, 0, 0, CUBEBOOT_FLIPPY_FROM_DRIVE, a->request,
            CUBEBOOT_FLIPPY_STAT_BYTES) != 0) { end(a); return IO; }
    end(a);
    for (i = 0; i + 1 < name_cap && i < CUBEBOOT_FLIPPY_PATH_MAX && a->request[REC_NAME + i]; ++i) name[i] = (char)a->request[REC_NAME + i];
    name[i] = 0;
    *type = a->request[REC_TYPE];
    return 0;
}

int cubeboot_flippy_close(api_t *a, uint8_t handle)
{
    uint32_t result;
    int r = IO;
    begin(a);
    if (ipc(a, ((uint32_t)handle << 16) | SUB_CLOSE, 0, 0,
            CUBEBOOT_FLIPPY_NO_DATA, NULL, 0) == 0 &&
        fetch_status(a, &result, NULL, NULL) == 0)
        r = (int)result;
    end(a);
    return r;
}

int cubeboot_flippy_mkdir(api_t *a, const char *path)
{
    if (fill_record(a->request, path, 1 /* dir */, 0) != 0) return IO;
    return path_command(a, SUB_MKDIR, 1, NULL, NULL);
}

int cubeboot_flippy_unlink(api_t *a, const char *path)
{
    if (fill_record(a->request, path, 0, 0) != 0) return IO;
    return path_command(a, SUB_UNLINK, 1, NULL, NULL);
}

int cubeboot_flippy_rename(api_t *a, const char *from, const char *to)
{
    if (fill_record(a->request, from, 0, 0) != 0 ||
        fill_record(a->request + CUBEBOOT_FLIPPY_STAT_BYTES, to, 0, 0) != 0)
        return IO;
    return path_command(a, SUB_RENAME, 2, NULL, NULL);
}

int cubeboot_flippy_pread(api_t *a, uint8_t handle, uint64_t file_size,
                          uint32_t offset, void *buffer, uint32_t len)
{
    uint8_t *buf = buffer;
    if (offset > file_size || len > file_size - offset) return IO;
    while (len != 0) {
        uint32_t n = len > CUBEBOOT_FLIPPY_CHUNK_BYTES
            ? CUBEBOOT_FLIPPY_CHUNK_BYTES : len;
        uint32_t window = round32(n);
        uint64_t start = offset;
        if (start + window > file_size && file_size < window) {
            /* A file shorter than one DMA window (a 110-byte config file, run 1 of the loader): the transfer must be a multiple of 32 bytes and
               never reads past the end, so rebuild the whole file from an aligned head read and a 32-byte read of the tail, then copy the slice.
               Files under 32 bytes cannot be read at all (pad them). */
            uint32_t size = (uint32_t)file_size, head = size & ~31u, tail = size - head;
            if (size < 32u || size > CUBEBOOT_FLIPPY_CHUNK_BYTES) return IO;
            if (head != 0 && ipc(a, ((uint32_t)handle << 16) | SUB_READ, 0, head, CUBEBOOT_FLIPPY_FROM_DRIVE, a->scratch, head) != 0) return IO;
            if (tail != 0) {
                if (ipc(a, ((uint32_t)handle << 16) | SUB_READ, size - 32u, 32u, CUBEBOOT_FLIPPY_FROM_DRIVE, a->status, 32u) != 0) return IO;
                memcpy(a->scratch + head, a->status + (32u - tail), tail);
            }
            if (offset > size || len > size - offset) return IO;
            memcpy(buf, a->scratch + offset, len);
            return 0;
        }
        if (start + window > file_size) start = file_size - window;
        if (ipc(a, ((uint32_t)handle << 16) | SUB_READ, (uint32_t)start,
                window, CUBEBOOT_FLIPPY_FROM_DRIVE, a->scratch, window) != 0)
            return IO;
        memcpy(buf, a->scratch + (offset - (uint32_t)start), n);
        buf += n; offset += n; len -= n;
    }
    return 0;
}

int cubeboot_flippy_pwrite(api_t *a, uint8_t handle, uint32_t offset,
                           const void *buffer, uint32_t len)
{
    const uint8_t *buf = buffer;
    while (len != 0) {
        uint32_t n = len > CUBEBOOT_FLIPPY_CHUNK_BYTES
            ? CUBEBOOT_FLIPPY_CHUNK_BYTES : len;
        uint32_t padded = round32(n);
        memcpy(a->scratch, buf, n);
        memset(a->scratch + n, 0, padded - n);      /* padding never reaches the file */
        if (ipc(a, ((uint32_t)handle << 16) | SUB_WRITE, offset, n,
                CUBEBOOT_FLIPPY_TO_DRIVE, a->scratch, padded) != 0)
            return IO;
        offset += n; buf += n; len -= n;
    }
    return 0;
}

int cubeboot_flippy_set_disc(api_t *a, uint8_t fd1, uint8_t fd2)
{
    return ipc(a, ((uint32_t)fd1 << 16) | ((uint32_t)fd2 << 8) | SUB_SET_DISC, 0, 0, CUBEBOOT_FLIPPY_NO_DATA, NULL, 0);
}
