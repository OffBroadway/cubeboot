/* Copyright (C) 2026 Nigzel / Bjerreman. SPDX-License-Identifier: GPL-2.0-only */
#ifndef CBVMC_RAMSTORE_H
#define CBVMC_RAMSTORE_H

/*
 * RAM-backed store for the card model (struct cubeboot_vmc_store): ONE card image in a caller-owned buffer.
 * Two names are known: the card path and "<path>.tmp" (what cubeboot_vmc_format builds before it renames).  Both name the
 * same buffer; `rename` only flips the "exists" flags, so formatting needs no second copy of the image.
 * No heap, no libc beyond memcpy/memset.  Step 1 of docs/virtual_memory_card.md: the Flippy-backed store replaces this later.
 */
#include <stddef.h>
#include <stdint.h>

#include "cubeboot_vmc.h"

struct cbvmc_ramstore {
    uint8_t *image;
    uint32_t capacity;
    uint32_t size[2];       /* [0] the card path, [1] the .tmp name; valid while exists */
    uint8_t exists[2];
    char path[CUBEBOOT_VMC_PATH_MAX];
    uint32_t reads, writes, erased_bytes;   /* counters for the tests and the result block */
};

/* Binds `store` to `rs` (which is initialised here: no file exists yet). `path` is the card path. */
int cbvmc_ramstore_init(struct cbvmc_ramstore *rs, struct cubeboot_vmc_store *store,
                        uint8_t *image, uint32_t capacity, const char *path);

#endif
