/* Copyright (C) 2026 Nigzel / Bjerreman. SPDX-License-Identifier: GPL-2.0-only */
/*
 * Host test of two virtual cards on ONE write-back window (cbvmc_wbmux.c), through the real resident glue of the Dolphin build
 * (cbvmc_resident_ram.c): both cards formatted by the card model, interleaved page programs, switches, reads, journals, recovery.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "cbvmc_resident.h"
#include "cbvmc_wbstore.h"

extern uint8_t *cbvmc_test_card, *cbvmc_test_wal, *cbvmc_test_card_b, *cbvmc_test_wal_b;
extern uint32_t cbvmc_test_card_cap, cbvmc_test_card_len, cbvmc_test_card_b_cap, cbvmc_test_card_b_len;
extern char cbvmc_card_path[], cbvmc_card_path_b[];

#define CHECK(c) do { if (!(c)) { fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #c); exit(1); } } while (0)
#define CAP 0x80000u

static uint8_t A[CAP], B[CAP], WA[CBVMC_WB_JHDR + CBVMC_WB_WINDOW], WB[CBVMC_WB_JHDR + CBVMC_WB_WINDOW];

int main(void)
{
    const struct cubeboot_vmc_store *ta, *tb;
    struct cubeboot_vmc_identity id = { 1, 2 };
    struct cubeboot_vmc_geometry g = { 4, CUBEBOOT_VMC_ENC_WINDOWS_1252 };
    uint8_t buf[512], page[128];
    int detail, i;
    uint32_t n;

    CHECK(strncmp(cbvmc_card_path, "/cubeboot/saves/", 16) == 0 && strncmp(cbvmc_card_path_b, "/cubeboot/saves/", 16) == 0);      /* never Swiss's directory by default */
    CHECK(strcmp(cbvmc_card_path, "/cubeboot/saves/MemoryCardA.USA.raw") == 0 && strcmp(cbvmc_card_path_b, "/cubeboot/saves/MemoryCardB.USA.raw") == 0);      /* = CBVMC_DEFAULT_CARD_A/B of the loader */
    CHECK(strstr(cbvmc_card_path, "/swiss/") == NULL && strstr(cbvmc_card_path_b, "/swiss/") == NULL);          /* never under Swiss's directory */
    cbvmc_test_card = A; cbvmc_test_card_cap = CAP; cbvmc_test_wal = WA;
    cbvmc_test_card_b = B; cbvmc_test_card_b_cap = CAP; cbvmc_test_wal_b = WB;
    CHECK(cbvmc_resident_open(0, &ta, &detail) == CBVMC_OPEN_OK);
    CHECK(cbvmc_resident_open(1, &tb, &detail) == CBVMC_OPEN_OK);
    CHECK(ta != tb);
    CHECK(cubeboot_vmc_format(ta, cbvmc_card_path, 0, &id, &g) == 0);
    CHECK(cubeboot_vmc_format(tb, cbvmc_card_path_b, 1, &id, &g) == 0);
    CHECK(cubeboot_vmc_validate(ta, cbvmc_card_path) == 0);
    CHECK(cubeboot_vmc_validate(tb, cbvmc_card_path_b) == 0);
    CHECK(cbvmc_resident_recover(0) == 0 && cbvmc_resident_recover(1) == 0);
    CHECK(cbvmc_resident_prepare(0) == 0 && cbvmc_resident_prepare(1) == 0);      /* the journals exist in full size before the first save */
    CHECK(cbvmc_resident_prepare(0) == 0);                                          /* idempotent */

    /* interleave: partial writes to A, then to B (must commit A's block first), then A again */
    {
        uint32_t off = 0x10000;                          /* inside the data area, away from the header blocks */
        memset(page, 0xA1, sizeof(page));
        CHECK(ta->write(ta->context, cbvmc_card_path, off, page, sizeof(page)) == 0);
        CHECK(memcmp(A + off, page, 128) != 0);          /* still only in the window */
        memset(page, 0xB2, sizeof(page));
        CHECK(tb->write(tb->context, cbvmc_card_path_b, off, page, sizeof(page)) == 0);
        CHECK(memcmp(A + off, "\xA1\xA1\xA1\xA1", 4) == 0);   /* A's block was committed to A when B took the window */
        CHECK(memcmp(B + off, page, 4) != 0);                  /* B's is in the window now */
        /* reads see each card's own data */
        CHECK(ta->read(ta->context, cbvmc_card_path, off, buf, 4) == 0 && memcmp(buf, "\xA1\xA1\xA1\xA1", 4) == 0);
        CHECK(tb->read(tb->context, cbvmc_card_path_b, off, buf, 4) == 0 && memcmp(buf, "\xB2\xB2\xB2\xB2", 4) == 0);
        CHECK(memcmp(B + off, "\xB2\xB2\xB2\xB2", 4) == 0);    /* the read of B after A's read forced B's commit */
    }
    /* sync commits whoever holds the window */
    memset(page, 0xC3, sizeof(page));
    CHECK(tb->write(tb->context, cbvmc_card_path_b, 0x12000, page, sizeof(page)) == 0);
    CHECK(cbvmc_resident_sync() == 0);
    CHECK(memcmp(B + 0x12000, page, 128) == 0);
    /* A's file is untouched by everything B did, and vice versa */
    for (i = 0; i < 100; ++i) {
        uint32_t o = 0x20000 + (uint32_t)i * 512u;
        memset(buf, i, sizeof(buf));
        CHECK(((i & 1) ? tb : ta)->write(((i & 1) ? tb : ta)->context, (i & 1) ? cbvmc_card_path_b : cbvmc_card_path, o, buf, 512) == 0);
    }
    CHECK(cbvmc_resident_sync() == 0);
    for (i = 0; i < 100; ++i) {
        uint32_t o = 0x20000 + (uint32_t)i * 512u;
        const uint8_t *img = (i & 1) ? B : A, *other = (i & 1) ? A : B;
        for (n = 0; n < 512; ++n) CHECK(img[o + n] == (uint8_t)i);
        CHECK(other[o] != (uint8_t)i || other[o + 1] != (uint8_t)i || (i == 0));      /* the other card has not received it */
    }
    printf("cbvmc_wbmux_test: all passed\n");
    (void)cbvmc_test_card_len; (void)cbvmc_test_card_b_len;
    return 0;
}
