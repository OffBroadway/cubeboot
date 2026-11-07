/* SPDX-License-Identifier: GPL-2.0-only */
/* Host test of the Memory Card page popup extension's card-image part (cbvmc_ipl_menu.c) over the IPL resident store (RAM card):
   snapshot -> import of a .gci (entry, chained blocks, block map, checksums validated by the card model) -> the file moved -> a second import is
   a duplicate and skipped -> undo restores the five system blocks (free blocks and directory as before) -> a .gci that does not fit is skipped.
     cbvmc_ipl_menu_test CARD.raw TMPDIR */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <dirent.h>
#include <sys/stat.h>

#define CBVMC_TEST_RAM 1
#include "../cbvmc_resident_ipl.c"
#include "../cbvmc_ipl_menu.h"

static int bad;
static int check(const char *what, int ok) { printf("%s %s\n", ok ? "ok  " : "FAIL", what); if (!ok) ++bad; return ok; }

/* ---- a file store over a host directory (paths are absolute card paths under TMPDIR) ---- */
static char root[1024];
static void hp(char *out, const char *p) { snprintf(out, 1024, "%s%s", root, p); }
static int h_size(void *c, const char *p, uint32_t *b) { char q[1024]; struct stat st; (void)c; hp(q, p); if (stat(q, &st) != 0) return CUBEBOOT_VMC_ERR_MISSING; *b = (uint32_t)st.st_size; return 0; }
static int h_read(void *c, const char *p, uint32_t o, void *b, uint32_t l) { char q[1024]; FILE *f; size_t n; (void)c; hp(q, p); if (!(f = fopen(q, "rb"))) return -1; fseek(f, (long)o, SEEK_SET); n = fread(b, 1, l, f); fclose(f); return n == l ? 0 : -1; }
static int h_write(void *c, const char *p, uint32_t o, const void *b, uint32_t l) { char q[1024]; FILE *f; size_t n; (void)c; hp(q, p); if (!(f = fopen(q, "r+b"))) return -1; fseek(f, (long)o, SEEK_SET); n = fwrite(b, 1, l, f); fclose(f); return n == l ? 0 : -1; }
static int h_create(void *c, const char *p) { char q[1024]; FILE *f; (void)c; hp(q, p); if ((f = fopen(q, "rb"))) { fclose(f); return CUBEBOOT_VMC_ERR_EXISTS; } if (!(f = fopen(q, "wb"))) return -1; fclose(f); return 0; }
static int h_rename(void *c, const char *a, const char *b) { char qa[1024], qb[1024]; (void)c; hp(qa, a); hp(qb, b); return rename(qa, qb) == 0 ? 0 : -1; }
static int h_remove(void *c, const char *p) { char q[1024]; (void)c; hp(q, p); return remove(q) == 0 ? 0 : -1; }
static int h_flush(void *c, const char *p) { (void)c; (void)p; return 0; }
static int h_list(void *c, const char *dir, int (*cb)(void *cbctx, const char *name), void *cbctx)
{
    char q[1024]; DIR *d; struct dirent *e; int n = 0; (void)c;
    hp(q, dir);
    if (!(d = opendir(q))) return -1;
    while ((e = readdir(d))) { if (e->d_name[0] == '.') continue; ++n; if (cb(cbctx, e->d_name) != 0) break; }
    closedir(d);
    return n;
}
static int h_mkdir(void *c, const char *p) { char q[1024]; (void)c; hp(q, p); return mkdir(q, 0777) == 0 || 1 ? 0 : -1; }

static void make_gci(const char *name, const char *game, const char *fname, unsigned blocks, unsigned seed)
{
    char q[1024]; FILE *f; unsigned i; uint8_t ent[64]; uint8_t *data = malloc(blocks * 8192u);
    snprintf(q, sizeof(q), "%s" CBVMC_MENU_IMPORT_DIR "/%s", root, name);
    memset(ent, 0, sizeof(ent));
    memcpy(ent, game, 4); memcpy(ent + 4, "01", 2); ent[6] = 0xff; ent[7] = 2;
    strncpy((char *)ent + 8, fname, 32);
    ent[0x34] = 4; ent[0x35] = 0; ent[0x36] = 0xff; ent[0x37] = 0xff; ent[0x38] = (uint8_t)(blocks >> 8); ent[0x39] = (uint8_t)blocks; ent[0x3a] = 0xff; ent[0x3b] = 0xff;
    for (i = 0; i < blocks * 8192u; ++i) data[i] = (uint8_t)(seed * 31u + i * 7u + (i >> 10));
    f = fopen(q, "wb"); fwrite(ent, 1, 64, f); fwrite(data, 1, blocks * 8192u, f); fclose(f);
    free(data);
}
static int find_entry(const struct cubeboot_vmc_store *top, const char *path, const char *fname, uint8_t *out)
{
    uint8_t dir[8192]; unsigned blk, e; uint16_t best = 0; int found = 0;
    for (blk = 1; blk <= 2; ++blk) {
        uint16_t ctr;
        if (top->read(top->context, path, blk * 8192u, dir, 8192) != 0) return -1;
        ctr = (uint16_t)((dir[0x1ffa] << 8) | dir[0x1ffb]);
        if (blk == 2 && (int16_t)(ctr - best) <= 0) continue;
        best = ctr; found = 0;
        for (e = 0; e < 127; ++e) if (dir[e * 64] != 0xff && strncmp((const char *)dir + e * 64 + 8, fname, 32) == 0) { memcpy(out, dir + e * 64, 64); found = 1; }
    }
    return found;
}

int main(int argc, char **argv)
{
    FILE *f; uint8_t *img; long n; int detail = 0, free0, free1;
    const struct cubeboot_vmc_store *top = 0;
    struct cbvmc_menu_fs fs; struct cbvmc_menu_card cd; struct cubeboot_vmc_store hs;
    char q[1024]; unsigned done = 0, skipped = 0; uint32_t sz; uint8_t ent[64], blk[8192], want[8192]; unsigned i, first;

    if (argc < 3 || !(f = fopen(argv[1], "rb"))) { fprintf(stderr, "usage: cbvmc_ipl_menu_test CARD.raw TMPDIR\n"); return 2; }
    fseek(f, 0, SEEK_END); n = ftell(f); fseek(f, 0, SEEK_SET);
    img = malloc((size_t)n); if (fread(img, 1, (size_t)n, f) != (size_t)n) return 2; fclose(f);
    snprintf(root, sizeof(root), "%s", argv[2]);
    snprintf(q, sizeof(q), "%s/cubeboot", root); mkdir(q, 0777); snprintf(q, sizeof(q), "%s/cubeboot/saves", root); mkdir(q, 0777);
    snprintf(q, sizeof(q), "%s" CBVMC_MENU_IMPORT_DIR, root); mkdir(q, 0777);

    cbvmc_test_card = img; cbvmc_test_card_len = (uint32_t)n;
    { static uint8_t wal_a[16 + 8192], wal_b[16 + 8192]; cbvmc_test_wal = wal_a; cbvmc_test_wal_b = wal_b; }
    check("slot A opens", cbvmc_resident_open(0, &top, &detail) == CBVMC_OPEN_OK && top != 0);
    check("journal prepared", cbvmc_resident_prepare(0) == 0);

    memset(&hs, 0, sizeof(hs)); hs.size = h_size; hs.read = h_read; hs.write = h_write; hs.create = h_create; hs.rename = h_rename; hs.remove = h_remove; hs.flush = h_flush;
    memset(&fs, 0, sizeof(fs)); fs.list = h_list; fs.mkdir = h_mkdir; fs.files = &hs;
    cbvmc_menu_bind_fs(&fs);
    memset(&cd, 0, sizeof(cd)); cd.store = top; cd.path = cbvmc_card_path; cd.model = 0; cd.is_virtual = 1;
    cbvmc_menu_bind_card(0, &cd);

    free0 = cubeboot_vmc_free_blocks(top, cbvmc_card_path);
    check("card validates before", cubeboot_vmc_validate(top, cbvmc_card_path) == 0 && free0 > 3);
    check("no undo yet", cbvmc_menu_undo_available(0) == 0);
    check("snapshot", cbvmc_menu_snapshot(0) == 0 && cbvmc_menu_undo_available(0) == 1);
    snprintf(q, sizeof(q), "%s.undo", cbvmc_card_path);
    check("undo file is the five system blocks", hs.size(0, q, &sz) == 0 && sz == 5u * 8192u);

    make_gci("TestSave.gci", "GALE", "TestSave", 2, 5);
    make_gci("Other.GCI", "GTEE", "OtherSave", 1, 9);
    make_gci("notes.txt", "GXXE", "NotAGci", 1, 1);
    check("import: two .gci done, the .txt ignored", cbvmc_menu_import(0, &done, &skipped) == 0 && done == 2 && skipped == 0);
    check("card validates after the import", cubeboot_vmc_validate(top, cbvmc_card_path) == 0);
    free1 = cubeboot_vmc_free_blocks(top, cbvmc_card_path);
    check("three blocks fewer free", free1 == free0 - 3);
    check("TestSave entry present with 2 blocks", find_entry(top, cbvmc_card_path, "TestSave", ent) == 1 && ent[0x39] == 2 && memcmp(ent, "GALE01", 6) == 0);
    first = (unsigned)((ent[0x36] << 8) | ent[0x37]);
    check("first block is a data block", first >= 5 && first < (unsigned)(n / 8192));
    for (i = 0; i < 8192; ++i) want[i] = (uint8_t)(5u * 31u + i * 7u + (i >> 10));
    check("first data block holds the file's first block", top->read(top->context, cbvmc_card_path, first * 8192u, blk, 8192) == 0 && memcmp(blk, want, 8192) == 0);
    { uint8_t bat[8192]; unsigned nxt; uint16_t ca, cb;
      top->read(top->context, cbvmc_card_path, 3 * 8192u, bat, 8192); ca = (uint16_t)((bat[4] << 8) | bat[5]);
      top->read(top->context, cbvmc_card_path, 4 * 8192u, bat, 8192); cb = (uint16_t)((bat[4] << 8) | bat[5]);
      if ((int16_t)(ca - cb) > 0) top->read(top->context, cbvmc_card_path, 3 * 8192u, bat, 8192);
      nxt = (unsigned)((bat[10 + 2 * (first - 5)] << 8) | bat[11 + 2 * (first - 5)]);
      check("the chain: first -> second -> end", nxt >= 5 && nxt != 0xffff && ((bat[10 + 2 * (nxt - 5)] << 8) | bat[11 + 2 * (nxt - 5)]) == 0xffff);
      for (i = 0; i < 8192; ++i) want[i] = (uint8_t)(5u * 31u + (8192 + i) * 7u + ((8192 + i) >> 10));
      check("second data block holds the file's second block", top->read(top->context, cbvmc_card_path, nxt * 8192u, blk, 8192) == 0 && memcmp(blk, want, 8192) == 0); }
    snprintf(q, sizeof(q), CBVMC_MENU_IMPORTED_DIR "/TestSave.gci");
    check("the .gci moved to the imported folder", hs.size(0, q, &sz) == 0 && sz == 64 + 2 * 8192u && hs.size(0, CBVMC_MENU_IMPORT_DIR "/TestSave.gci", &sz) != 0);

    make_gci("TestSave.gci", "GALE", "TestSave", 2, 5);
    check("importing the same save again is skipped (duplicate)", cbvmc_menu_import(0, &done, &skipped) == 0 && done == 0 && skipped == 1);
    check("undo, then the same file imports again and replaces its older copy in imported/", cbvmc_menu_undo(0) == 0 && cbvmc_menu_snapshot(0) == 0 && cbvmc_menu_import(0, &done, &skipped) == 0 && done == 1 && hs.size(0, CBVMC_MENU_IMPORTED_DIR "/TestSave.gci", &sz) == 0 && hs.size(0, CBVMC_MENU_IMPORT_DIR "/TestSave.gci", &sz) != 0);
    make_gci("Other.GCI", "GTEE", "OtherSave", 1, 9);
    check("the second save again (the card is as after the first import)", cbvmc_menu_import(0, &done, &skipped) == 0 && done == 1 && cubeboot_vmc_free_blocks(top, cbvmc_card_path) == free1);
    make_gci("TestSave.gci", "GALE", "TestSave", 2, 5);
    check("duplicate again", cbvmc_menu_import(0, &done, &skipped) == 0 && done == 0 && skipped == 1);
    make_gci("Huge.gci", "GHUG", "Huge", (unsigned)(free1 + 1), 3);
    check("a .gci that does not fit is skipped", cbvmc_menu_import(0, &done, &skipped) == 0 && done == 0 && skipped == 2);
    check("card still validates", cubeboot_vmc_validate(top, cbvmc_card_path) == 0 && cubeboot_vmc_free_blocks(top, cbvmc_card_path) == free1);

    check("undo", cbvmc_menu_undo(0) == 0 && cbvmc_menu_undo_available(0) == 0);
    check("card validates after the undo, free blocks as before, the entry gone", cubeboot_vmc_validate(top, cbvmc_card_path) == 0 && cubeboot_vmc_free_blocks(top, cbvmc_card_path) == free0 && find_entry(top, cbvmc_card_path, "TestSave", ent) == 0);
    check("undo again is refused", cbvmc_menu_undo(0) == CBVMC_MENU_ERR_NOUNDO);
    check("sync", cbvmc_resident_sync() == 0);

    /* a full card image in the import folder: its saves come over one by one (the chain of the image's own block map), the image moves */
    {   const char *im = CBVMC_MENU_IMPORT_DIR "/Old.raw"; unsigned b, own = 0; uint8_t ent2[64];
        check("image: snapshot", cbvmc_menu_snapshot(0) == 0);
        make_gci("TestSave.gci", "GALE", "TestSave", 2, 5);
        make_gci("Other.GCI", "GTEE", "OtherSave", 1, 9);
        check("image: two saves on the card first", cbvmc_menu_import(0, &done, &skipped) == 0 && done == 2 && cbvmc_resident_sync() == 0);
        check("image: file made", hs.create(0, im) == 0);
        for (b = 0; b < (unsigned)(n / 8192); ++b) { if (top->read(top->context, cbvmc_card_path, b * 8192u, blk, 8192) != 0 || hs.write(0, im, b * 8192u, blk, 8192) != 0) break; }
        check("image: a copy of the card", b == (unsigned)(n / 8192));
        check("image: undo empties the card again", cbvmc_menu_undo(0) == 0 && find_entry(top, cbvmc_card_path, "TestSave", ent) == 0 && cubeboot_vmc_free_blocks(top, cbvmc_card_path) == free0);
        (void)hs.remove(0, CBVMC_MENU_IMPORT_DIR "/Huge.gci"); (void)hs.remove(0, CBVMC_MENU_IMPORT_DIR "/notes.txt");   /* leftovers of the folder tests */
        check("image: snapshot before", cbvmc_menu_snapshot(0) == 0);
        {   /* the image also carries whatever the golden card held before (already on the card: duplicates, skipped) */
            unsigned m = 0, e; uint8_t dir[8192];
            if (hs.read(0, im, 8192u, dir, 8192) == 0) for (e = 0; e < 127; ++e) if (dir[e * 64] != 0xff) ++m;
            check("image import: the two new saves done, the old ones skipped as duplicates", cbvmc_menu_import(0, &done, &skipped) == 0 && done == 2 && skipped == m - 2);
            own = m; }
        check("image import: card validates, three blocks used", cubeboot_vmc_validate(top, cbvmc_card_path) == 0 && cubeboot_vmc_free_blocks(top, cbvmc_card_path) == free0 - 3);
        check("image import: TestSave back with 2 blocks", find_entry(top, cbvmc_card_path, "TestSave", ent) == 1 && ent[0x39] == 2 && find_entry(top, cbvmc_card_path, "OtherSave", ent2) == 1 && ent2[0x39] == 1);
        first = (unsigned)((ent[0x36] << 8) | ent[0x37]);
        for (i = 0; i < 8192; ++i) want[i] = (uint8_t)(5u * 31u + i * 7u + (i >> 10));
        check("image import: first data block matches", top->read(top->context, cbvmc_card_path, first * 8192u, blk, 8192) == 0 && memcmp(blk, want, 8192) == 0);
        { uint8_t bat[8192]; unsigned nxt; uint16_t ca, cb;
          top->read(top->context, cbvmc_card_path, 3 * 8192u, bat, 8192); ca = (uint16_t)((bat[4] << 8) | bat[5]);
          top->read(top->context, cbvmc_card_path, 4 * 8192u, bat, 8192); cb = (uint16_t)((bat[4] << 8) | bat[5]);
          if ((int16_t)(ca - cb) > 0) top->read(top->context, cbvmc_card_path, 3 * 8192u, bat, 8192);
          nxt = (unsigned)((bat[10 + 2 * (first - 5)] << 8) | bat[11 + 2 * (first - 5)]);
          for (i = 0; i < 8192; ++i) want[i] = (uint8_t)(5u * 31u + (8192 + i) * 7u + ((8192 + i) >> 10));
          check("image import: second data block follows the chain", nxt >= 5 && nxt != 0xffff && top->read(top->context, cbvmc_card_path, nxt * 8192u, blk, 8192) == 0 && memcmp(blk, want, 8192) == 0); }
        check("image moved to the imported folder", hs.size(0, CBVMC_MENU_IMPORTED_DIR "/Old.raw", &sz) == 0 && hs.size(0, im, &sz) != 0);
        check("image: file made again", hs.rename(0, CBVMC_MENU_IMPORTED_DIR "/Old.raw", im) == 0);
        check("image import again: every save is a duplicate", cbvmc_menu_import(0, &done, &skipped) == 0 && done == 0 && skipped == own);
        check("image: undo", cbvmc_menu_undo(0) == 0 && cubeboot_vmc_free_blocks(top, cbvmc_card_path) == free0 && cbvmc_resident_sync() == 0);
    }

    /* Swap Slot: the two card files of the SD change place, their .undo snapshots too; a physical card in either slot refuses */
    { struct cbvmc_menu_card ca, cb; uint8_t wa[512], wb[512], rd[512]; const char *pa = "/cubeboot/saves/SwapA.raw", *pb = "/cubeboot/saves/SwapB.raw";
      for (i = 0; i < 512; ++i) { wa[i] = (uint8_t)(i * 3u); wb[i] = (uint8_t)(0xa5u - i); }
      check("swap files made", hs.create(0, pa) == 0 && hs.write(0, pa, 0, wa, 512) == 0 && hs.create(0, pb) == 0 && hs.write(0, pb, 0, wb, 512) == 0 && hs.create(0, "/cubeboot/saves/SwapA.raw.undo") == 0);
      memset(&ca, 0, sizeof(ca)); ca.store = &hs; ca.path = pa; ca.is_virtual = 1; cb = ca; cb.path = pb;
      cbvmc_menu_bind_card(0, &ca);
      check("swap not offered with one card bound", cbvmc_menu_swap_available() == 0);
      cbvmc_menu_bind_card(1, &cb);
      check("swap offered with two virtual cards", cbvmc_menu_swap_available() == 1);
      check("swap", cbvmc_menu_swap() == 0);
      check("A holds B's bytes", hs.read(0, pa, 0, rd, 512) == 0 && memcmp(rd, wb, 512) == 0);
      check("B holds A's bytes", hs.read(0, pb, 0, rd, 512) == 0 && memcmp(rd, wa, 512) == 0);
      check("the undo snapshot moved with its card", hs.size(0, "/cubeboot/saves/SwapB.raw.undo", &sz) == 0 && hs.size(0, "/cubeboot/saves/SwapA.raw.undo", &sz) != 0);
      check("no temporary file left", hs.size(0, "/cubeboot/saves/SwapA.raw.swap", &sz) != 0 && hs.size(0, "/cubeboot/saves/SwapA.raw.undo.swap", &sz) != 0);
      check("models refreshed (no port bound: nothing to do)", cbvmc_resident_cards_swapped() == 0);
      check("swap back", cbvmc_menu_swap() == 0 && hs.read(0, pa, 0, rd, 512) == 0 && memcmp(rd, wa, 512) == 0 && hs.size(0, "/cubeboot/saves/SwapA.raw.undo", &sz) == 0);
      cb.is_virtual = 0; cbvmc_menu_bind_card(1, &cb);
      check("a physical card in B refuses the swap", cbvmc_menu_swap_available() == 0 && cbvmc_menu_swap() == CBVMC_MENU_ERR_PHYSICAL);
      cbvmc_menu_bind_card(0, &cd); memset(&cb, 0, sizeof(cb)); cbvmc_menu_bind_card(1, &cb); }
    printf(bad ? "FAIL %d\n" : "PASS cbvmc_ipl_menu_test\n", bad);
    return bad ? 1 : 0;
}
