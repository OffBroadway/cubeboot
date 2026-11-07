/* Copyright (C) 2026 Nigzel / Bjerreman. SPDX-License-Identifier: GPL-2.0-only */
/* FatFS-backed card image store. See include/cubeboot_vmc_fatfs.h. */
#include <string.h>

#include "cubeboot_vmc_fatfs.h"

typedef struct cubeboot_vmc_fatfs ctx_t;
typedef struct cubeboot_vmc_fatfs_handle handle_t;

static handle_t *find(ctx_t *c, const char *path)
{
    unsigned i;
    for (i = 0; i < CUBEBOOT_VMC_FATFS_HANDLES; ++i)
        if (c->handles[i].open && strcmp(c->handles[i].path, path) == 0)
            return &c->handles[i];
    return NULL;
}

static int release(handle_t *h)
{
    FRESULT r = FR_OK;
    if (!h->open) return 0;
    if (h->dirty) r = f_sync(&h->file);
    if (f_close(&h->file) != FR_OK && r == FR_OK) r = FR_DISK_ERR;
    h->open = 0;
    h->dirty = 0;
    return r == FR_OK ? 0 : -1;
}

static int release_path(ctx_t *c, const char *path)
{
    handle_t *h = find(c, path);
    return h ? release(h) : 0;
}

/* Opens (or reuses) a read/write handle on an existing file. */
static handle_t *acquire(ctx_t *c, const char *path)
{
    handle_t *h = find(c, path);
    unsigned i;

    if (h) return h;
    if (strlen(path) >= CUBEBOOT_VMC_PATH_MAX) return NULL;
    for (i = 0; i < CUBEBOOT_VMC_FATFS_HANDLES; ++i)
        if (!c->handles[i].open) { h = &c->handles[i]; break; }
    if (!h) {                               /* evict round-robin */
        h = &c->handles[c->next_victim++ % CUBEBOOT_VMC_FATFS_HANDLES];
        if (release(h) != 0) return NULL;
    }
    if (f_open(&h->file, path, FA_READ | FA_WRITE | FA_OPEN_EXISTING) != FR_OK)
        return NULL;
    memcpy(h->path, path, strlen(path) + 1);
    h->open = 1;
    h->dirty = 0;
    return h;
}

static int fs_size(void *context, const char *path, uint32_t *bytes)
{
    ctx_t *c = context;
    handle_t *h = find(c, path);
    FILINFO info;
    FRESULT r;

    if (h) {
        FSIZE_t s = f_size(&h->file);
        *bytes = s > 0xffffffffu ? 0xffffffffu : (uint32_t)s;
        return CUBEBOOT_VMC_OK;
    }
    r = f_stat(path, &info);
    if (r == FR_NO_FILE || r == FR_NO_PATH) return CUBEBOOT_VMC_ERR_MISSING;
    if (r != FR_OK || (info.fattrib & AM_DIR)) return CUBEBOOT_VMC_ERR_IO;
    *bytes = info.fsize > 0xffffffffu ? 0xffffffffu : (uint32_t)info.fsize;
    return CUBEBOOT_VMC_OK;
}

static int fs_read(void *context, const char *path, uint32_t offset,
                   void *buffer, uint32_t length)
{
    handle_t *h = acquire(context, path);
    UINT done;
    if (!h || f_lseek(&h->file, offset) != FR_OK) return -1;
    if (f_read(&h->file, buffer, length, &done) != FR_OK || done != length)
        return -1;
    return 0;
}

static int fs_write(void *context, const char *path, uint32_t offset,
                    const void *buffer, uint32_t length)
{
    handle_t *h = acquire(context, path);
    UINT done;
    if (!h || offset > f_size(&h->file)) return -1;   /* no sparse growth */
    if (f_lseek(&h->file, offset) != FR_OK || f_tell(&h->file) != offset)
        return -1;
    if (f_write(&h->file, buffer, length, &done) != FR_OK || done != length)
        return -1;
    h->dirty = 1;
    return 0;
}

/* Creates every missing directory in front of the final path component. */
static void make_parents(const char *path)
{
    char dir[CUBEBOOT_VMC_PATH_MAX];
    size_t i, n = strlen(path);

    if (n >= sizeof(dir)) return;
    memcpy(dir, path, n + 1);
    for (i = 1; i < n; ++i) {
        if (dir[i] != '/') continue;
        dir[i] = '\0';
        f_mkdir(dir);                       /* FR_EXIST is fine */
        dir[i] = '/';
    }
}

static int fs_create(void *context, const char *path)
{
    FIL f;
    FRESULT r;
    (void)context;
    make_parents(path);
    r = f_open(&f, path, FA_CREATE_NEW | FA_WRITE);
    if (r == FR_EXIST) return CUBEBOOT_VMC_ERR_EXISTS;
    if (r != FR_OK) return CUBEBOOT_VMC_ERR_IO;
    return f_close(&f) == FR_OK ? 0 : CUBEBOOT_VMC_ERR_IO;
}

static int fs_rename(void *context, const char *from, const char *to)
{
    if (release_path(context, from) != 0 || release_path(context, to) != 0)
        return -1;
    return f_rename(from, to) == FR_OK ? 0 : -1;
}

static int fs_remove(void *context, const char *path)
{
    release_path(context, path);
    return f_unlink(path) == FR_OK ? 0 : -1;
}

static int fs_flush(void *context, const char *path)
{
    handle_t *h = find(context, path);
    if (!h || !h->dirty) return 0;
    if (f_sync(&h->file) != FR_OK) return -1;
    h->dirty = 0;
    return 0;
}

void cubeboot_vmc_fatfs_init(ctx_t *ctx, struct cubeboot_vmc_store *out)
{
    memset(ctx, 0, sizeof(*ctx));
    out->context = ctx;
    out->size = fs_size;
    out->read = fs_read;
    out->write = fs_write;
    out->create = fs_create;
    out->rename = fs_rename;
    out->remove = fs_remove;
    out->flush = fs_flush;
}

int cubeboot_vmc_fatfs_close(ctx_t *ctx)
{
    unsigned i;
    int r = 0;
    for (i = 0; i < CUBEBOOT_VMC_FATFS_HANDLES; ++i)
        if (release(&ctx->handles[i]) != 0) r = -1;
    return r;
}
