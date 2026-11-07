/* Copyright (C) 2026 Nigzel / Bjerreman. SPDX-License-Identifier: GPL-2.0-only */
/*
 * Host test of the card generator (cbvmc_cardgen.c): no file, file present, garbage file present, read-only SD, full SD, interrupted creation,
 * and the result validated (here by the model's validator and, from run_tests.sh, by tools/vmc_builder/vmc_validate.py on the dumped file).
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "cbvmc_cardgen.h"
#include "cbvmc_ramstore.h"

#define CHECK(c) do { if (!(c)) { fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #c); exit(1); } } while (0)
#define CARD "/cubeboot/saves/MemoryCardA.USA.raw"
#define IMG (16u * 1024u * 1024u)

static uint8_t *image;
static struct cbvmc_ramstore rs;
static struct cubeboot_vmc_store base, inj;
static int fail_create, fail_write_after, writes_seen, progress_calls;
static uint32_t last_done;

static int i_size(void *c, const char *p, uint32_t *b) { return base.size(base.context, p, b); }
static int i_read(void *c, const char *p, uint32_t o, void *b, uint32_t l) { return base.read(base.context, p, o, b, l); }
static int i_write(void *c, const char *p, uint32_t o, const void *b, uint32_t l)
{
    if (fail_write_after >= 0 && writes_seen++ >= fail_write_after) return -1;           /* the SD is full */
    return base.write(base.context, p, o, b, l);
}
static int i_create(void *c, const char *p) { return fail_create ? -1 : base.create(base.context, p); }     /* read-only SD */
static int i_rename(void *c, const char *a, const char *b) { return base.rename(base.context, a, b); }
static int i_remove(void *c, const char *p) { return base.remove(base.context, p); }
static int i_flush(void *c, const char *p) { return 0; }

static void progress(void *ctx, uint32_t done, uint32_t total) { (void)ctx; (void)total; progress_calls++; last_done = done; }

static void fresh(void)
{
    memset(image, 0x5A, IMG);
    CHECK(cbvmc_ramstore_init(&rs, &base, image, IMG, CARD) == 0);
    inj.context = 0; inj.size = i_size; inj.read = i_read; inj.write = i_write; inj.create = i_create; inj.rename = i_rename; inj.remove = i_remove; inj.flush = i_flush;
    fail_create = 0; fail_write_after = -1; writes_seen = 0; progress_calls = 0; last_done = 0;
}

int main(int argc, char **argv)
{
    struct cubeboot_vmc_identity id = { 0x1122334455667788ull, 0x99AABBCCDDEEFF00ull };
    struct cbvmc_gen_stats st;
    uint32_t sz;
    image = malloc(IMG);
    CHECK(image);

    /* 1. no file: generated, complete, valid, 16 MiB */
    fresh();
    CHECK(cbvmc_cardgen(&inj, CARD, 0, &id, 0, progress, 0, &st) == CBVMC_GEN_CREATED);
    CHECK(base.size(base.context, CARD, &sz) == 0 && sz == IMG);
    CHECK(st.bytes == IMG && st.writes == IMG / 8192u);                                   /* 2048 block writes of 8 KiB: on the console about 4.6 s (probe run 2) */
    CHECK(progress_calls == 16 && last_done == IMG);                                      /* a progress line per MiB */
    CHECK(base.size(base.context, CARD ".tmp", &sz) != 0);                                /* no stale tmp */
    CHECK(cubeboot_vmc_validate(&base, CARD) == CUBEBOOT_VMC_OK);
    if (argc > 1) { FILE *f = fopen(argv[1], "wb"); CHECK(f && fwrite(image, 1, IMG, f) == IMG); fclose(f); }
    /* 2. file present: untouched (byte for byte) */
    { static uint8_t keep[64]; memcpy(keep, image + 0x2000, sizeof(keep)); image[0x2000] ^= 0xFF; image[0x2000] ^= 0xFF;
      CHECK(cbvmc_cardgen(&inj, CARD, 0, &id, 0, 0, 0, &st) == CBVMC_GEN_EXISTS && st.writes == 0 && memcmp(keep, image + 0x2000, sizeof(keep)) == 0); }

    /* 3. a garbage file is present (a short, broken final file): never overwritten, and the card open refuses it */
    fresh();
    rs.exists[0] = 1; rs.size[0] = 100; memset(image, 0x77, 100);
    CHECK(cbvmc_cardgen(&inj, CARD, 0, &id, 0, 0, 0, &st) == CBVMC_GEN_EXISTS && st.writes == 0);
    CHECK(image[0] == 0x77 && rs.size[0] == 100);
    CHECK(cubeboot_vmc_validate(&base, CARD) != CUBEBOOT_VMC_OK);                         /* so the slot is refused by the installer, not served */

    /* 4. a relative path is refused */
    fresh();
    CHECK(cbvmc_cardgen(&inj, "relative.raw", 0, &id, 0, 0, 0, &st) == CBVMC_GEN_ERR_PATH);

    /* 5. read-only SD: create fails, nothing exists afterwards */
    fresh(); fail_create = 1;
    CHECK(cbvmc_cardgen(&inj, CARD, 0, &id, 0, 0, 0, &st) < 0 && st.writes == 0 && !rs.exists[0] && !rs.exists[1]);

    /* 6. full SD: a write fails half way; no final file, the partial tmp is removed */
    fresh(); fail_write_after = 1000;
    CHECK(cbvmc_cardgen(&inj, CARD, 0, &id, 0, 0, 0, &st) < 0 && st.writes == 999);      /* the formatter's probe write past the end (refused by the RAM store) is the first of the 1000 attempts */
    CHECK(!rs.exists[0] && !rs.exists[1]);
    fail_write_after = -1; writes_seen = 0;
    CHECK(cbvmc_cardgen(&inj, CARD, 0, &id, 0, 0, 0, &st) == CBVMC_GEN_CREATED);          /* space came back: the next boot generates it */
    CHECK(cubeboot_vmc_validate(&base, CARD) == CUBEBOOT_VMC_OK);
    fresh(); fail_write_after = 3;                                                        /* ... also failing in the very first blocks */
    CHECK(cbvmc_cardgen(&inj, CARD, 0, &id, 0, 0, 0, &st) < 0 && !rs.exists[0] && !rs.exists[1]);

    /* 7. interrupted creation (power cut): a partial .tmp is left, the final file does not exist; the next boot removes the tmp and generates */
    fresh();
    CHECK(base.create(base.context, CARD ".tmp") == 0);
    CHECK(base.write(base.context, CARD ".tmp", 0, image, 8192) == 0);                    /* some blocks, then the lights go out */
    CHECK(base.size(base.context, CARD, &sz) != 0);                                       /* never served: the final name is absent */
    CHECK(cbvmc_cardgen(&inj, CARD, 1, &id, 0, 0, 0, &st) == CBVMC_GEN_CREATED);
    CHECK(base.size(base.context, CARD, &sz) == 0 && sz == IMG && cubeboot_vmc_validate(&base, CARD) == CUBEBOOT_VMC_OK);
    CHECK(base.size(base.context, CARD ".tmp", &sz) != 0);

    /* 8. the packed geometry word (config.ini vmc_blocks / vmc_region -> resident image): round trip, every valid size, and everything else = default */
    {
        struct cubeboot_vmc_geometry g;
        static const unsigned ok[] = { 4, 8, 16, 32, 64, 128 };
        unsigned i;
        for (i = 0; i < 6; ++i) {
            CHECK(cbvmc_geom_unpack(cbvmc_geom_pack(ok[i], 1), &g) == 0 && g.mbits == ok[i] && g.encoding == CUBEBOOT_VMC_ENC_SHIFT_JIS);
            CHECK(cbvmc_geom_unpack(cbvmc_geom_pack(ok[i], 0), &g) == 0 && g.mbits == ok[i] && g.encoding == CUBEBOOT_VMC_ENC_WINDOWS_1252);
        }
        CHECK(cbvmc_geom_unpack(0, &g) == -1 && g.mbits == 128 && g.encoding == CUBEBOOT_VMC_ENC_WINDOWS_1252);
        CHECK(cbvmc_geom_unpack(cbvmc_geom_pack(12, 0), &g) == -1 && g.mbits == 128);              /* not a card size */
        CHECK(cbvmc_geom_unpack(cbvmc_geom_pack(64, 2), &g) == -1 && g.mbits == 128);              /* not an encoding */
        CHECK(cbvmc_geom_unpack(cbvmc_geom_pack(64, 1) & 0x00FFFFFFu, &g) == -1);                  /* no marker: stale memory, not a setting */
        /* and the generator honours it: a 251-block Shift-JIS card */
        g.mbits = 16; g.encoding = CUBEBOOT_VMC_ENC_SHIFT_JIS;
        fresh();
        CHECK(cbvmc_cardgen(&inj, CARD, 0, &id, &g, 0, 0, &st) == CBVMC_GEN_CREATED);
        CHECK(base.size(base.context, CARD, &sz) == 0 && sz == 16u * 16u * 8192u && cubeboot_vmc_validate(&base, CARD) == CUBEBOOT_VMC_OK);
        {
            uint8_t hdr[0x26];
            CHECK(base.read(base.context, CARD, 0, hdr, sizeof hdr) == 0 && hdr[0x22] == 0 && hdr[0x23] == 16 && hdr[0x24] == 0 && hdr[0x25] == 1);
        }
    }

    printf("cbvmc_cardgen_test: all passed\n");
    free(image);
    return 0;
}
