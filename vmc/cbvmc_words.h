/* Copyright (C) 2026 Nigzel / Bjerreman. SPDX-License-Identifier: GPL-2.0-only */
#ifndef CBVMC_WORDS_H
#define CBVMC_WORDS_H

/*
 * A word table (address, original word, patched word) and the checker / applier that puts it into a game image in RAM: the scanner
 * (cbvmc_scan.c) produces the table at boot, nothing is fixed per game.
 * Strict C89, no libc.  The loader checks EVERY original word before it writes any: a DOL that is not exactly the expected
 * one (another version, a baked image whose words moved, an image already patched) is refused and left untouched, never
 * guessed at.
 */
#include <stdint.h>

struct cbvmc_word {
    uint32_t addr;
    uint32_t orig;
    uint32_t patched;
};

enum {
    CBVMC_WORDS_OK = 0,
    CBVMC_WORDS_ERR_ORIG = -1,      /* a word does not hold the expected original: nothing was written */
    CBVMC_WORDS_ERR_RANGE = -2      /* a word lies outside the image */
};

/*
 * `image` holds the game's memory as it is at `base` (the words are read and written in place, big-endian in memory).
 * Returns CBVMC_WORDS_OK, or an error with *bad = index of the first offending word (may be NULL).
 */
int cbvmc_words_apply(uint8_t *image, uint32_t base, uint32_t size, const struct cbvmc_word *w, unsigned n, unsigned *bad);

/* Same checks, writes nothing. */
int cbvmc_words_check(const uint8_t *image, uint32_t base, uint32_t size, const struct cbvmc_word *w, unsigned n, unsigned *bad);

#endif
