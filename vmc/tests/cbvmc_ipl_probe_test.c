/* SPDX-License-Identifier: GPL-2.0-only */
/* Host test of the G3 IPL probe (cbvmc_ipl_probe.c).  No BS2 image exists here, so the probe is run on stand-ins:
 *   cbvmc_ipl_probe_test DOL     a retail-style game DOL placed at its addresses in a 24 MiB image, scan window = its code: the report must contain the
 *                                scanner verdict of the game path (Melee 1.02: ok, 25 words, hook 8034312C, VerifyID, DoMount)
 * and on hostile images (all zero, random, 0xFF): no crash (ASan/UBSan), five stages, text terminated and printable, memory never written. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "cbvmc_ipl_probe.h"

static uint32_t be(const uint8_t *p) { return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) | ((uint32_t)p[2] << 8) | p[3]; }

struct cap { char text[6][9000]; unsigned len[6]; unsigned calls; unsigned fail_at; unsigned order_ok; };

static int emit(void *ctx, unsigned stage, const char *text, unsigned len)
{
    struct cap *c = (struct cap *)ctx;
    if (stage != c->calls + 2u) c->order_ok = 0;
    if (stage >= 2 && stage <= 6 && len < sizeof(c->text[0])) { memcpy(c->text[stage - 1], text, len + 1u); c->len[stage - 1] = len; }
    ++c->calls;
    return c->fail_at && stage == c->fail_at;
}

static uint64_t fake_now(void) { static uint64_t t; t += 40500u; return t; }

static const struct cbvmc_ipl_sym syms[] = {
    { "fakeset", "EXIImm", 0x803551D0u }, { "fakeset", "EXIImmEx", 0x803551D0u }, { "fakeset", "EXISync", 0x803551D0u }, { "fakeset", "EXISelect", 0x803551D0u },
    { "fakeset", "EXIDeselect", 0x803551D0u }, { "fakeset", "EXILock", 0x803551D0u }, { "fakeset", "EXIUnlock", 0x803551D0u }, { "fakeset", "CARDProbe", 0x803551D0u },
    { "otherset", "EXIImm", 0x80003100u }, { "otherset", "EXILock", 0x80003100u },
};

static int check(const char *what, int ok) { printf("%s %s\n", ok ? "ok  " : "FAIL", what); return ok ? 0 : 1; }
static int has(struct cap *c, const char *needle) { unsigned i; for (i = 1; i < 6; ++i) if (strstr(c->text[i], needle)) return 1; return 0; }

static unsigned run(uint8_t *ram, uint32_t lo, uint32_t hi, struct cap *c)
{
    struct cbvmc_ipl_env e;
    memset(&e, 0, sizeof(e)); memset(c, 0, sizeof(*c));
    c->order_ok = 1;
    e.mem = ram; e.bs2_lo = lo; e.bs2_hi = hi; e.ipl_rev = 10; e.r2 = 0x8148AAE0u; e.r13 = 0x8148B640u; e.msr = 0x0000B032u;
    e.syms = syms; e.nsyms = sizeof(syms) / sizeof(syms[0]); e.now = fake_now; e.ticks_per_ms = 40500u; e.emit = emit; e.ctx = c;
    e.patches_end = 0x81640000u; e.lowmem_end = 0x80100800u; e.vmc_enabled = 1;
    return cbvmc_ipl_probe_run(&e);
}

int main(int argc, char **argv)
{
    FILE *f; uint8_t *d, *ram, *copy; long n; unsigned i, bad = 0, got; uint32_t lo = 0, hi = 0; static struct cap c;
    if (argc < 2 || !(f = fopen(argv[1], "rb"))) { fprintf(stderr, "usage: cbvmc_ipl_probe_test DOL\n"); return 2; }
    fseek(f, 0, SEEK_END); n = ftell(f); fseek(f, 0, SEEK_SET);
    d = malloc((size_t)n); if (fread(d, 1, (size_t)n, f) != (size_t)n) return 2; fclose(f);
    ram = calloc(1, 0x1800000); copy = malloc(0x1800000);
    for (i = 0; i < 18; ++i) {
        uint32_t off = be(d + 4 * i), addr = be(d + 0x48 + 4 * i), len = be(d + 0x90 + 4 * i);
        if (!off || !len) continue;
        memcpy(ram + (addr - 0x80000000u), d + off, len);
        if (i < 7 && len > hi - lo) { lo = addr; hi = addr + len; }          /* the biggest code section */
    }
    if (hi - lo > 0x400000u) hi = lo + 0x400000u;
    memcpy(copy, ram, 0x1800000);
    got = run(ram, lo, hi, &c);
    bad += check("five stages emitted in order", got == 5 && c.calls == 5 && c.order_ok);
    bad += check("memory not written", memcmp(ram, copy, 0x1800000) == 0);
    bad += check("strict scan ok", has(&c, "[scan strict] ok 1"));
    bad += check("hook 8034312C -> EXIInit", has(&c, "hook 8034312C -> EXIInit"));
    bad += check("25 words", has(&c, "words 25"));
    bad += check("VerifyID candidate found once", has(&c, "VerifyID shape candidates: 803551D0 total 1"));
    bad += check("DoMount compare found once", has(&c, "total 1\n[sites]") || has(&c, "DoMount compare candidates (branch addr orig patched): "));
    bad += check("EXIInit has one caller", has(&c, "callers (bl): 8034312C  total 1"));
    bad += check("best set picks the one with prologues", has(&c, "best set fakeset prologues 8") || has(&c, "best set NONE"));
    bad += check("sites listed", has(&c, "[sites]") && has(&c, "exi_or_pi_sites"));
    bad += check("blob window reported", has(&c, "blob window 80000C00-80003000"));
    bad += check("threads section", has(&c, "[threads]"));
    if (argc > 2 && !strcmp(argv[2], "--dump")) for (i = 1; i < 6; ++i) printf("--- stage %u (%u bytes) ---\n%s", i + 1, c.len[i], c.text[i]);
    /* a failing sink stops the probe */
    memset(&c, 0, sizeof(c)); c.order_ok = 1; c.fail_at = 3;
    { struct cbvmc_ipl_env e; memset(&e, 0, sizeof(e)); e.mem = ram; e.bs2_lo = lo; e.bs2_hi = hi; e.emit = emit; e.ctx = &c;
      got = cbvmc_ipl_probe_run(&e); bad += check("sink failure at stage 3 stops after 3 calls", c.calls == 2 && got == 2); }
    /* hostile images */
    {
        static const unsigned fill[] = { 0, 1, 2 };
        unsigned k;
        for (k = 0; k < 3; ++k) {
            unsigned j; uint32_t x = 12345u;
            for (j = 0; j < 0x1800000u; ++j) { x = x * 1664525u + 1013904223u; ram[j] = fill[k] == 0 ? 0 : fill[k] == 1 ? 0xFF : (uint8_t)(x >> 24); }
            memcpy(copy, ram, 0x1800000);
            got = run(ram, 0x81300000u, 0x81400000u, &c);
            bad += check(fill[k] == 0 ? "zero image: five stages" : fill[k] == 1 ? "0xFF image: five stages" : "random image: five stages", got == 5);
            bad += check("memory not written", memcmp(ram, copy, 0x1800000) == 0);
            for (i = 1; i < 6; ++i) { unsigned q; for (q = 0; q < c.len[i]; ++q) if ((unsigned char)c.text[i][q] > 126 || ((unsigned char)c.text[i][q] < 32 && c.text[i][q] != '\n')) { bad += check("printable text", 0); break; } }
        }
        got = run(ram, 0x81300000u, 0x81300000u, &c); bad += check("empty window does not scan", got == 5 && has(&c, "window invalid"));
        got = run(ram, 0x7F000000u, 0x81400000u, &c); bad += check("window outside MEM1 does not scan", got == 5 && has(&c, "window invalid"));
    }
    printf("IPL PROBE TEST: %s\n", bad ? "FAIL" : "PASS");
    return bad ? 1 : 0;
}
