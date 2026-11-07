/* Copyright (C) 2026 Nigzel / Bjerreman. SPDX-License-Identifier: GPL-2.0-only */
/* RAM-backed card store, see cbvmc_ramstore.h.  Original work. */
#include <string.h>

#include "cbvmc_ramstore.h"

static int name_slot(const struct cbvmc_ramstore *rs, const char *p)
{
    size_t n = strlen(rs->path);
    if (strncmp(p, rs->path, n) != 0) return -1;
    if (p[n] == '\0') return 0;
    if (strcmp(p + n, ".tmp") == 0) return 1;
    return -1;
}

static int rs_size(void *c, const char *p, uint32_t *bytes)
{
    struct cbvmc_ramstore *rs = c;
    int s = name_slot(rs, p);
    if (s < 0 || !rs->exists[s]) return CUBEBOOT_VMC_ERR_MISSING;
    *bytes = rs->size[s];
    return CUBEBOOT_VMC_OK;
}

static int rs_read(void *c, const char *p, uint32_t off, void *buf, uint32_t len)
{
    struct cbvmc_ramstore *rs = c;
    int s = name_slot(rs, p);
    if (s < 0 || !rs->exists[s] || off > rs->size[s] || len > rs->size[s] - off) return -1;
    memcpy(buf, rs->image + off, len);
    rs->reads++;
    return 0;
}

static int rs_write(void *c, const char *p, uint32_t off, const void *buf, uint32_t len)
{
    struct cbvmc_ramstore *rs = c;
    int s = name_slot(rs, p);
    if (s < 0 || !rs->exists[s] || off > rs->size[s]) return -1;   /* in place or append only */
    if (len > rs->capacity - off) return -1;
    memcpy(rs->image + off, buf, len);
    if (off + len > rs->size[s]) rs->size[s] = off + len;
    rs->writes++;
    return 0;
}

static int rs_create(void *c, const char *p)
{
    struct cbvmc_ramstore *rs = c;
    int s = name_slot(rs, p);
    if (s < 0) return CUBEBOOT_VMC_ERR_PATH;
    if (rs->exists[s]) return CUBEBOOT_VMC_ERR_EXISTS;
    if (rs->exists[s ^ 1]) return CUBEBOOT_VMC_ERR_EXISTS;   /* one buffer: tmp and final never coexist */
    rs->exists[s] = 1;
    rs->size[s] = 0;
    return CUBEBOOT_VMC_OK;
}

static int rs_rename(void *c, const char *from, const char *to)
{
    struct cbvmc_ramstore *rs = c;
    int a = name_slot(rs, from), b = name_slot(rs, to);
    if (a != 1 || b != 0 || !rs->exists[1] || rs->exists[0]) return -1;
    rs->size[0] = rs->size[1];
    rs->exists[0] = 1;
    rs->exists[1] = 0;
    return 0;
}

static int rs_remove(void *c, const char *p)
{
    struct cbvmc_ramstore *rs = c;
    int s = name_slot(rs, p);
    if (s < 0) return CUBEBOOT_VMC_ERR_PATH;
    rs->exists[s] = 0;
    rs->size[s] = 0;
    return CUBEBOOT_VMC_OK;
}

int cbvmc_ramstore_init(struct cbvmc_ramstore *rs, struct cubeboot_vmc_store *store,
                        uint8_t *image, uint32_t capacity, const char *path)
{
    size_t n = strlen(path);
    if (n + 5 > sizeof(rs->path)) return CUBEBOOT_VMC_ERR_PATH;
    memset(rs, 0, sizeof(*rs));
    memcpy(rs->path, path, n + 1);
    rs->image = image;
    rs->capacity = capacity;
    memset(store, 0, sizeof(*store));
    store->context = rs;
    store->size = rs_size;
    store->read = rs_read;
    store->write = rs_write;
    store->create = rs_create;
    store->rename = rs_rename;
    store->remove = rs_remove;
    store->flush = NULL;
    return CUBEBOOT_VMC_OK;
}
