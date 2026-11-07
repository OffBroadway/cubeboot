/* Copyright (C) 2026 Nigzel / Bjerreman. SPDX-License-Identifier: GPL-2.0-only */
/* Helpers of tests/cbvmc_t2_test.c: big-endian RAM access, DOL placement, register-init search, store encoders. */
#ifndef CBVMC_T2_UTIL_H
#define CBVMC_T2_UTIL_H

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CHECK(c) do { if (!(c)) { fprintf(stderr, "%s:%d: CHECK failed: %s\n", __FILE__, __LINE__, #c); abort(); } } while (0)
#define RAMSZ 0x01800000u
#define RAMB  0x80000000u

static uint32_t rd(const uint8_t *m, uint32_t a) { const uint8_t *p = m + (a - RAMB); return (uint32_t)p[0] << 24 | (uint32_t)p[1] << 16 | (uint32_t)p[2] << 8 | p[3]; }
static void wr(uint8_t *m, uint32_t a, uint32_t w) { uint8_t *p = m + (a - RAMB); p[0] = (uint8_t)(w >> 24); p[1] = (uint8_t)(w >> 16); p[2] = (uint8_t)(w >> 8); p[3] = (uint8_t)w; }
static uint32_t be(const uint8_t *p) { return (uint32_t)p[0] << 24 | (uint32_t)p[1] << 16 | (uint32_t)p[2] << 8 | p[3]; }

/* One executable as the test places it in RAM. */
struct exe {
    const char *name;
    uint8_t *file; uint32_t len;
    uint32_t shift;                 /* added to every section address (synthesized relocated copy) */
    uint32_t text_lo[7], text_hi[7]; unsigned ntext;
    uint32_t r1, r13;               /* from __init_registers (lis/ori r1, r2, r13) */
};

static uint8_t *slurp(const char *fn, uint32_t *len)
{
    FILE *f = fopen(fn, "rb");
    uint8_t *b;
    long n;
    if (!f) return NULL;
    fseek(f, 0, SEEK_END); n = ftell(f); fseek(f, 0, SEEK_SET);
    b = malloc((size_t)n);
    CHECK(b && fread(b, 1, (size_t)n, f) == (size_t)n);
    fclose(f);
    *len = (uint32_t)n;
    return b;
}

static int exe_open(struct exe *e, const char *name, const char *fn, uint32_t shift)
{
    unsigned s;
    memset(e, 0, sizeof(*e));
    e->name = name; e->shift = shift;
    if (!(e->file = slurp(fn, &e->len))) return 0;
    for (s = 0; s < 7; ++s) {
        uint32_t off = be(e->file + 4 * s), a = be(e->file + 0x48 + 4 * s), n = be(e->file + 0x90 + 4 * s), k;
        if (!off || !n) continue;
        e->text_lo[e->ntext] = a + shift; e->text_hi[e->ntext] = a + shift + n; e->ntext++;
        for (k = 0; k + 24 <= n; k += 4) {
            const uint8_t *p = e->file + off + k;
            if (be(p) >> 16 == 0x3C20 && be(p + 4) >> 16 == 0x6021 && be(p + 8) >> 16 == 0x3C40 && be(p + 12) >> 16 == 0x6042 &&
                be(p + 16) >> 16 == 0x3DA0 && be(p + 20) >> 16 == 0x61AD) {
                e->r1 = (be(p) << 16 | (be(p + 4) & 0xFFFFu)) + shift;
                e->r13 = (be(p + 16) << 16 | (be(p + 20) & 0xFFFFu)) + shift;
            }
        }
    }
    CHECK(e->ntext > 0 && e->r1 && e->r13);
    return 1;
}

/* Copy every section to its address (over whatever the previous executable left), then zero the parts of the header's bss span that no
   section covers (what crt0 clears before OSInit).  Memory outside the image is left as it was. */
static void exe_load(const struct exe *e, uint8_t *m)
{
    unsigned s;
    uint32_t bss = be(e->file + 0xD8) + e->shift, bn = be(e->file + 0xDC), a;
    for (a = bss; a < bss + bn; ++a) {
        int cov = 0;
        for (s = 0; s < 18 && !cov; ++s) {
            uint32_t off = be(e->file + 4 * s), sa = be(e->file + 0x48 + 4 * s) + e->shift, n = be(e->file + 0x90 + 4 * s);
            cov = off && n && a >= sa && a < sa + n;
        }
        if (!cov) m[a - RAMB] = 0;
    }
    for (s = 0; s < 18; ++s) {
        uint32_t off = be(e->file + 4 * s), sa = be(e->file + 0x48 + 4 * s) + e->shift, n = be(e->file + 0x90 + 4 * s);
        if (off && n) { CHECK(off + n <= e->len && sa - RAMB + n <= RAMSZ); memcpy(m + (sa - RAMB), e->file + off, n); }
    }
}

/* The SDK exception stub (__OSEVStart: mtsprg 0,r4 ; lwz r4,0xC0(0)), 0x98 bytes; 0 if absent. */
static uint32_t find_stub(const uint8_t *m, const struct exe *e)
{
    unsigned i;
    uint32_t a;
    for (i = 0; i < e->ntext; ++i)
        for (a = e->text_lo[i]; a + 8 <= e->text_hi[i]; a += 4)
            if (rd(m, a) == 0x7C9043A6u && rd(m, a + 4) == 0x808000C0u) return a;
    return 0;
}

/* Store encoders (D form: op rS,d(rA); X form: 31, xo). */
static uint32_t enc_d(unsigned op, unsigned rs, unsigned ra, int d) { return (uint32_t)op << 26 | rs << 21 | ra << 16 | ((uint32_t)d & 0xFFFFu); }
static uint32_t enc_x(unsigned xo, unsigned rs, unsigned ra, unsigned rb) { return 31u << 26 | rs << 21 | ra << 16 | rb << 11 | xo << 1; }
#define OP_STW 36u
#define OP_STWU 37u
#define OP_STB 38u
#define OP_STBU 39u
#define XO_STWX 151u
#define XO_STWUX 183u
#define XO_STBX 215u
#define XO_STBUX 247u

#endif
