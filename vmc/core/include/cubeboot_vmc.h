/* Copyright (C) 2026 Nigzel / Bjerreman. SPDX-License-Identifier: GPL-2.0-only */
#ifndef CUBEBOOT_VMC_VMC_H
#define CUBEBOOT_VMC_VMC_H

/*
 * Portable virtual memory-card core for cubeboot-resident.
 *
 * Three layers, none of which touch hardware or the C library beyond
 * memcpy/memset, so they build on the host (tests) and under devkitPPC:
 *
 *   store   - byte-addressed file backend (FatFS, Flippy IPC, or a test stub)
 *   card    - one EXI memory-card device: command state machine, flash
 *             semantics (erase to 0xFF, page program), DMA read/write
 *   port    - Slot A/B arbitration: physical card > valid image > create
 *
 * The EXI shim (not part of this module) forwards select/immediate/DMA
 * traffic for a channel to cubeboot_vmc_card_* and delivers the events that
 * cubeboot_vmc_port_service() returns.
 */

#include <stddef.h>
#include <stdint.h>

#include "cubeboot_vmc_layout.h"

/*
 * Card geometry. Standard GameCube cards are 4/8/16/32/64/128 Mbit
 * (59/123/251/507/1019/2043 usable blocks). The header stores the Mbit count
 * (also the EXI "Nintendo ID") and a text encoding, which the console's card
 * library checks against its region: Windows-1252 for NTSC-U and PAL,
 * Shift-JIS for NTSC-J.
 */
#define CUBEBOOT_VMC_MBIT_BYTES      131072u         /* 1 Mbit */
#define CUBEBOOT_VMC_BYTES_FOR(mbits) ((uint32_t)(mbits) * CUBEBOOT_VMC_MBIT_BYTES)
#define CUBEBOOT_VMC_BLOCKS_FOR(mbits) ((uint32_t)(mbits) * 16u)

enum cubeboot_vmc_encoding {
    CUBEBOOT_VMC_ENC_WINDOWS_1252 = 0,
    CUBEBOOT_VMC_ENC_SHIFT_JIS = 1,
};

enum cubeboot_vmc_region {
    CUBEBOOT_VMC_REGION_NTSC_U = 0,
    CUBEBOOT_VMC_REGION_NTSC_J,
    CUBEBOOT_VMC_REGION_PAL,
};

struct cubeboot_vmc_geometry {
    uint16_t mbits;      /* 4, 8, 16, 32, 64 or 128 */
    uint16_t encoding;   /* enum cubeboot_vmc_encoding */
};

/* 128 Mbit (2043 blocks), Windows-1252: the previous fixed behaviour. */
#define CUBEBOOT_VMC_GEOMETRY_DEFAULT { 128u, CUBEBOOT_VMC_ENC_WINDOWS_1252 }

int cubeboot_vmc_mbits_valid(unsigned mbits);
/* Encoding the card library expects for a console/game region. */
enum cubeboot_vmc_encoding cubeboot_vmc_region_encoding(
    enum cubeboot_vmc_region region);
/* Geometry for a region and size (mbits 0 = 128). */
struct cubeboot_vmc_geometry cubeboot_vmc_geometry_for(
    enum cubeboot_vmc_region region, unsigned mbits);
/*
 * Builds "/swiss/saves/MemoryCard<A|B>.<USA|JPN|EUR>.raw" (Swiss's convention:
 * path format and region strings; NTSC-U = USA, NTSC-J = JPN, PAL = EUR).
 * No block count in the name: the card size is the file size. mbits is only
 * validated (0 = 128, else a standard size). Returns 0, or ERR_PATH for a bad
 * argument (NULL/zero-capacity buffer, bad region, slot or size) or if the
 * path does not fit (the buffer is then not guaranteed to be terminated).
 */
int cubeboot_vmc_card_path(char *out, size_t capacity,
                           enum cubeboot_vmc_region region,
                           enum cubeboot_vmc_slot slot, unsigned mbits);
/* Same name under the older "/swiss/patches/" location, for an optional
 * fallback read when the card is not found under /swiss/saves/. */
int cubeboot_vmc_legacy_card_path(char *out, size_t capacity,
                                  enum cubeboot_vmc_region region,
                                  enum cubeboot_vmc_slot slot, unsigned mbits);

#define CUBEBOOT_VMC_PAGE_BYTES      128u
#define CUBEBOOT_VMC_SECTOR_BYTES    512u
#define CUBEBOOT_VMC_PATH_MAX        96u

/* EXI memory-card status register bits (command 0x83). */
#define CUBEBOOT_VMC_ST_BUSY         0x80u
#define CUBEBOOT_VMC_ST_UNLOCKED     0x40u
#define CUBEBOOT_VMC_ST_SLEEP        0x20u
#define CUBEBOOT_VMC_ST_ERASE_ERR    0x10u
#define CUBEBOOT_VMC_ST_PROGRAM_ERR  0x08u
#define CUBEBOOT_VMC_ST_READY        0x01u

enum cubeboot_vmc_result {
    CUBEBOOT_VMC_OK = 0,
    CUBEBOOT_VMC_ERR_IO = -1,
    CUBEBOOT_VMC_ERR_MISSING = -2,
    CUBEBOOT_VMC_ERR_SIZE = -3,        /* present but not exactly 16 MiB */
    CUBEBOOT_VMC_ERR_CORRUPT = -4,     /* size right, metadata invalid */
    CUBEBOOT_VMC_ERR_EXISTS = -5,
    CUBEBOOT_VMC_ERR_PATH = -6,
    CUBEBOOT_VMC_ERR_STATE = -7,
};

/* ---- store: file backend --------------------------------------------- */

struct cubeboot_vmc_store {
    void *context;
    /* Returns OK and the size, or ERR_MISSING if the file does not exist. */
    int (*size)(void *context, const char *path, uint32_t *bytes);
    int (*read)(void *context, const char *path, uint32_t offset,
                void *buffer, uint32_t length);
    /* Writes extend the file when offset == current size (append). */
    int (*write)(void *context, const char *path, uint32_t offset,
                 const void *buffer, uint32_t length);
    /* Creates an empty file; ERR_EXISTS if it already exists. */
    int (*create)(void *context, const char *path);
    /* Must replace atomically with respect to power loss where the
     * filesystem allows it; used only to publish a fully formatted image. */
    int (*rename)(void *context, const char *from, const char *to);
    int (*remove)(void *context, const char *path);
    /* Optional (may be NULL): make everything written to path durable. The
     * card calls it once per completed erase/program, and the formatter
     * before publishing. A failure marks that operation as errored. */
    int (*flush)(void *context, const char *path);
};

/* ---- image: format / validate ---------------------------------------- */

struct cubeboot_vmc_identity {
    uint64_t time;   /* OSTime-style ticks written to the header */
    uint64_t seed;   /* entropy for the 12-byte card serial */
};

/*
 * Validates the raw image at path: a standard size, a header whose Mbit
 * count matches the file size and whose checksum holds, and at least one
 * checksum-valid copy of both the directory and the block-allocation table.
 * Fills *geometry (may be NULL) with what the image actually is. Never
 * modifies the file. Returns OK, ERR_MISSING, ERR_SIZE (not a standard card
 * size), ERR_CORRUPT or ERR_IO.
 */
int cubeboot_vmc_inspect(const struct cubeboot_vmc_store *store,
                         const char *path, struct cubeboot_vmc_geometry *geometry);
int cubeboot_vmc_validate(const struct cubeboot_vmc_store *store,
                          const char *path);

/* Free-block count from the newest valid BAT copy; <0 on error. */
int cubeboot_vmc_free_blocks(const struct cubeboot_vmc_store *store,
                             const char *path);

/*
 * Creates a freshly formatted image of the given geometry (NULL = default).
 * Builds "<path>.tmp", verifies it with cubeboot_vmc_inspect(), then renames
 * it into place: returns ERR_EXISTS (and touches nothing) if path already
 * exists, ERR_SIZE for a non-standard geometry.
 */
int cubeboot_vmc_format(const struct cubeboot_vmc_store *store,
                        const char *path, unsigned slot,
                        const struct cubeboot_vmc_identity *identity,
                        const struct cubeboot_vmc_geometry *geometry);

/* ---- card: one EXI memory-card device -------------------------------- */

struct cubeboot_vmc_card {
    const struct cubeboot_vmc_store *store;
    char path[CUBEBOOT_VMC_PATH_MAX];
    uint8_t status;
    uint8_t command;
    uint8_t interrupt_enable;
    uint8_t interrupt_set;
    uint8_t selected;
    uint8_t programmed_by_dma;
    uint8_t io_error;                 /* sticky until cleared via 0x89 */
    uint32_t position;
    uint32_t address;
    uint8_t program_buffer[CUBEBOOT_VMC_PAGE_BYTES];
    /* Read cache: one 512-byte sector, dropped on any write/erase. */
    uint8_t cache[CUBEBOOT_VMC_SECTOR_BYTES];
    uint32_t cache_base;
    uint8_t cache_valid;
    struct cubeboot_vmc_geometry geometry;   /* of the open image */
    uint32_t image_bytes;
    /* Optional diagnostics hook (NULL = off): type is 'M' (a command finished at chip-select release: command in c->command, c->address, c->position, c->status),
       'R' (DMA read) or 'W' (DMA write); len and result as the model computed them. Set by the owner of the card after cubeboot_vmc_card_open (which clears it). */
    void (*trace)(const struct cubeboot_vmc_card *c, char type, uint32_t addr, uint32_t len, int result, const void *data);   /* data: the DMA buffer of R/W (NULL otherwise) */
};

/* Opens (validates) an existing image; the card starts idle (0x41). */
int cubeboot_vmc_card_open(struct cubeboot_vmc_card *card,
                           const struct cubeboot_vmc_store *store,
                           const char *path);
/* Opens an existing image of a standard size without trusting its header, directory or block map: the geometry comes from the
   file size, the encoding is the caller's. The console sees a card that must be formatted. */
int cubeboot_vmc_card_open_raw(struct cubeboot_vmc_card *card,
                               const struct cubeboot_vmc_store *store,
                               const char *path, unsigned encoding);

/* EXI chip select edge: 1 = asserted, 0 = released (commits erase/program). */
void cubeboot_vmc_card_select(struct cubeboot_vmc_card *card, int selected);

/* One immediate-mode byte: returns the byte the card drives back. */
uint8_t cubeboot_vmc_card_transfer_byte(struct cubeboot_vmc_card *card,
                                        uint8_t byte);

/* DMA after an immediate command phase. Return OK or ERR_STATE/ERR_IO. */
int cubeboot_vmc_card_dma_read(struct cubeboot_vmc_card *card,
                               void *buffer, uint32_t length);
int cubeboot_vmc_card_dma_write(struct cubeboot_vmc_card *card,
                                const void *buffer, uint32_t length);

/* Level of the card's EXI interrupt line (gated by command 0x81). */
int cubeboot_vmc_card_interrupt(const struct cubeboot_vmc_card *card);

/* ---- port: Slot A/B arbitration -------------------------------------- */

enum cubeboot_vmc_event {
    CUBEBOOT_VMC_EVENT_NONE = 0,
    CUBEBOOT_VMC_EVENT_VIRTUAL_INSERTED,  /* game should see a card appear */
    CUBEBOOT_VMC_EVENT_VIRTUAL_REMOVED,   /* game should see the card leave */
};

struct cubeboot_vmc_port {
    const struct cubeboot_vmc_store *store;
    enum cubeboot_vmc_slot slot;
    char path[CUBEBOOT_VMC_PATH_MAX];
    struct cubeboot_vmc_identity identity;
    struct cubeboot_vmc_geometry geometry;   /* used when an image is created */
    uint8_t encoding_mismatch;               /* existing image's encoding differs
                                              * from the requested geometry's */
    enum cubeboot_vmc_source source;
    int last_error;                /* result behind source == ERROR */
    uint8_t physical_present;
    uint8_t dirty;                 /* re-evaluate at next service point */
    struct cubeboot_vmc_card card; /* valid only when source == VIRTUAL */
};

/*
 * `geometry` (NULL = default) is what a NEW image gets. An existing valid
 * image of any standard size is used as it is; if its encoding differs from
 * the requested one the port still goes VIRTUAL but sets encoding_mismatch
 * so the UI can warn (a game of the wrong region will refuse such a card).
 */
int cubeboot_vmc_port_init(struct cubeboot_vmc_port *port,
                           const struct cubeboot_vmc_store *store,
                           enum cubeboot_vmc_slot slot, const char *path,
                           const struct cubeboot_vmc_identity *identity,
                           const struct cubeboot_vmc_geometry *geometry);

/* Record physical presence (EXI ext interrupt or bounded poll). Cheap; does
 * no I/O and never switches the backing store. */
void cubeboot_vmc_port_observe_physical(struct cubeboot_vmc_port *port,
                                        int present);

/* Clear an ERROR latch (user action) so the image is re-examined. */
void cubeboot_vmc_port_retry(struct cubeboot_vmc_port *port);

/*
 * Apply pending changes. Call only at an EXI request boundary; if the
 * virtual card has chip-select asserted this is a no-op, so a request is
 * never switched mid-flight. Returns the event the shim must deliver.
 * An invalid or short image latches ERROR and is never overwritten.
 */
enum cubeboot_vmc_event cubeboot_vmc_port_service(
    struct cubeboot_vmc_port *port);

/* The virtual card to route traffic to, or NULL unless source == VIRTUAL. */
struct cubeboot_vmc_card *cubeboot_vmc_port_card(
    struct cubeboot_vmc_port *port);

#endif
