/* Copyright (C) 2026 Nigzel / Bjerreman. SPDX-License-Identifier: GPL-2.0-only */
#ifndef CUBEBOOT_VMC_VMC_LAYOUT_H
#define CUBEBOOT_VMC_VMC_LAYOUT_H

/* Default (128 Mbit) raw GameCube-card geometry. cubeboot_vmc.h's geometry
 * struct covers every standard size; these remain the defaults. */
#define CUBEBOOT_VMC_SLOT_COUNT          2u
#define CUBEBOOT_VMC_BLOCK_BYTES         8192u
#define CUBEBOOT_VMC_TOTAL_BLOCKS        2048u
#define CUBEBOOT_VMC_FREE_BLOCKS         2043u
#define CUBEBOOT_VMC_IMAGE_BYTES         (16u * 1024u * 1024u)
#define CUBEBOOT_VMC_METADATA_BLOCKS      5u

/* Card paths follow Swiss's convention, so the same images work under Swiss and
 * under the resident service. */
#define CUBEBOOT_VMC_CARD_DIR            "/swiss/saves/"
#define CUBEBOOT_VMC_LEGACY_CARD_DIR     "/swiss/patches/"   /* optional fallback read */
#define CUBEBOOT_VMC_SLOT_A_PATH "/swiss/saves/MemoryCardA.USA.raw"
#define CUBEBOOT_VMC_SLOT_B_PATH "/swiss/saves/MemoryCardB.USA.raw"

enum cubeboot_vmc_slot {
    CUBEBOOT_VMC_SLOT_A = 0,
    CUBEBOOT_VMC_SLOT_B = 1,
};

enum cubeboot_vmc_source {
    CUBEBOOT_VMC_SOURCE_PHYSICAL = 0,
    CUBEBOOT_VMC_SOURCE_VIRTUAL = 1,
    CUBEBOOT_VMC_SOURCE_CREATING = 2,
    CUBEBOOT_VMC_SOURCE_ERROR = 3,
};

/*
 * Port arbitration invariant:
 *
 *   physical card present  => route this port to the physical card
 *   physical card absent + valid image => route this port to its virtual card
 *   physical card absent + no image    => atomically create and format a new image
 *
 * A virtual image is parked (never deleted) while a physical card is present.
 * The transition is delivered to the game as an EXI card removal/insertion event
 * only at an EXI request boundary; never swap backing storage in mid-DMA.
 */
struct cubeboot_vmc_port_state {
    enum cubeboot_vmc_slot slot;
    enum cubeboot_vmc_source source;
    unsigned int physical_present;
    unsigned int image_valid;
    unsigned int request_in_flight;
};

/*
 * Backend contract. Both operations transfer exactly one card page or a
 * card-protocol-approved multiple. Completion must be reported through the
 * resident EXI shim; callers must never block a game frame on filesystem I/O.
 */
struct cubeboot_vmc_request {
    enum cubeboot_vmc_slot slot;
    unsigned int offset;
    unsigned int length;
    void *game_buffer;
    unsigned int write;
};

#endif
