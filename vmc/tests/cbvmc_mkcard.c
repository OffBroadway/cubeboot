/* Copyright (C) 2026 Nigzel / Bjerreman. SPDX-License-Identifier: GPL-2.0-only */
/* Makes a formatted, empty card image file with the card generator (the golden image of the host tests): cbvmc_mkcard OUT.raw [mbits] [jpn] */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "cbvmc_cardgen.h"
#include "cbvmc_ramstore.h"

int main(int argc, char **argv)
{
    struct cubeboot_vmc_identity id = { 0x4D53484E454C4954ull, 0x00000001u };
    struct cubeboot_vmc_geometry geom;
    struct cbvmc_ramstore rs; struct cubeboot_vmc_store st;
    uint8_t *image; uint32_t bytes; FILE *f;
    if (argc < 2) { fprintf(stderr, "usage: cbvmc_mkcard OUT.raw [mbits=4] [jpn]\n"); return 2; }
    geom.mbits = argc > 2 ? (uint32_t)atoi(argv[2]) : 4u;
    geom.encoding = (argc > 3 && strcmp(argv[3], "jpn") == 0) ? CUBEBOOT_VMC_ENC_SHIFT_JIS : CUBEBOOT_VMC_ENC_WINDOWS_1252;
    bytes = geom.mbits * 131072u;
    image = malloc(bytes); if (!image) return 1;
    memset(image, 0, bytes);
    if (cbvmc_ramstore_init(&rs, &st, image, bytes, "/card") != 0) return 1;
    if (cbvmc_cardgen(&st, "/card", 0, &id, &geom, 0, 0, 0) != CBVMC_GEN_CREATED) { fprintf(stderr, "cardgen failed\n"); return 1; }
    f = fopen(argv[1], "wb"); if (!f) return 1;
    if (fwrite(image, 1, bytes, f) != bytes) return 1;
    fclose(f);
    printf("%s: %u Mbit card, %s\n", argv[1], (unsigned)geom.mbits, geom.encoding == CUBEBOOT_VMC_ENC_SHIFT_JIS ? "Shift-JIS" : "Windows-1252");
    return 0;
}
