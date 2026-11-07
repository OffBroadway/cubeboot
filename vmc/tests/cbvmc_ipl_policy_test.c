/* SPDX-License-Identifier: GPL-2.0-only */
/* Host test of the G4 stage 1 install decision (cbvmc_ipl_policy.c) against the numbers the owner's console measured on 2026-10-10
 * (docs/data/ipl_probe_console_2026-10-10.txt: PAL 1.2-101, ipl_rev_id 11, MSR 0000B032, ibat0/dbat0 80001FFF/00000002, dbat1 C0001FFF/0000002A, the DSI stub, the
 * region 816ED000-81700000 all zero, patches_end 816477E0, main thread stack 815E156C..815EF570, pal12 symbol set 8/8 prologues).
 * There is no BS2 image here, so the BS2 text window is stood in for by a 1 MiB slice of the retail Melee 1.02 DOL (the slice 0x80320000.. moved to 0x81300000: the
 * scanner is position independent and finds the same shape: 25 words, EXIInit with one caller, VerifyID, DoMount).  The known-symbol set is built from Melee's own EXI/CARD
 * function addresses moved by the same delta.  What this proves: every branch of the decision, with the console's register values.  What it does not prove: that BS2's
 * real code gives the same scan result (the console probe says it does: strict scan ok, 25 words, the same single hook), nor anything about the hardware.
 *   cbvmc_ipl_policy_test RETAIL.dol   (written against Melee 1.02; any retail SDK game DOL of that era should do, nothing is shipped here) */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "cbvmc_ipl_policy.h"

static uint32_t be(const uint8_t *p) { return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) | ((uint32_t)p[2] << 8) | p[3]; }
static void put(uint8_t *ram, uint32_t a, uint32_t v) { uint8_t *p = ram + (a - 0x80000000u); p[0] = (uint8_t)(v >> 24); p[1] = (uint8_t)(v >> 16); p[2] = (uint8_t)(v >> 8); p[3] = (uint8_t)v; }
static uint32_t get(const uint8_t *ram, uint32_t a) { return be(ram + (a - 0x80000000u)); }

static int bad;
static int check(const char *what, int ok) { printf("%s %s\n", ok ? "ok  " : "FAIL", what); if (!ok) ++bad; return ok; }

#define DELTA 0x00FE0000u               /* Melee 0x80320000 -> 0x81300000 */

static struct cbvmc_ipl_symset g_sets[3];
static struct cbvmc_ipl_in base_in(const uint8_t *ram)
{
    struct cbvmc_ipl_in in;
    memset(&in, 0, sizeof(in));
    in.mem = ram; in.vmc_enabled = 1; in.ipl_rev = 11; in.msr = 0x0000B032u;
    in.bat[0] = 0x80001FFFu; in.bat[1] = 0x00000002u;              /* ibat0 */
    in.bat[8] = 0x80001FFFu; in.bat[9] = 0x00000002u;              /* dbat0 */
    in.bat[10] = 0xC0001FFFu; in.bat[11] = 0x0000002Au;            /* dbat1 */
    in.patches_end = 0x816477E0u;
    in.sp = 0x815EE800u; in.stack_end = 0x815E156Cu;
    in.allow = 2u;                                                  /* slot A holds a device on the owner's console (EXT bit set), B is free */
    in.sets = g_sets; in.nsets = 3; in.region_base = CBVMC_IPL_BASE;
    return in;
}

int main(int argc, char **argv)
{
    FILE *f; uint8_t *d, *ram, *copy; long n; unsigned i, lo = 0, k; static struct cbvmc_ipl_out out;
    struct cbvmc_ipl_in in;
    static const uint32_t melee_fn[8] = { 0x80345B64u, 0x80345B64u + 0x100u, 0x80345F4Cu, 0x80346688u, 0x803467B4u, 0x80346D80u, 0x80346E74u, 0x80355F94u };   /* EXIImm .. CARDProbeEx (symbols.txt) */
    if (argc < 2 || !(f = fopen(argv[1], "rb"))) { fprintf(stderr, "usage: cbvmc_ipl_policy_test MELEE.dol\n"); return 2; }
    fseek(f, 0, SEEK_END); n = ftell(f); fseek(f, 0, SEEK_SET);
    d = malloc((size_t)n); if (fread(d, 1, (size_t)n, f) != (size_t)n) return 2; fclose(f);
    ram = calloc(1, 0x1800000); copy = calloc(1, 0x1800000);
    /* the slice 0x80320000..0x80420000 of Melee, moved to 0x81300000 */
    for (i = 0; i < 18; ++i) {
        uint32_t off = be(d + 4 * i), addr = be(d + 0x48 + 4 * i), len = be(d + 0x90 + 4 * i), a;
        if (!off || !len) continue;
        for (a = addr; a < addr + len; a += 4) if (a >= 0x80320000u && a < 0x80420000u) put(ram, a + DELTA, be(d + off + (a - addr)));
    }
    (void)lo;
    /* the symbol sets: the "pal12" stand-in (rev 11) built from Melee's EXI/CARD function addresses, one set of another revision, one whose addresses hold no code */
    g_sets[0].name = "pal12"; g_sets[0].rev_mask = 1u << 11;
    g_sets[1].name = "ntsc12_101"; g_sets[1].rev_mask = 1u << 10;
    g_sets[2].name = "empty"; g_sets[2].rev_mask = 0;
    for (k = 0; k < 8; ++k) { g_sets[0].fn[k] = melee_fn[k] + DELTA; g_sets[1].fn[k] = melee_fn[k] + DELTA; g_sets[2].fn[k] = 0x81500000u + 0x40u * k; }
    for (k = 0; k < 8; ++k) {                                                    /* make every stand-in function start like a function */
        uint32_t w = get(ram, g_sets[0].fn[k]);
        if (w != 0x7C0802A6u && (w >> 16) != 0x9421u) put(ram, g_sets[0].fn[k], 0x7C0802A6u);
    }
    put(ram, 0x80000300u, 0x7C9043A6u); put(ram, 0x80000304u, 0x808000C0u);       /* the SDK stub's first words, as seen on the console */
    memcpy(copy, ram, 0x1800000);

    in = base_in(ram);
    check("decision with the console's numbers: OK", cbvmc_ipl_decide(&in, &out) == CBVMC_IPLR_OK);
    check("scan: 25 words, one hook, strict ok", out.scan.ok == 1 && out.scan.nwords == 25 && out.scan.hook_addr == 0x8034312Cu + DELTA);
    check("best symbol set = pal12 with 8/8 prologues", out.best_set == 0 && out.best_prologues == 8);
    check("memory untouched by the decision", memcmp(ram, copy, 0x1800000) == 0);
    in.allow = 3; check("both slots free: OK", cbvmc_ipl_decide(&in, &out) == CBVMC_IPLR_OK);

    /* switches */
    in = base_in(ram); in.vmc_enabled = 0; check("vmc = 0: refused", cbvmc_ipl_decide(&in, &out) == CBVMC_IPLR_VMC_OFF);
    in = base_in(ram); in.z_held = 1; check("skip button held: refused", cbvmc_ipl_decide(&in, &out) == CBVMC_IPLR_BUTTON);
    /* machine */
    in = base_in(ram); in.ipl_rev = 0; check("unknown revision: refused", cbvmc_ipl_decide(&in, &out) == CBVMC_IPLR_REV);
    in = base_in(ram); in.ipl_rev = 10; check("revision 10, but the best set (pal12) belongs to 11: refused as symrev", cbvmc_ipl_decide(&in, &out) == CBVMC_IPLR_SYMREV);
    in = base_in(ram); in.ipl_rev = 10; in.sets = g_sets + 1; in.nsets = 1; check("revision 10 with a matching set: OK", cbvmc_ipl_decide(&in, &out) == CBVMC_IPLR_OK);
    in = base_in(ram); in.msr = 0x0000B002u; check("translation off: refused", cbvmc_ipl_decide(&in, &out) == CBVMC_IPLR_MSR);
    in = base_in(ram); in.bat[9] = 0x00000022u; check("RAM mapped cache-inhibited: refused", cbvmc_ipl_decide(&in, &out) == CBVMC_IPLR_BAT && out.detail == 2);
    in = base_in(ram); in.bat[11] = 0x00000002u; check("I/O window cacheable: refused", cbvmc_ipl_decide(&in, &out) == CBVMC_IPLR_BAT && out.detail == 3);
    in = base_in(ram); in.bat[12] = 0x0C001FFFu; in.bat[13] = 0x0000002Au; check("a DBAT maps 0x0C000000 (the trap would not fault): refused", cbvmc_ipl_decide(&in, &out) == CBVMC_IPLR_BAT && out.detail == 4);
    in = base_in(ram); in.bat[0] = 0; check("no IBAT for the code: refused", cbvmc_ipl_decide(&in, &out) == CBVMC_IPLR_BAT && out.detail == 1);
    in = base_in(ram); in.bat[8] = 0x80001FFCu; check("invalid DBAT0 (Vs=Vp=0): refused", cbvmc_ipl_decide(&in, &out) == CBVMC_IPLR_BAT);
    put(ram, 0x80000300u, 0x48000002u | 0x1000u); in = base_in(ram); check("DSI vector already taken: refused", cbvmc_ipl_decide(&in, &out) == CBVMC_IPLR_VECTOR && out.detail == 0x48001002u);
    put(ram, 0x80000300u, 0x7C9043A6u);
    /* code */
    { uint32_t was = get(ram, 0x803551D0u + DELTA); put(ram, 0x803551D0u + DELTA, 0x60000000u); in = base_in(ram);
      check("VerifyID not found: scanner refuses, install refused with the scanner's code", cbvmc_ipl_decide(&in, &out) == CBVMC_IPLR_SCAN && out.detail == CBVMC_R_VERIFYID); put(ram, 0x803551D0u + DELTA, was); }
    in = base_in(ram); in.sets = g_sets + 2; in.nsets = 1; check("symbol set whose addresses hold no functions: refused", cbvmc_ipl_decide(&in, &out) == CBVMC_IPLR_SYMSET);
    { struct cbvmc_ipl_symset narrow = g_sets[0]; for (k = 0; k < 8; ++k) narrow.fn[k] = 0x81301000u + 0x40u * k;     /* a block far from the words */
      for (k = 0; k < 8; ++k) put(ram, narrow.fn[k], 0x7C0802A6u);
      in = base_in(ram); in.sets = &narrow; in.nsets = 1; check("words outside the EXI/CARD block: refused with the word index", cbvmc_ipl_decide(&in, &out) == CBVMC_IPLR_SITES);
      for (k = 0; k < 8; ++k) put(ram, narrow.fn[k], 0); }
    /* memory */
    in = base_in(ram); in.patches_end = 0x81650000u; check("stage-1 framebuffer reaching the region is allowed (dead memory under BS2)", cbvmc_ipl_decide(&in, &out) == CBVMC_IPLR_OK);
    in = base_in(ram); in.patches_end = CBVMC_IPL_BASE + 0x100u; check("the patch image itself reaching the region: refused", cbvmc_ipl_decide(&in, &out) == CBVMC_IPLR_REGION_XFB);
    in = base_in(ram); in.patches_end = 0x816477E0u; check("the G3 probe build's patches_end still fits (framebuffer ends 0x816ED000 < 0x816F0000)", cbvmc_ipl_decide(&in, &out) == CBVMC_IPLR_OK);
    put(ram, CBVMC_IPL_END - 4u, 0xFFFFFFFFu); in = base_in(ram); check("leftover data in the region is no refusal (the installer zeroes it)", cbvmc_ipl_decide(&in, &out) == CBVMC_IPLR_OK); put(ram, CBVMC_IPL_END - 4u, 0);
    in = base_in(ram); in.sp = in.stack_end + 0x6000u; check("only 24 KiB of stack: refused", cbvmc_ipl_decide(&in, &out) == CBVMC_IPLR_STACK);
    in = base_in(ram); in.stack_end = 0; check("unknown stack bounds: refused", cbvmc_ipl_decide(&in, &out) == CBVMC_IPLR_STACK);
    /* devices */
    in = base_in(ram); in.allow = 0; check("both slots physical: refused", cbvmc_ipl_decide(&in, &out) == CBVMC_IPLR_SLOTS);
    in = base_in(ram); in.exi_busy = 1; check("card channel busy: refused", cbvmc_ipl_decide(&in, &out) == CBVMC_IPLR_EXI_BUSY);
    /* hostile memory: nothing is written, no crash, always a clean refusal */
    {
        uint32_t x = 7u; unsigned j, round;
        for (round = 0; round < 3; ++round) {
            for (j = 0; j < 0x1800000u; ++j) { x = x * 1664525u + 1013904223u; ram[j] = round == 0 ? 0 : round == 1 ? 0xFF : (uint8_t)(x >> 24); }
            memcpy(copy, ram, 0x1800000); in = base_in(ram);
            check(round == 0 ? "all-zero memory: refused cleanly" : round == 1 ? "0xFF memory: refused cleanly" : "random memory: refused cleanly", cbvmc_ipl_decide(&in, &out) != CBVMC_IPLR_OK && memcmp(ram, copy, 0x1800000) == 0);
        }
    }
    for (i = 0; i < CBVMC_IPLR_COUNT; ++i) if (!cbvmc_ipl_refuse_text(i)[0]) { check("every refusal has a text", 0); break; }
    printf("IPL POLICY TEST: %s\n", bad ? "FAIL" : "PASS");
    return bad ? 1 : 0;
}
