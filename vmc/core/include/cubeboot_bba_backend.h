/* Copyright (C) 2026 Nigzel / Bjerreman. SPDX-License-Identifier: GPL-2.0-only */
#ifndef CUBEBOOT_VMC_BBA_BACKEND_H
#define CUBEBOOT_VMC_BBA_BACKEND_H

/*
 * Frame-backend seam for Cubeboot's future resident virtual BBA.
 *
 * The virtual-BBA register/DMA emulator is above this interface.  A backend
 * only moves complete Ethernet frames (without FCS); it never implements a
 * game protocol or owns a game's network state.
 */
#define CUBEBOOT_BBA_FRAME_MAX 1536u

typedef unsigned char cubeboot_bba_u8;
typedef unsigned short cubeboot_bba_u16;

enum cubeboot_bba_result {
    CUBEBOOT_BBA_OK = 0,
    CUBEBOOT_BBA_EMPTY,
    CUBEBOOT_BBA_WOULD_BLOCK,
    CUBEBOOT_BBA_LINK_DOWN,
    CUBEBOOT_BBA_UNSUPPORTED,
    CUBEBOOT_BBA_ERROR,
};

struct cubeboot_bba_capabilities {
    cubeboot_bba_u8 mac[6];
    cubeboot_bba_u16 max_tx_frame;
    cubeboot_bba_u16 max_rx_frame;
    cubeboot_bba_u8 tx_queue_depth;
    cubeboot_bba_u8 rx_queue_depth;
    cubeboot_bba_u8 link_up;
};

struct cubeboot_bba_backend {
    void *context;
    enum cubeboot_bba_result (*get_capabilities)(
        void *context, struct cubeboot_bba_capabilities *out);
    enum cubeboot_bba_result (*submit_tx)(
        void *context, const cubeboot_bba_u8 *frame, cubeboot_bba_u16 length);
    enum cubeboot_bba_result (*receive_rx)(
        void *context, cubeboot_bba_u8 *frame, cubeboot_bba_u16 capacity,
        cubeboot_bba_u16 *length);
    enum cubeboot_bba_result (*poll)(void *context);
    void (*reset)(void *context);
};

#endif
