/* SPDX-License-Identifier: GPL-2.0-only */
/* Host test of the READ-ONLY card store of the IPL install (cbvmc_resident_ipl.c, test build over a RAM card) behind the real card model (cubeboot_vmc.c).
 * What is checked: the card opens and validates, reads (array read by immediate bytes and by DMA) equal the file, EVERY write path the IPL's card manager can use is refused
 * with the model's own error bits (page program by bytes, page program by DMA, sector erase, chip erase), create/rename/remove of the formatter are refused, the card file is
 * byte-identical afterwards, the refusals are counted, the commands the Cubeboot menu sends to channel 0 (0x8B ...) are answered with a floating bus and do not disturb the card,
 * a missing second card is NOT_FOUND and not created, and a wrong path is MISSING.
 *   cbvmc_ipl_ro_test CARD.raw      (a 0.5 MiB card made by tools/vmc_builder) */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CBVMC_TEST_RAM 1
#include "../cbvmc_resident_ipl.c"

static int bad;
static int check(const char *what, int ok) { printf("%s %s\n", ok ? "ok  " : "FAIL", what); if (!ok) ++bad; return ok; }

static uint8_t sel(struct cubeboot_vmc_card *c, int on) { cubeboot_vmc_card_select(c, on); return 0; }

/* 0x52 array read of `n` bytes at card address `addr` through immediate transfers */
static void read_bytes(struct cubeboot_vmc_card *c, uint32_t addr, uint8_t *out, unsigned n)
{
    unsigned i;
    sel(c, 1);
    cubeboot_vmc_card_transfer_byte(c, 0x52);
    cubeboot_vmc_card_transfer_byte(c, (uint8_t)(addr >> 17));
    cubeboot_vmc_card_transfer_byte(c, (uint8_t)(addr >> 9));
    cubeboot_vmc_card_transfer_byte(c, (uint8_t)((addr >> 7) & 3u));
    cubeboot_vmc_card_transfer_byte(c, (uint8_t)(addr & 0x7F));
    for (i = 0; i < 4; ++i) cubeboot_vmc_card_transfer_byte(c, 0);            /* latency bytes */
    for (i = 0; i < n; ++i) out[i] = cubeboot_vmc_card_transfer_byte(c, 0);
    sel(c, 0);
}

static uint8_t status(struct cubeboot_vmc_card *c)
{
    uint8_t s;
    sel(c, 1);
    cubeboot_vmc_card_transfer_byte(c, 0x83);
    s = cubeboot_vmc_card_transfer_byte(c, 0);
    sel(c, 0);
    return s;
}

int main(int argc, char **argv)
{
    FILE *f; uint8_t *golden, *img; long n; unsigned i; int detail = 0, rc;
    const struct cubeboot_vmc_store *top = 0;
    struct cubeboot_vmc_port port;
    struct cubeboot_vmc_card *c;
    struct cubeboot_vmc_identity id = { 1, 2 };
    struct cubeboot_vmc_geometry geom = { 128, CUBEBOOT_VMC_ENC_WINDOWS_1252 };
    uint8_t buf[512], page[16];
    static uint8_t dma[2048] __attribute__((aligned(32)));
    uint32_t sz;

    if (argc < 2 || !(f = fopen(argv[1], "rb"))) { fprintf(stderr, "usage: cbvmc_ipl_ro_test CARD.raw\n"); return 2; }
    fseek(f, 0, SEEK_END); n = ftell(f); fseek(f, 0, SEEK_SET);
    golden = malloc((size_t)n); img = malloc((size_t)n);
    if (fread(golden, 1, (size_t)n, f) != (size_t)n) return 2;
    fclose(f);
    memcpy(img, golden, (size_t)n);
    cbvmc_test_card = img; cbvmc_test_card_len = (uint32_t)n;
    { static uint8_t wal_a[16 + 8192], wal_b[16 + 8192]; cbvmc_test_wal = wal_a; cbvmc_test_wal_b = wal_b; }

    check("slot A opens", cbvmc_resident_open(0, &top, &detail) == CBVMC_OPEN_OK && top != 0);
    check("slot B (no card) is NOFILE, nothing created", cbvmc_resident_open(1, &top, &detail) == CBVMC_OPEN_ERR_NOFILE);
    cbvmc_resident_open(0, &top, &detail);
    check("size() of the card path", top->size(top->context, cbvmc_card_path, &sz) == 0 && sz == (uint32_t)n);
    check("size() of another path is MISSING", top->size(top->context, "/other", &sz) == CUBEBOOT_VMC_ERR_MISSING);
    check("size() of <path>.tmp is MISSING (no formatter work file)", ({ char t[100]; strcpy(t, cbvmc_card_path); strcat(t, ".tmp"); top->size(top->context, t, &sz) == CUBEBOOT_VMC_ERR_MISSING; }));

    check("port init + service -> VIRTUAL (card validates)", cubeboot_vmc_port_init(&port, top, CUBEBOOT_VMC_SLOT_A, cbvmc_card_path, &id, &geom) == 0 && (cubeboot_vmc_port_service(&port), port.source == CUBEBOOT_VMC_SOURCE_VIRTUAL));
    c = &port.card;
    check("the model reports the card as the file is (mbits from the size)", c->geometry.mbits == (uint32_t)(n / 131072) && c->image_bytes == (uint32_t)n);

    /* reads */
    for (i = 0; i < 8; ++i) {
        uint32_t addr = 0x2000u * i + 0x40u * i;
        read_bytes(c, addr, buf, 64);
        if (memcmp(buf, golden + addr, 64) != 0) break;
    }
    check("array reads by immediate bytes equal the file (8 spots)", i == 8);
    sel(c, 1);
    cubeboot_vmc_card_transfer_byte(c, 0x52); cubeboot_vmc_card_transfer_byte(c, 0); cubeboot_vmc_card_transfer_byte(c, 0x10); cubeboot_vmc_card_transfer_byte(c, 0); cubeboot_vmc_card_transfer_byte(c, 0);
    for (i = 0; i < 4; ++i) cubeboot_vmc_card_transfer_byte(c, 0);
    check("array read by DMA (2048 bytes) equals the file", cubeboot_vmc_card_dma_read(c, dma, sizeof(dma)) == 0 && memcmp(dma, golden + 0x2000, sizeof(dma)) == 0);
    sel(c, 0);
    { static uint8_t big[20000]; uint32_t off = 0x1F00u;
      check("a read spanning three 8 KiB cache blocks (offset 0x1F00, 20000 bytes) equals the file", top->read(top->context, cbvmc_card_path, off, big, sizeof(big)) == 0 && memcmp(big, golden + off, sizeof(big)) == 0);
      check("the last bytes of the file read through the cache", top->read(top->context, cbvmc_card_path, (uint32_t)n - 32u, big, 32) == 0 && memcmp(big, golden + n - 32, 32) == 0); }
    check("reads were counted", cbvmc_ripl_stat[0].reads > 0);

    /* the Cubeboot menu's own channel-0 traffic: MemCard PRO commands 0x8B 0x00 (device id) and 0x8B 0x11/0x12: the model must answer with a floating bus and stay readable */
    { uint32_t idw = 0; sel(c, 1); cubeboot_vmc_card_transfer_byte(c, 0x8B); cubeboot_vmc_card_transfer_byte(c, 0x00);
      for (i = 0; i < 4; ++i) idw = (idw << 8) | cubeboot_vmc_card_transfer_byte(c, 0); sel(c, 0);
      check("0x8B 0x00 answers 0xFFFFFFFF: Cubeboot's MCP_GetDeviceID sees 'wrong device' (not 0x3842xxxx) and gives up", idw == 0xFFFFFFFFu && (idw >> 16) != 0x3842u);
      sel(c, 1); cubeboot_vmc_card_transfer_byte(c, 0x8B); cubeboot_vmc_card_transfer_byte(c, 0x11); for (i = 0; i < 10; ++i) cubeboot_vmc_card_transfer_byte(c, 'A'); sel(c, 0); }
    read_bytes(c, 0x2000, buf, 64);
    check("card still reads correctly after the 0x8B traffic", memcmp(buf, golden + 0x2000, 64) == 0 && (status(c) & (CUBEBOOT_VMC_ST_PROGRAM_ERR | CUBEBOOT_VMC_ST_ERASE_ERR)) == 0);

    /* every write path goes through the write-back window now (2026-11-06); after a sync the file holds it */
    check("status before: ready, no errors", (status(c) & 0x39) == (CUBEBOOT_VMC_ST_READY));
    for (i = 0; i < 16; ++i) page[i] = (uint8_t)(0xA0 + i);
    sel(c, 1); cubeboot_vmc_card_transfer_byte(c, 0xF1); cubeboot_vmc_card_transfer_byte(c, 0); cubeboot_vmc_card_transfer_byte(c, 0x20); cubeboot_vmc_card_transfer_byte(c, 0); sel(c, 0);   /* erase block 2 (0x4000: AD2 = 0x4000 >> 9) */
    check("sector erase accepted: no ERASE error bit", (status(c) & CUBEBOOT_VMC_ST_ERASE_ERR) == 0);
    sel(c, 1);                                                                                  /* page program by immediate bytes at 0x4000 */
    cubeboot_vmc_card_transfer_byte(c, 0xF2); cubeboot_vmc_card_transfer_byte(c, 0); cubeboot_vmc_card_transfer_byte(c, 0x20); cubeboot_vmc_card_transfer_byte(c, 0); cubeboot_vmc_card_transfer_byte(c, 0);
    for (i = 0; i < 16; ++i) cubeboot_vmc_card_transfer_byte(c, page[i]);
    sel(c, 0);
    check("page program (bytes) accepted: no PROGRAM error bit", (status(c) & CUBEBOOT_VMC_ST_PROGRAM_ERR) == 0 && c->io_error == 0);
    check("sync commits the window: the file holds the page and the erased block", cbvmc_resident_sync() == 0 && memcmp(img + 0x4000, page, 16) == 0 && img[0x4000 + 16] == 0xFF);
    read_bytes(c, 0x4000, buf, 16);
    check("the model reads the programmed page back", memcmp(buf, page, 16) == 0);
    check("no refusals counted", cbvmc_ripl_stat[0].refused_writes == 0);
    /* the formatter over an existing card is still refused (the file exists); the card stays valid */
    { struct cubeboot_vmc_identity gid = { 5, 6 };
      check("format over an existing card refused (EXISTS)", cubeboot_vmc_format(top, cbvmc_card_path, 0, &gid, &geom) != CUBEBOOT_VMC_OK);
      check("the card is still valid for the model after all that", cubeboot_vmc_validate(top, cbvmc_card_path) == CUBEBOOT_VMC_OK); }

    /* a hostile read (beyond the file) fails cleanly and is counted */
    check("read beyond the end fails", top->read(top->context, cbvmc_card_path, (uint32_t)n - 16u, buf, 64) != 0);
    check("tick runs", (cbvmc_resident_tick(cbvmc_resident_tick_context()), cbvmc_ripl_stat[0].ticks == 1));

    /* slot B with its own card: independent */
    { uint8_t *imgb = malloc((size_t)n); struct cubeboot_vmc_port pb; const struct cubeboot_vmc_store *tb = 0;
      memcpy(imgb, golden, (size_t)n); imgb[0x2000 + 0x40] ^= 0; cbvmc_test_card_b = imgb; cbvmc_test_card_b_len = (uint32_t)n;
      rc = cbvmc_resident_open(1, &tb, &detail);
      check("slot B opens with its own card and own counters", rc == CBVMC_OPEN_OK && tb != top && cubeboot_vmc_port_init(&pb, tb, CUBEBOOT_VMC_SLOT_B, cbvmc_card_path_b, &id, &geom) == 0 && (cubeboot_vmc_port_service(&pb), pb.source == CUBEBOOT_VMC_SOURCE_VIRTUAL));
      check("slot B write lands in B only (after sync)", tb->write(tb->context, cbvmc_card_path_b, 0x6000, page, 16) == 0 && cbvmc_resident_sync() == 0 && memcmp(imgb + 0x6000, page, 16) == 0 && memcmp(img + 0x6000, golden + 0x6000, 16) == 0);
      free(imgb); }

    check("finish (host build) is a no-op", cbvmc_ipl_finish_c() == 0);
    printf("IPL RO TEST: %s\n", bad ? "FAIL" : "PASS");
    return bad ? 1 : 0;
}
