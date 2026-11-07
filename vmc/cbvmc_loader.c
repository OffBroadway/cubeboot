/* Copyright (C) 2026 Nigzel / Bjerreman. SPDX-License-Identifier: GPL-2.0-only */
/* Loader-side half of the cubeboot virtual card, see cbvmc_loader.h.  Original work.  Freestanding; host-testable. */
#include <string.h>

#include "cbvmc_loader.h"

/* installer stack that follows the install image (= CBVMC_ISTACK_BYTES of cbvmc_image.h, checked by the host test) */
#define CBVMC_ISTACK 0xA000u
#include "cbvmc_words.h"

static uint32_t rd32(const uint8_t *p) { return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) | ((uint32_t)p[2] << 8) | p[3]; }
static void wr32(uint8_t *p, uint32_t v) { p[0] = (uint8_t)(v >> 24); p[1] = (uint8_t)(v >> 16); p[2] = (uint8_t)(v >> 8); p[3] = (uint8_t)v; }

static void flush(uint32_t addr, uint32_t len)
{
#if defined(__powerpc__) || defined(__PPC__)
    uint32_t p;
    for (p = addr & ~31u; p < addr + len; p += 32u)
        __asm__ volatile("dcbf 0,%0 ; sync ; icbi 0,%0" : : "r"(p) : "memory");
    __asm__ volatile("sync ; isync" : : : "memory");
#else
    (void)addr; (void)len;
#endif
}

/* `bl target` placed at `from` (relative, 26-bit signed), or 0 if out of reach */
static uint32_t make_bl(uint32_t from, uint32_t target)
{
    int32_t off = (int32_t)(target - from);
    if (off >= 0x02000000 || off < -0x02000000 || (off & 3)) return 0;
    return 0x48000001u | ((uint32_t)off & 0x03FFFFFCu);
}

static int install_common(const struct cbvmc_loader_cfg *c, const struct cbvmc_word *words, unsigned nwords, uint32_t hook_addr, uint32_t hook_target, int allow_empty)
{
    uint8_t *hook, *blk;
    uint32_t want, bl;
    unsigned bad = 0, i;

    if (!c || !c->ram || !c->res_image || !c->ins_image || c->res_len == 0 || c->ins_len == 0 || c->ram_base != 0x80000000u || (!words && nwords) ||
        (nwords == 0 && !allow_empty) || nwords > CBVMC_HOOKBLK_MAXW)
        return CBVMC_LOADER_ERR_ARGS;
    /* the resident image lies in the region the loader took out of the arena; the install image (plus its stack) inside the arena, above the
       game DOL and below bi2, so that the two never overlap each other, the DOL, or the FST/bi2 the loader placed; the hook block is the last 4 KiB of that region */
    if (c->res_len > CBVMC_R_SIZE || CBVMC_R_BASE + CBVMC_R_SIZE > c->ram_base + c->ram_size ||
        c->ins_len + CBVMC_ISTACK > CBVMC_HOOKBLK - CBVMC_B_BASE || CBVMC_B_BASE + CBVMC_B_SIZE > CBVMC_R_BASE ||
        c->dol_max > CBVMC_B_BASE || c->bi2_addr < CBVMC_B_BASE + CBVMC_B_SIZE || c->bi2_addr > CBVMC_R_BASE) return CBVMC_LOADER_ERR_IMAGE;
    if (cbvmc_words_check(c->ram, c->ram_base, c->ram_size, words, nwords, &bad) != CBVMC_WORDS_OK)
        return CBVMC_LOADER_ERR_WORDS;
    if (hook_addr < c->ram_base || hook_addr + 4u > c->ram_base + c->ram_size || (hook_addr & 3u)) return CBVMC_LOADER_ERR_HOOK;
    hook = c->ram + (hook_addr - c->ram_base);
    want = make_bl(hook_addr, hook_target);
    if (want == 0 || rd32(hook) != want) return CBVMC_LOADER_ERR_HOOK;
    bl = make_bl(hook_addr, CBVMC_B_BASE);          /* the install image entry is at its first byte */
    if (bl == 0) return CBVMC_LOADER_ERR_IMAGE;

    memcpy(c->ram + (CBVMC_R_BASE - c->ram_base), c->res_image, c->res_len);
    flush(CBVMC_R_BASE, c->res_len);
    memcpy(c->ram + (CBVMC_B_BASE - c->ram_base), c->ins_image, c->ins_len);
    flush(CBVMC_B_BASE, c->ins_len);
    blk = c->ram + (CBVMC_HOOKBLK - c->ram_base);
    wr32(blk, CBVMC_HOOKBLK_MAGIC); wr32(blk + 4, hook_addr); wr32(blk + 8, hook_target); wr32(blk + 12, nwords);
    for (i = 0; i < nwords; ++i) { wr32(blk + 16 + 12 * i, words[i].addr); wr32(blk + 20 + 12 * i, words[i].orig); wr32(blk + 24 + 12 * i, words[i].patched); }
    flush(CBVMC_HOOKBLK, 16u + 12u * nwords);
    wr32(hook, bl);
    flush(hook_addr, 4);
    return CBVMC_LOADER_OK;
}

int cbvmc_loader_install_generic(const struct cbvmc_loader_cfg *c, const struct cbvmc_word *words, unsigned nwords, uint32_t hook_addr, uint32_t hook_target)
{
    return install_common(c, words, nwords, hook_addr, hook_target, 1);
}
