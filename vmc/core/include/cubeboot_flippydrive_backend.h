/* Copyright (C) 2026 Nigzel / Bjerreman. SPDX-License-Identifier: GPL-2.0-only */
#ifndef CUBEBOOT_VMC_FLIPPYDRIVE_BACKEND_H
#define CUBEBOOT_VMC_FLIPPYDRIVE_BACKEND_H

#include "cubeboot_bba_backend.h"

/*
 * The only stock-firmware fact represented here is a diagnostic result from
 * the normal network-disc path: a TCP session to the configured FlippyDrive
 * server was observed.  It is deliberately not represented as frame I/O.
 */
enum cubeboot_flippy_stock_network_state {
    CUBEBOOT_FLIPPY_STOCK_NOT_TESTED = 0,
    CUBEBOOT_FLIPPY_STOCK_TCP_TO_SERVER_CONFIRMED,
    CUBEBOOT_FLIPPY_STOCK_NETWORK_DISC_FAILED,
};

struct cubeboot_flippy_stock_diagnostic {
    enum cubeboot_flippy_stock_network_state network_state;
    cubeboot_bba_u8 remote_mode_isolated;
};

/*
 * Installs a diagnostic-only BBA backend. It reports the known link evidence
 * but refuses TX/RX, because stock Remote Access/network-disc mode has no
 * documented concurrent raw-frame ABI for a running game.
 */
void cubeboot_flippy_stock_backend_init(
    struct cubeboot_bba_backend *backend,
    struct cubeboot_flippy_stock_diagnostic *diagnostic);

/*
 * Future documented firmware adapters implement these operations and can be
 * wrapped by Cubeboot without changing the virtual BBA above the seam.
 */
struct cubeboot_flippy_frame_service {
    void *context;
    enum cubeboot_bba_result (*get_capabilities)(
        void *context, struct cubeboot_bba_capabilities *out);
    enum cubeboot_bba_result (*submit_frame)(
        void *context, const cubeboot_bba_u8 *frame, cubeboot_bba_u16 length);
    enum cubeboot_bba_result (*receive_frame)(
        void *context, cubeboot_bba_u8 *frame, cubeboot_bba_u16 capacity,
        cubeboot_bba_u16 *length);
    enum cubeboot_bba_result (*poll)(void *context);
    void (*reset)(void *context);
};

void cubeboot_flippy_frame_backend_init(
    struct cubeboot_bba_backend *backend,
    struct cubeboot_flippy_frame_service *service);

#endif
