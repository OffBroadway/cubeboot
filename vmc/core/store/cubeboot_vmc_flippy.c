/* Copyright (C) 2026 Nigzel / Bjerreman. SPDX-License-Identifier: GPL-2.0-only */
/* FlippyDrive file-API store. See include/cubeboot_vmc_flippy.h. */
#include <string.h>

#include "cubeboot_vmc_flippy.h"

typedef struct cubeboot_vmc_flippy ctx_t;
typedef struct cubeboot_vmc_flippy_handle file_t;

#define RW_FLAGS (CUBEBOOT_FLIPPY_FLAGS_DEFAULT | CUBEBOOT_FLIPPY_FLAG_WRITE)

/* ---- cached handles ---------------------------------------------------- */

static file_t *find(ctx_t *c, const char *path)
{
    unsigned i;
    for (i = 0; i < CUBEBOOT_FLIPPY_HANDLES; ++i)
        if (c->files[i].open && strcmp(c->files[i].path, path) == 0)
            return &c->files[i];
    return NULL;
}

static int release(ctx_t *c, file_t *f)
{
    int r;
    if (!f->open) return 0;
    r = cubeboot_flippy_close(&c->api, f->handle);
    f->open = 0;
    return r == 0 ? 0 : -1;
}

static int release_path(ctx_t *c, const char *path)
{
    file_t *f = find(c, path);
    return f ? release(c, f) : 0;
}

static file_t *acquire(ctx_t *c, const char *path)
{
    file_t *f = find(c, path);
    unsigned i;
    uint8_t handle;
    uint64_t size;

    if (f) return f;
    if (strlen(path) >= CUBEBOOT_VMC_PATH_MAX) return NULL;
    for (i = 0; i < CUBEBOOT_FLIPPY_HANDLES; ++i)
        if (!c->files[i].open) { f = &c->files[i]; break; }
    if (!f) {
        f = &c->files[c->next_victim++ % CUBEBOOT_FLIPPY_HANDLES];
        if (release(c, f) != 0) return NULL;
    }
    if (cubeboot_flippy_open(&c->api, path, RW_FLAGS, &handle, &size) != 0)
        return NULL;
    memcpy(f->path, path, strlen(path) + 1);
    f->handle = handle;
    f->size = size;
    f->open = 1;
    return f;
}

/* ---- store operations -------------------------------------------------- */

static int fl_size(void *context, const char *path, uint32_t *bytes)
{
    ctx_t *c = context;
    file_t *f = find(c, path);
    uint8_t handle;
    uint64_t size;
    int r;

    if (f) {
        size = f->size;
    } else {
        r = cubeboot_flippy_open(&c->api, path, CUBEBOOT_FLIPPY_FLAGS_DEFAULT,
                                 &handle, &size);                 /* read-only */
        if (r == (int)CUBEBOOT_FLIPPY_RESULT_NO_FILE ||
            r == (int)CUBEBOOT_FLIPPY_RESULT_NO_PATH)
            return CUBEBOOT_VMC_ERR_MISSING;
        if (r != 0) return CUBEBOOT_VMC_ERR_IO;
        if (cubeboot_flippy_close(&c->api, handle) != 0)
            return CUBEBOOT_VMC_ERR_IO;
    }
    *bytes = size > 0xffffffffu ? 0xffffffffu : (uint32_t)size;
    return CUBEBOOT_VMC_OK;
}

static int fl_read(void *context, const char *path, uint32_t offset,
                   void *buffer, uint32_t length)
{
    ctx_t *c = context;
    file_t *f = acquire(c, path);
    int r = f && cubeboot_flippy_pread(&c->api, f->handle, f->size, offset,
                                       buffer, length) == 0 ? 0 : -1;
    if (f && c->flush_policy == CUBEBOOT_FLIPPY_PER_ACCESS) (void)release(c, f);
    return r;
}

static int fl_write(void *context, const char *path, uint32_t offset,
                    const void *buffer, uint32_t length)
{
    ctx_t *c = context;
    file_t *f = acquire(c, path);
    if (!f || offset > f->size) return -1;          /* no sparse growth */
    if (cubeboot_flippy_pwrite(&c->api, f->handle, offset, buffer, length) != 0) {
        if (c->flush_policy == CUBEBOOT_FLIPPY_PER_ACCESS) (void)release(c, f);
        return -1;
    }
    if ((uint64_t)offset + length > f->size) f->size = (uint64_t)offset + length;
    if (c->flush_policy == CUBEBOOT_FLIPPY_PER_ACCESS) return release(c, f);
    return 0;
}

static void make_parents(ctx_t *c, const char *path)
{
    char dir[CUBEBOOT_VMC_PATH_MAX];
    size_t i, n = strlen(path);

    if (n >= sizeof(dir)) return;
    memcpy(dir, path, n + 1);
    for (i = 1; i < n; ++i) {
        if (dir[i] != '/') continue;
        dir[i] = '\0';
        cubeboot_flippy_mkdir(&c->api, dir);        /* EXIST is fine */
        dir[i] = '/';
    }
}

/*
 * The stock file API documents no explicit "create". We probe read-only
 * first (so an existing file is never opened for write here), then try a
 * write open and require a zero-length file back. A drive that does not
 * create on write-open reports NO_FILE and we fail with ERR_IO.
 */
static int fl_create(void *context, const char *path)
{
    ctx_t *c = context;
    uint32_t size;
    uint8_t handle;
    uint64_t fsize;
    int r;

    r = fl_size(context, path, &size);
    if (r == CUBEBOOT_VMC_OK) return CUBEBOOT_VMC_ERR_EXISTS;
    if (r != CUBEBOOT_VMC_ERR_MISSING) return r;

    make_parents(c, path);
    r = cubeboot_flippy_open(&c->api, path, RW_FLAGS, &handle, &fsize);
    if (r != 0) return CUBEBOOT_VMC_ERR_IO;
    if (cubeboot_flippy_close(&c->api, handle) != 0) return CUBEBOOT_VMC_ERR_IO;
    return fsize == 0 ? CUBEBOOT_VMC_OK : CUBEBOOT_VMC_ERR_IO;
}

static int fl_rename(void *context, const char *from, const char *to)
{
    ctx_t *c = context;
    if (release_path(c, from) != 0 || release_path(c, to) != 0) return -1;
    return cubeboot_flippy_rename(&c->api, from, to) == 0 ? 0 : -1;
}

static int fl_remove(void *context, const char *path)
{
    ctx_t *c = context;
    release_path(c, path);
    return cubeboot_flippy_unlink(&c->api, path) == 0 ? 0 : -1;
}

static int fl_flush(void *context, const char *path)
{
    ctx_t *c = context;
    file_t *f;
    if (c->flush_policy == CUBEBOOT_FLIPPY_FLUSH_NONE) return 0;
    f = find(c, path);
    return f ? release(c, f) : 0;       /* reopened lazily by the next access */
}

void cubeboot_vmc_flippy_init(ctx_t *ctx,
                              const struct cubeboot_flippy_transport *transport,
                              enum cubeboot_flippy_flush_policy policy,
                              struct cubeboot_vmc_store *out)
{
    memset(ctx, 0, sizeof(*ctx));
    cubeboot_flippy_api_init(&ctx->api, transport);
    ctx->flush_policy = policy;
    out->context = ctx;
    out->size = fl_size;
    out->read = fl_read;
    out->write = fl_write;
    out->create = fl_create;
    out->rename = fl_rename;
    out->remove = fl_remove;
    out->flush = fl_flush;
}

int cubeboot_vmc_flippy_close(ctx_t *ctx)
{
    unsigned i;
    int r = 0;
    for (i = 0; i < CUBEBOOT_FLIPPY_HANDLES; ++i)
        if (release(ctx, &ctx->files[i]) != 0) r = -1;
    return r;
}
