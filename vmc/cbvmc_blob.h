/* Copyright (C) 2026 Nigzel / Bjerreman. SPDX-License-Identifier: GPL-2.0-only */
#ifndef CBVMC_BLOB_H
#define CBVMC_BLOB_H

/*
 * The runtime blob (low memory, linked at CBVMC_BLOB_BASE) and what the installer needs to know about it.
 * Layout of the flat blob, offsets from CBVMC_BLOB_BASE:
 *   +0x00  the SDK's own SystemCallVector words (8): __OSInitSystemCall copies them to 0x80000C00 anyway and then flushes 0x100
 *          bytes, so the blob starts with exactly those words and the copy is a no-op
 *   +0x20  DSI entry (reached by `ba` from the DSI vector at 0x80000300, real mode), then the C code, tables, state, stack
 */
#include "cbvmc_ramstore.h"
#include "cbvmc_rt.h"

#define CBVMC_BLOB_BASE    0x80000C00u
#define CBVMC_BLOB_LIMIT   0x80003000u      /* the OS globals start at 0x80003000: the blob must end below this */
#define CBVMC_DSI_VECTOR   0x80000300u
#define CBVMC_DSI_ORIG     0x7C9043A6u      /* mtsprg 0,r4: first word of the SDK's exception stub (OSExceptionVector) */

struct cbvmc_state {
    uint32_t magic;                         /* 'CBVM' */
    uint32_t version;
    struct cbvmc_rt rt;
    struct cubeboot_vmc_store store;
    struct cbvmc_ramstore ram;
    struct cubeboot_vmc_port port;
};
#define CBVMC_STATE_MAGIC 0x4342564Du

extern struct cbvmc_state cbvmc_state;

extern uint32_t cbvmc_sp;

/* Called by the installer after the blob is in place: binds the RAM store, the EXI model and the handler stack. 0 on success. */
int cbvmc_blob_bind(uint8_t *image, uint32_t capacity, const char *path, uint32_t stack_top);

/* Loader build (docs/virtual_memory_card.md): the store (copied) lives outside the blob; `tick` (may be NULL) runs on every
   PI INTSR read with `tick_ctx`.  Does not use the RAM store.  0 on success. */
int cbvmc_blob_bind_ext(const struct cubeboot_vmc_store *store, void (*tick)(void *), void *tick_ctx, uint32_t stack_top);

/* Slot B (EXI channel 1): `port` is the resident image's port record of the second card, already initialised and serviced. 0 on success. */
int cbvmc_blob_bind_b(struct cubeboot_vmc_port *port);

#endif
