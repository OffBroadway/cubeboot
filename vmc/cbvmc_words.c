/* Copyright (C) 2026 Nigzel / Bjerreman. SPDX-License-Identifier: GPL-2.0-only */
/* Word table loader, see cbvmc_words.h.  Original work. */
#include "cbvmc_words.h"

static uint32_t get32(const uint8_t *p)
{
    return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) | ((uint32_t)p[2] << 8) | p[3];
}

static void put32(uint8_t *p, uint32_t v)
{
    p[0] = (uint8_t)(v >> 24); p[1] = (uint8_t)(v >> 16); p[2] = (uint8_t)(v >> 8); p[3] = (uint8_t)v;
}

int cbvmc_words_check(const uint8_t *image, uint32_t base, uint32_t size, const struct cbvmc_word *w, unsigned n, unsigned *bad)
{
    unsigned i;
    for (i = 0; i < n; ++i) {
        uint32_t off = w[i].addr - base;
        if (w[i].addr < base || (w[i].addr & 3u) || off > size || size - off < 4u) {
            if (bad) *bad = i;
            return CBVMC_WORDS_ERR_RANGE;
        }
        if (get32(image + off) != w[i].orig) {
            if (bad) *bad = i;
            return CBVMC_WORDS_ERR_ORIG;
        }
    }
    return CBVMC_WORDS_OK;
}

int cbvmc_words_apply(uint8_t *image, uint32_t base, uint32_t size, const struct cbvmc_word *w, unsigned n, unsigned *bad)
{
    unsigned i;
    int r = cbvmc_words_check(image, base, size, w, n, bad);
    if (r != CBVMC_WORDS_OK) return r;
    for (i = 0; i < n; ++i) put32(image + (w[i].addr - base), w[i].patched);
    return CBVMC_WORDS_OK;
}
