/* Copyright (C) 2026 Nigzel / Bjerreman. SPDX-License-Identifier: GPL-2.0-only */
/* Two cards on one write-back window, see cbvmc_wbmux.h.  Original work.  Freestanding (string.h only). */
#include <string.h>

#include "cbvmc_wbmux.h"

static int ensure(struct cbvmc_wbmux_slot *s)
{
    struct cbvmc_wbmux *m = s->mux;
    if (m->cur == s) return 0;
    if (cbvmc_wb_rebind(m->wb, s->path, s->jpath) != 0) return -1;
    m->cur = s;
    return 0;
}

static int m_size(void *c, const char *p, uint32_t *b) { struct cbvmc_wbmux_slot *s = c; return ensure(s) ? -1 : s->mux->inner.size(s->mux->inner.context, p, b); }
static int m_read(void *c, const char *p, uint32_t o, void *b, uint32_t l) { struct cbvmc_wbmux_slot *s = c; return ensure(s) ? -1 : s->mux->inner.read(s->mux->inner.context, p, o, b, l); }
static int m_write(void *c, const char *p, uint32_t o, const void *b, uint32_t l) { struct cbvmc_wbmux_slot *s = c; return ensure(s) ? -1 : s->mux->inner.write(s->mux->inner.context, p, o, b, l); }
static int m_create(void *c, const char *p) { struct cbvmc_wbmux_slot *s = c; return ensure(s) ? -1 : s->mux->inner.create(s->mux->inner.context, p); }
static int m_rename(void *c, const char *a, const char *b) { struct cbvmc_wbmux_slot *s = c; return ensure(s) ? -1 : s->mux->inner.rename(s->mux->inner.context, a, b); }
static int m_remove(void *c, const char *p) { struct cbvmc_wbmux_slot *s = c; return ensure(s) ? -1 : s->mux->inner.remove(s->mux->inner.context, p); }
static int m_flush(void *c, const char *p) { struct cbvmc_wbmux_slot *s = c; return ensure(s) ? -1 : s->mux->inner.flush(s->mux->inner.context, p); }

int cbvmc_wbmux_init(struct cbvmc_wbmux *mux, struct cbvmc_wb *wb, const struct cubeboot_vmc_store *lower, uint8_t *window, uint32_t idle_ticks)
{
    memset(mux, 0, sizeof(*mux));
    mux->wb = wb;
    return cbvmc_wb_init(wb, &mux->inner, lower, "", 0, window, idle_ticks);
}

int cbvmc_wbmux_add(struct cbvmc_wbmux *mux, struct cbvmc_wbmux_slot *slot, struct cubeboot_vmc_store *out, const char *path)
{
    size_t n = strlen(path);
    if (n + 5 > sizeof(slot->jpath) || n >= sizeof(slot->path)) return CUBEBOOT_VMC_ERR_PATH;
    memset(slot, 0, sizeof(*slot));
    slot->mux = mux;
    memcpy(slot->path, path, n + 1);
    memcpy(slot->jpath, path, n);
    memcpy(slot->jpath + n, ".wal", 5);
    memset(out, 0, sizeof(*out));
    out->context = slot;
    out->size = m_size; out->read = m_read; out->write = m_write; out->create = m_create;
    out->rename = m_rename; out->remove = m_remove; out->flush = m_flush;
    return CUBEBOOT_VMC_OK;
}

int cbvmc_wbmux_recover(struct cbvmc_wbmux *mux, const struct cbvmc_wbmux_slot *slot)
{
    if (ensure((struct cbvmc_wbmux_slot *)slot) != 0) return -1;
    return cbvmc_wb_recover(mux->wb);
}

int cbvmc_wbmux_prepare(struct cbvmc_wbmux *mux, const struct cbvmc_wbmux_slot *slot)
{
    if (ensure((struct cbvmc_wbmux_slot *)slot) != 0) return -1;
    return cbvmc_wb_prepare_journal(mux->wb);
}

int cbvmc_wbmux_sync(struct cbvmc_wbmux *mux) { return cbvmc_wb_sync(mux->wb); }
