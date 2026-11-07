/* Copyright (C) 2026 Nigzel / Bjerreman. SPDX-License-Identifier: GPL-2.0-only */
/* See cbvmc_cardgen.h.  Original work.  Freestanding (string.h only). */
#include <string.h>

#include "cbvmc_cardgen.h"

struct counting {
    const struct cubeboot_vmc_store *inner;
    struct cbvmc_gen_stats *st;
    void (*progress)(void *ctx, uint32_t done, uint32_t total);
    void *ctx;
    uint32_t total, next;
};

static int c_size(void *c, const char *p, uint32_t *b) { struct counting *k = c; return k->inner->size(k->inner->context, p, b); }
static int c_read(void *c, const char *p, uint32_t o, void *b, uint32_t l) { struct counting *k = c; return k->inner->read(k->inner->context, p, o, b, l); }
static int c_write(void *c, const char *p, uint32_t o, const void *b, uint32_t l)
{
    struct counting *k = c;
    int r = k->inner->write(k->inner->context, p, o, b, l);
    if (r == 0) {
        k->st->writes++;
        k->st->bytes += l;
        if (k->progress && k->st->bytes >= k->next) { k->next = (k->st->bytes | 0xFFFFFu) + 1u; k->progress(k->ctx, k->st->bytes, k->total); }
    }
    return r;
}
static int c_create(void *c, const char *p) { struct counting *k = c; return k->inner->create(k->inner->context, p); }
static int c_rename(void *c, const char *a, const char *b) { struct counting *k = c; return k->inner->rename(k->inner->context, a, b); }
static int c_remove(void *c, const char *p) { struct counting *k = c; return k->inner->remove(k->inner->context, p); }
static int c_flush(void *c, const char *p) { struct counting *k = c; return k->inner->flush ? k->inner->flush(k->inner->context, p) : 0; }

uint32_t cbvmc_geom_pack(unsigned mbits, unsigned encoding)
{
    return CBVMC_GEOM_MARK | ((encoding & 0xFFu) << 16) | (mbits & 0xFFFFu);
}

int cbvmc_geom_unpack(uint32_t packed, struct cubeboot_vmc_geometry *out)
{
    unsigned mbits = packed & 0xFFFFu, enc = (packed >> 16) & 0xFFu;
    out->mbits = 128u; out->encoding = CUBEBOOT_VMC_ENC_WINDOWS_1252;
    if ((packed & 0xFF000000u) != CBVMC_GEOM_MARK || !cubeboot_vmc_mbits_valid(mbits) || enc > CUBEBOOT_VMC_ENC_SHIFT_JIS) return -1;
    out->mbits = (uint16_t)mbits; out->encoding = (uint16_t)enc;
    return 0;
}

int cbvmc_cardgen(const struct cubeboot_vmc_store *store, const char *path, unsigned slot, const struct cubeboot_vmc_identity *id,
                  const struct cubeboot_vmc_geometry *geom, void (*progress)(void *ctx, uint32_t done, uint32_t total), void *ctx, struct cbvmc_gen_stats *stats)
{
    static const struct cubeboot_vmc_geometry dflt = { 128u, CUBEBOOT_VMC_ENC_WINDOWS_1252 };
    struct cbvmc_gen_stats local;
    struct counting k;
    struct cubeboot_vmc_store s;
    uint32_t size;
    const struct cubeboot_vmc_geometry *g = geom ? geom : &dflt;

    if (!stats) stats = &local;
    memset(stats, 0, sizeof(*stats));
    if (path[0] != '/' || strlen(path) >= CUBEBOOT_VMC_PATH_MAX - 4u) return CBVMC_GEN_ERR_PATH;
    if (store->size(store->context, path, &size) == CUBEBOOT_VMC_OK) return CBVMC_GEN_EXISTS;

    k.inner = store; k.st = stats; k.progress = progress; k.ctx = ctx;
    k.total = CUBEBOOT_VMC_BLOCKS_FOR(g->mbits) * 8192u;
    k.next = 0x100000u;
    memset(&s, 0, sizeof(s));
    s.context = &k;
    s.size = c_size; s.read = c_read; s.write = c_write; s.create = c_create; s.rename = c_rename; s.remove = c_remove; s.flush = c_flush;
    {
        int r = cubeboot_vmc_format(&s, path, slot, id, g);
        return r == CUBEBOOT_VMC_OK ? CBVMC_GEN_CREATED : (r < 0 ? r : -r - 1000);
    }
}
