/* Copyright (C) 2026 Nigzel / Bjerreman. SPDX-License-Identifier: GPL-2.0-only */
/* G4 stage 1, Cubeboot side, see cbvmc_ipl_dev.h and docs/virtual_memory_card.md.  Only with -DCBVMC_IPL.  Original work. */
#ifdef CBVMC_IPL

#include <stddef.h>
#include <string.h>

#include "flippy_sync.h"
#include "reloc.h"
#include "attr.h"
#include "time.h"
#include "ipl.h"
#include "usbgecko.h"

#include "cbvmc_ipl_dev.h"
#include "cbvmc_ipl_policy.h"
#include "cbvmc_flash_hook.h"
#include "cbvmc_ipl_blob_bin.h"
#include "cbvmc_ipl_res_bin.h"

extern const void _patches_end;
extern uint32_t cbvmc_blocks;                            /* cbvmc_flash_hook.c: vmc_blocks for a card file generated this boot */
extern void custom_OSReport(const char *fmt, ...);

/* config.ini vmc_ipl_diag: bisect switches, cbvmc_ipl_plan.h CBVMC_IPLD_*; 0 = the full install */
extern uint32_t cbvmc_ipl_diag;                           /* cbvmc_flash_hook.c (vmc_diag) */
volatile uint32_t cbvmc_ipl_noatomic;                  /* read by flippy_sync.c: diag bit 1 */
volatile uint32_t cbvmc_ipl_cnt[16];                    /* heartbeat counters: 0 frames (bs2tick), 1 menu input handler calls, 2 mcp_set_gameid, 3 setup_gameid_commands, 4 its BUSY spins, 5 &pad_status record */
extern uint32_t disable_mcp_select;                    /* gameid.c */

static uint32_t w_vec, w_addr, w_patched;                /* what the install wrote: the DSI vector word and the first BS2 word, watched from bs2tick */
static uint32_t ipl_live;                                /* the installer reported OK: the hand-over has work to do */

/* the known function addresses of patches/linker/link_*.ld: absolute symbols resolved at link time, nothing is called */
#define SYMLIST(X, P) X(P, EXIImm) X(P, EXIImmEx) X(P, EXISync) X(P, EXISelect) X(P, EXIDeselect) X(P, EXILock) X(P, EXIUnlock) X(P, CARDProbe)
#define X_EXTERN(P, N) extern char P##_##N[];
#define X_ENTRY(P, N) (uint32_t)(uintptr_t)&P##_##N,
#define SETDEF(P, MASK) { #P, MASK, { SYMLIST(X_ENTRY, P) } }
SYMLIST(X_EXTERN, ntsc10) SYMLIST(X_EXTERN, ntsc11) SYMLIST(X_EXTERN, ntsc12_001) SYMLIST(X_EXTERN, ntsc12_101)
SYMLIST(X_EXTERN, pal10) SYMLIST(X_EXTERN, pal11) SYMLIST(X_EXTERN, pal12)
/* ipl_revision_t ids (ipl.h): 1 NTSC_10_001, 2 NTSC_10_002, 3 DEV_10, 4 NTSC_11_001, 5 PAL_10_001, 6 PAL_10_002, 7 MPAL_11, 8 TDEV_11, 9 NTSC_12_001, 10 NTSC_12_101, 11 PAL_12_101 */
static const struct cbvmc_ipl_symset g_sets[] = {
    SETDEF(ntsc10, (1u << 1) | (1u << 2)), SETDEF(ntsc11, 1u << 4), SETDEF(ntsc12_001, 1u << 9), SETDEF(ntsc12_101, 1u << 10),
    SETDEF(pal10, (1u << 5) | (1u << 6)), SETDEF(pal11, 0), SETDEF(pal12, 1u << 11),
};

#define MFSPR(n) ({ uint32_t v_; __asm__ volatile("mfspr %0,%1" : "=r"(v_) : "i"(n)); v_; })
#define MSR_EE 0x8000u

static uint32_t exi_rd(unsigned ch, unsigned idx) { return ((volatile uint32_t *)(0xCC006800u + 0x14u * ch))[idx]; }

/* A card channel with a selected device or a running transfer: somebody is in the middle of a transaction */
static uint32_t exi_busy(void)
{
    unsigned ch;
    uint32_t b = 0;
    for (ch = 0; ch < 2; ++ch) {
        if (exi_rd(ch, 0) & 0x380u) b |= 1u << (2 * ch);
        if (exi_rd(ch, 3) & 1u) b |= 2u << (2 * ch);
    }
    return b;
}

/* Z held at power-on (the skip switch), read from the SI controller-0 input buffer BS2's pad polling fills.  If the polling is not running yet or the pad does not
   answer this returns 0 (not held): the switch is best effort; the sure switches are vmc = 0 in config.ini and the recovery flash. */
static uint32_t z_held(void)
{
    /* The Z skip is OFF by default since 2026-10-11: reading SIC0INBUFH during BS2's pad initialisation takes a response word (and the RDST flag) that the SDK's SI
       state machine is waiting for; the pad then never becomes ready (pad record stays zero, menu input dead) although the adapter polls.  Opt in with diag bit 17. */
    if (!(cbvmc_ipl_diag & CBVMC_IPLD_ENABLE_Z) || (cbvmc_ipl_diag & CBVMC_IPLD_NO_Z)) return 0;
    volatile uint32_t *si = (volatile uint32_t *)0xCC006400;
    uint32_t poll = si[0x30 / 4], hi;
    if (!(poll & 0x80u)) return 0;                      /* EN0: polling of controller 0 */
    hi = si[1];                                         /* SIC0INBUFH: ERRSTAT (31), ERRLATCH (30), then the pad word */
    if (hi & 0x80000000u) return 0;
    return (hi & 0x00100000u) != 0;                     /* Z */
}

static void flush_range(uint32_t a, uint32_t n)
{
    uint32_t p;
    for (p = a & ~31u; p < a + n; p += 32u) __asm__ volatile("dcbf 0,%0 ; sync ; icbi 0,%0" : : "r"(p) : "memory");
    __asm__ volatile("sync ; isync" : : : "memory");
}

/* ---- the decision file (own file, written after the install call so that it holds the result) ---- */
static char g_txt[1536] __attribute__((aligned(32)));
static unsigned g_n;
static void t_s(const char *s) { while (*s && g_n + 2 < sizeof(g_txt)) g_txt[g_n++] = *s++; }
static void t_x(uint32_t v) { unsigned i; for (i = 0; i < 8 && g_n + 2 < sizeof(g_txt); ++i) g_txt[g_n++] = "0123456789ABCDEF"[(v >> (28 - 4 * i)) & 15u]; }
static void t_d(unsigned v) { char b[12]; unsigned k = 0; do { b[k++] = (char)('0' + v % 10u); v /= 10u; } while (v); while (k && g_n + 2 < sizeof(g_txt)) g_txt[g_n++] = b[--k]; }

static void write_file(const char *path)
{
    file_status_t *st;
    unsigned pad = (32u - (g_n & 31u)) & 31u, i;
    for (i = 0; i < pad; ++i) g_txt[g_n + i] = '\n';
    dvd_custom_unlink((char *)path);
    if (dvd_custom_open(path, FILE_ENTRY_TYPE_FILE, IPC_FILE_FLAG_WRITE | IPC_FILE_FLAG_DISABLECACHE | IPC_FILE_FLAG_DISABLEFASTSEEK | IPC_FILE_FLAG_DISABLESPEEDEMU) != 0) return;
    st = dvd_custom_status();
    if (st == NULL || st->result != 0) return;
    DCFlushRange(g_txt, g_n + pad);
    dvd_custom_write(g_txt, 0, g_n + pad, st->fd);
    dvd_custom_close(st->fd);
}

/* one-off diagnostic (CBVMC_IPLD_DUMP_BS2): BS2 as it runs plus low memory, 8 KiB writes */
static void dump_memory(const char *path, uint32_t addr, uint32_t len, uint32_t addr2, uint32_t len2)
{
    file_status_t *st;
    uint32_t off = 0, k;
    dvd_custom_unlink((char *)path);
    if (dvd_custom_open(path, FILE_ENTRY_TYPE_FILE, IPC_FILE_FLAG_WRITE | IPC_FILE_FLAG_DISABLECACHE | IPC_FILE_FLAG_DISABLEFASTSEEK | IPC_FILE_FLAG_DISABLESPEEDEMU) != 0) return;
    st = dvd_custom_status();
    if (st == NULL || st->result != 0) return;
    DCFlushRange((void *)addr, len);
    for (k = 0; k < len; k += 8192u) { dvd_custom_write((void *)(addr + k), off, 8192u, st->fd); off += 8192u; }
    DCFlushRange((void *)addr2, len2);
    for (k = 0; k < len2; k += 8192u) { dvd_custom_write((void *)(addr2 + k), off, 8192u, st->fd); off += 8192u; }
    dvd_custom_close(st->fd);
}

void cbvmc_ipl_install_device(void)
{
    static struct cbvmc_ipl_in in;
    static struct cbvmc_ipl_out out;
    uint32_t msr, th, ticks0 = (uint32_t)gettime(), allow = 0, physical = 0, id[2] = { 0, 0 }, i;
    unsigned r;
    volatile struct cbvmc_ipl_args *args = (volatile struct cbvmc_ipl_args *)CBVMC_IPL_ARGS;
    int ires = -1;
    const unsigned lvl = (cbvmc_ipl_diag >> CBVMC_IPLD_STOP_SHIFT) & 15u;
#define STOP(n) do { if (lvl == (n)) return; } while (0)

    if (!cbvmc_flash_enabled()) return;                                   /* the IPL part is always on with vmc = 1: every check of the decision must pass */
    STOP(1);            /* vmc = 0: no file, no probe, stock behaviour */
    memset(&in, 0, sizeof(in));
    in.mem = (const uint8_t *)0x80000000;
    cbvmc_ipl_noatomic = (cbvmc_ipl_diag & CBVMC_IPLD_NO_ATOMIC) ? 1u : 0u;
    in.vmc_enabled = (uint32_t)cbvmc_flash_enabled();
    STOP(2);
    in.ipl_rev = (uint32_t)get_ipl_revision();
    STOP(3);
    in.patches_end = (uint32_t)(uintptr_t)&_patches_end;
    in.region_base = CBVMC_IPL_BASE;
    in.sets = g_sets; in.nsets = sizeof(g_sets) / sizeof(g_sets[0]);
    __asm__ volatile("mfmsr %0" : "=r"(in.msr));
    in.bat[0] = MFSPR(528); in.bat[1] = MFSPR(529); in.bat[2] = MFSPR(530); in.bat[3] = MFSPR(531);
    in.bat[4] = MFSPR(532); in.bat[5] = MFSPR(533); in.bat[6] = MFSPR(534); in.bat[7] = MFSPR(535);
    in.bat[8] = MFSPR(536); in.bat[9] = MFSPR(537); in.bat[10] = MFSPR(538); in.bat[11] = MFSPR(539);
    in.bat[12] = MFSPR(540); in.bat[13] = MFSPR(541); in.bat[14] = MFSPR(542); in.bat[15] = MFSPR(543);
    { register uint32_t r1v __asm__("r1"); in.sp = r1v; }
    th = *(volatile uint32_t *)0x800000E4;                                 /* OSCurrentThread: stackEnd is the lowest address of its stack */
    in.stack_end = (th >= 0x80000000u && th < 0x817FFD00u && !(th & 3u)) ? *(volatile uint32_t *)(th + 0x308) : 0;
    in.z_held = z_held();
    cbvmc_ipl_cnt[5] = (uint32_t)(uintptr_t)pad_status;
    STOP(4);
    custom_OSReport("cbvmc ipl: start (rev %d)\n", in.ipl_rev);
    STOP(5);

    /* the physical-device probe (owner rule: a physical card, a MemCard PRO or any other device in a slot wins), with interrupts off and only when both card
       channels are quiet; then the decision */
    if (cbvmc_ipl_diag & CBVMC_IPLD_NO_PROBE) allow = 3;
    else if (!in.z_held) {
        unsigned tries;
        for (tries = 0; tries < 100; ++tries) {                         /* a transaction of BS2's own on a card channel finishes within milliseconds: wait up to 100 ms */
            __asm__ volatile("mfmsr %0" : "=r"(msr));
            __asm__ volatile("mtmsr %0 ; isync" : : "r"(msr & ~MSR_EE) : "memory");
            in.exi_busy = exi_busy();
            if (!in.exi_busy) {
                allow = cbvmc_flash_probe();                            /* interrupts stay off for the probe: two short transfers */
                for (i = 0; i < 2; ++i) if (exi_rd(i, 0) & 0x1000u) ((volatile uint32_t *)(0xCC006800u + 0x14u * i))[0] = (exi_rd(i, 0) & 0x405u) | 0x8u;   /* masks as found, clear our TCINT */
            }
            __asm__ volatile("mtmsr %0 ; isync" : : "r"(msr) : "memory");
            if (!in.exi_busy) break;
            udelay(1000);
        }
    }
    STOP(6);
    physical = (~allow) & 3u;                                       /* a device sat in these slots: their virtual cards start parked */
    if (cbvmc_ipl_diag & CBVMC_IPLD_NO_A) allow &= ~1u;
    if (cbvmc_ipl_diag & CBVMC_IPLD_NO_B) allow &= ~2u;
    in.allow = allow;
    if (cbvmc_ipl_diag & CBVMC_IPLD_NO_SCAN) { memset(&out, 0, sizeof(out)); out.best_set = -1; r = 99u; }
    else r = cbvmc_ipl_decide(&in, &out);
    (void)id;
    STOP(7);

    if (r == CBVMC_IPLR_OK && !(cbvmc_ipl_diag & CBVMC_IPLD_SKIP_ALL)) {
        /* the images go into the top of the patch window; the argument block last.  Nothing points at them until the installer applies the DSI vector. */
        if (!(cbvmc_ipl_diag & CBVMC_IPLD_NO_MEMSET)) memset((void *)CBVMC_IPL_BASE, 0, CBVMC_IPL_SIZE);
        memcpy((void *)CBVMC_IPL_BLOB_BASE, cbvmc_ipl_blob_bin, cbvmc_ipl_blob_bin_size);
        if (!(cbvmc_ipl_diag & CBVMC_IPLD_BLOB_ONLY)) memcpy((void *)CBVMC_IPL_RES_BASE, cbvmc_ipl_res_bin, cbvmc_ipl_res_bin_size);
        args->magic = CBVMC_IPL_ARGS_MAGIC; args->version = 5; args->regions = cbvmc_flash_regions(); args->allow = allow; args->geom = cbvmc_blocks ? (0x47000000u | (((cbvmc_blocks + 5u) / 16u) & 0xFFFFu)) : 0u; args->physical = physical; args->flags = cbvmc_ipl_diag; args->hb_addr = (cbvmc_ipl_diag & CBVMC_IPLD_NO_HB) ? 0u : (uint32_t)(uintptr_t)cbvmc_ipl_cnt; args->nwords = out.scan.nwords;
        for (i = 0; i < out.scan.nwords; ++i) { args->words[i].addr = out.scan.words[i].addr; args->words[i].orig = out.scan.words[i].orig; args->words[i].patched = out.scan.words[i].patched; }
        {   /* the card folder and the console's default layer: Japanese font strap (VI 0xCC00206E bit 1) = JAP, a PAL IPL revision = EUR, else USA;
               the installer composes the two paths, after the layer file in the folder had its say */
            const char *dir = cbvmc_flash_dir();
            for (i = 0; dir[i] && i < CBVMC_IPL_PATH_MAX - 1u; ++i) args->dir[i] = dir[i];
            args->layer = (*(volatile uint16_t *)0xCC00206Eu & 2u) ? CBVMC_LAYER_JAP : (cbvmc_flash_regions() == CBVMC_REGIONS_SWISS && (in.ipl_rev == 5u || in.ipl_rev == 6u || in.ipl_rev == 11u)) ? CBVMC_LAYER_EUR : CBVMC_LAYER_USA;
        }
        flush_range(CBVMC_IPL_BASE, CBVMC_IPL_SIZE);
        STOP(8);
        if (!(cbvmc_ipl_diag & (CBVMC_IPLD_BLOB_ONLY | CBVMC_IPLD_NO_CALL))) ires = ((int (*)(volatile struct cbvmc_ipl_args *))CBVMC_IPL_ENTRY_INSTALL)(args);
        if (ires == CBVMC_IPLS_OK && (cbvmc_ipl_diag & CBVMC_IPLD_DRY) && (cbvmc_ipl_diag & CBVMC_IPLD_CLOSE_DRY)) ((int (*)(void))CBVMC_IPL_ENTRY_FINISH)();       /* handles closed again */
        if (ires == CBVMC_IPLS_OK && !(cbvmc_ipl_diag & CBVMC_IPLD_DRY)) {
            ipl_live = 1;
            w_vec = args->res_vec_after; w_addr = args->words[0].addr; w_patched = args->words[0].patched;
            /* Cubeboot's MemCard PRO game-id commands (EXILock/Select/Imm on channel 0 when a game is picked) would now meet the virtual card: they are for a MemCard PRO,
               not for us, and their BUSY loops are the one place in the menu that could wait on the card library; off while a virtual card is live */
            if (!(cbvmc_ipl_diag & CBVMC_IPLD_KEEP_MCP)) disable_mcp_select = 1;
        }
    }
    STOP(9);
#undef STOP

    g_n = 0;
    t_s("cbvmc ipl decision 1 (docs/virtual_memory_card.md; addresses, counts and codes only)\n");
    t_s("decision "); t_d(r); t_s(" ("); t_s(cbvmc_ipl_refuse_text(r)); t_s(") detail "); t_x(out.detail); t_s("\n");
    t_s("diag "); t_x(cbvmc_ipl_diag); t_s("\n");
    t_s("ipl_rev "); t_d(in.ipl_rev); t_s(" z_held "); t_d(in.z_held); t_s(" slot allow mask "); t_d(allow); t_s(" exi_busy "); t_d(in.exi_busy); t_s("\n");
    t_s("msr "); t_x(in.msr); t_s(" sp "); t_x(in.sp); t_s(" stack_end "); t_x(in.stack_end); t_s(" patches_end "); t_x(in.patches_end); t_s("\n");
    t_s("best symbol set "); t_s(out.best_set >= 0 ? g_sets[out.best_set].name : "none"); t_s(" prologues "); t_d(out.best_prologues); t_s(" block "); t_x(out.blk_lo); t_s("-"); t_x(out.blk_hi); t_s("\n");
    t_s("scan ok "); t_d((unsigned)(out.scan.ok > 0)); t_s(" refuse "); t_d(out.scan.refuse); t_s(" words "); t_d(out.scan.nwords); t_s(" hook "); t_x(out.scan.hook_addr); t_s("\n");
    if (r == CBVMC_IPLR_OK) {
        t_s("installer result "); t_d((unsigned)ires); t_s(" detail "); t_d(args->res_detail); t_s(" live mask "); t_d(args->res_live); t_s(" exi tries "); t_d(args->res_exi_tries);
        t_s(" slot A "); t_d(args->res_slot_status[0]); t_s("/"); t_d(args->res_slot_detail[0]); t_s(" slot B "); t_d(args->res_slot_status[1]); t_s("/"); t_d(args->res_slot_detail[1]);
        t_s(" ms "); t_d(args->res_ticks / (unsigned)(TB_TIMER_CLOCK)); t_s("\n");
    }
    t_s("total ms "); t_d(((uint32_t)gettime() - ticks0) / (unsigned)(TB_TIMER_CLOCK)); t_s("\n");
    if ((cbvmc_ipl_diag & CBVMC_IPLD_STATUS) && !(cbvmc_ipl_diag & CBVMC_IPLD_NO_DECFILE)) write_file("/cubeboot/cbvmc_ipl_decision.txt");
    if (cbvmc_ipl_diag & CBVMC_IPLD_DUMP_BS2) dump_memory("/cubeboot/bs2.bin", 0x81300000u, 0x100000u, 0x80000000u, 0x4000u);
    custom_OSReport("cbvmc ipl: decision %d install %d\n", r, ires);
}


/* heartbeat written by the patch itself (diag bit 15), called from bs2tick: about every 3 s, a new small file each time (unlink + write), main thread.  It needs no trap, so it works in
   the dry run: frames, input-handler calls, the pad record the menu reads, SI/PI/DI registers, MSR, every thread (state, priority, saved pc, lr). */
static uint32_t rd32m(uint32_t a) { return *(volatile uint32_t *)a; }
static int okm(uint32_t p) { return p >= 0x80000000u && p < 0x81800000u && (p & 3u) == 0; }
void cbvmc_ipl_patch_hb(void)
{
    static uint32_t tick, n;
    uint32_t k, th, pad, msr;
    unsigned guard = 0;
    if (!(cbvmc_ipl_diag & CBVMC_IPLD_PATCH_HB)) return;
    if (++tick < 180) return;
    tick = 0; ++n;
    if ((cbvmc_ipl_diag & CBVMC_IPLD_DUMP_HI) && n == 20) { dump_memory("/cubeboot/hi.bin", 0x81390000u, 0x10000u, 0x814A8000u, 0x10000u); dump_memory("/cubeboot/hi2.bin", 0x817C0000u, 0x20000u, 0x80000000u, 0x2000u); }
    g_n = 0;
    t_s("cbvmc ipl patch heartbeat n="); t_d(n); t_s(" diag "); t_x(cbvmc_ipl_diag); t_s(" live "); t_d(ipl_live); t_s("\n");
    t_s("frames "); t_d(cbvmc_ipl_cnt[0]); t_s(" input_calls "); t_d(cbvmc_ipl_cnt[1]); t_s(" mcp_set "); t_d(cbvmc_ipl_cnt[2]); t_s(" mcp_setup "); t_d(cbvmc_ipl_cnt[3]); t_s(" spins "); t_d(cbvmc_ipl_cnt[4]); t_s("\n");
    pad = cbvmc_ipl_cnt[5];
    if (okm(pad) && okm(pad + 0x14)) { t_s("pad record "); for (k = 0; k < 5; ++k) { t_x(rd32m(pad + 4 * k)); t_s(" "); } t_s("\n"); }
    t_s("PI cause "); t_x(rd32m(0xCC003000u)); t_s(" mask "); t_x(rd32m(0xCC003004u)); t_s(" SI poll "); t_x(rd32m(0xCC006430u)); t_s(" comcsr "); t_x(rd32m(0xCC006434u));
    t_s(" sisr "); t_x(rd32m(0xCC006438u)); t_s(" in0h "); t_x(rd32m(0xCC006404u)); t_s("\nDI sr "); t_x(rd32m(0xCC006000u)); t_s(" cr "); t_x(rd32m(0xCC00601Cu));
    __asm__ volatile("mfmsr %0" : "=r"(msr)); t_s(" msr "); t_x(msr); t_s("\nthreads (addr state prio srr0 lr; current "); t_x(rd32m(0x800000E4u)); t_s("):\n");
    th = rd32m(0x800000DCu);
    while (okm(th) && th + 0x310u < 0x81800000u && guard++ < 12u) {
        t_x(th); t_s(" "); t_d(rd32m(th + 0x2C8u) >> 16); t_s(" "); t_d(rd32m(th + 0x2D0u)); t_s(" "); t_x(rd32m(th + 0x198u)); t_s(" "); t_x(rd32m(th + 0x84u)); t_s("\n");
        th = rd32m(th + 0x2FCu);
    }
    write_file("/cubeboot/cbvmc_ipl_phb.txt");
}

/* from bs2tick, every frame once the install is live: has anything undone the install?  The vector word at 0x80000300 and the first patched BS2 word are compared with what the installer
   wrote; the first frame where they differ (plus the new value) goes into the counters (cnt[6..9], printed by the hand-over).  Option bits: REASSERT writes the vector word back, REPLUG announces
   the cards again (EXTINT) at frames 120, 600 and 1800. */
void cbvmc_ipl_watch(void)
{
    uint32_t frame, v, w;
    volatile uint32_t *vec = (volatile uint32_t *)0x80000300;
    if (!ipl_live) return;
    frame = cbvmc_ipl_cnt[0];
    v = *vec;
    if (v != w_vec) {
        if (!cbvmc_ipl_cnt[6]) { cbvmc_ipl_cnt[6] = frame + 1; cbvmc_ipl_cnt[7] = v; }
        if ((cbvmc_ipl_diag & CBVMC_IPLD_REASSERT)) { *vec = w_vec; flush_range(0x80000300u, 4); cbvmc_ipl_cnt[10]++; }
    }
    w = *(volatile uint32_t *)w_addr;
    if (w != w_patched && !cbvmc_ipl_cnt[8]) { cbvmc_ipl_cnt[8] = frame + 1; cbvmc_ipl_cnt[9] = w; }
    if ((cbvmc_ipl_diag & CBVMC_IPLD_REPLUG) && (frame == 120 || frame == 600 || frame == 1800)) { ((void (*)(void))CBVMC_IPL_ENTRY_REPLUG)(); cbvmc_ipl_cnt[11]++; }
}

void cbvmc_ipl_finish_device(void)
{
    if (!ipl_live) return;
    ipl_live = 0;
    ((int (*)(void))CBVMC_IPL_ENTRY_FINISH)();
}

#endif
