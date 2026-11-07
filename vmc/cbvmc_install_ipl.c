/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * cbvmc_install_ipl: the installer of the IPL install (G4 stage 1, docs/virtual_memory_card.md).  PowerPC, freestanding.  Original work.
 * Linked into the resident+installer image (the blob is a separate image at the bottom of the same region); called by the Cubeboot patch from
 * pre_thread_init as an ordinary C function, on the calling thread's stack, with interrupts ON, BS2's OS already running, the menu's file thread NOT yet started.
 *
 * The decisions (is this BS2 one we know, is the word table right, is the region free, is a physical card in the slot) were taken by the Cubeboot patch
 * before it copied the two images; this function repeats the cheap ones that protect memory (arguments, DSI stub word, every original word of the table) and
 * then does, in this order, and leaves the machine untouched on every failure before step 4:
 *   1. for each allowed slot: open the card file READ-ONLY, refuse a pending journal (cbvmc_resident_open)
 *   2. bind the trap blob to the first served slot's store, initialise the model of every served slot (validates the card: header, directory, block map)
 *   3. write the status file of the install (what was served, what not and why)
 *   4. the ATOMIC part: interrupts off; both card channels must be quiet (no selected device, no transfer): then the DSI vector (one word) and the word table are
 *      written and flushed, the insertion of the virtual cards is announced like a hot-plug (EXTINT latched), interrupts back on.  If the channels are not quiet the
 *      interrupts go back on, the thread waits a millisecond and tries again, at most CBVMC_IPL_EXI_TRIES times; after that nothing is applied.
 * Nothing in here ever writes a card file.
 */
#include "cbvmc_ipl_plan.h"
#include "cbvmc_blob.h"
#include "cbvmc_words.h"
#include "cbvmc_resident.h"
#include "cubeboot_vmc_exi.h"
#include "cbvmc_cardgen.h"
#include "cbvmc_ipl_menu.h"

void cbvmc_resident_menu_bind(unsigned virt);          /* cbvmc_resident_ipl.c */
void cbvmc_resident_window_discard(void);

extern char cbvmc_vec[];
extern char cbvmc_card_path[], cbvmc_card_path_b[];          /* cbvmc_resident_ipl.c */
extern uint32_t cbvmc_ripl_hb_addr, cbvmc_ripl_diag;

#define CBVMC_IPL_EXI_TRIES 200u
#define MSR_EE 0x8000u

enum { SLOT_OK = 0 };

static uint32_t tb(void) { uint32_t t; __asm__ volatile("mftb %0" : "=r"(t)); return t; }
static uint32_t get_msr(void) { uint32_t m; __asm__ volatile("mfmsr %0" : "=r"(m)); return m; }
static void set_msr(uint32_t m) { __asm__ volatile("mtmsr %0 ; isync" : : "r"(m) : "memory"); }

static void flush_range(uint32_t a, uint32_t n)
{
    uint32_t p;
    for (p = a & ~31u; p < a + n; p += 32u)
        __asm__ volatile("dcbf 0,%0 ; sync ; icbi 0,%0" : : "r"(p) : "memory");
    __asm__ volatile("sync ; isync" : : : "memory");
}

static void wait_ms(unsigned ms)
{
    uint32_t t0 = tb(), n = ms * 40500u;
    while ((uint32_t)(tb() - t0) < n) { }
}

/* the card channels (0 and 1) are idle: no chip select asserted and no transfer running.  Channel 2 (serial/debugger) is never virtual and is not looked at. */
static uint32_t exi_busy_mask(void)
{
    unsigned ch;
    uint32_t m = 0;
    for (ch = 0; ch < 2; ++ch) {
        const volatile uint32_t *r = (const volatile uint32_t *)(0xCC006800u + 0x14u * ch);
        if ((r[0] & 0x380u) != 0) m |= 1u << (2 * ch);          /* a chip select is asserted */
        if ((r[3] & 1u) != 0) m |= 2u << (2 * ch);              /* a transfer is running */
    }
    return m;
}

/* the note: built by hand (no libc formatting in this image) */
static uint32_t g_live;                                         /* slots the install made live (for the re-announce) */
static uint32_t g_allow;                                        /* slots a virtual card may serve (no physical device at boot): a region switch tries all of them */
static char note[640];
static unsigned nn;
static void n_s(const char *s) { while (*s && nn + 2 < sizeof(note)) note[nn++] = *s++; }
static void n_d(int v)
{
    char t[12]; unsigned k = 0;
    if (v < 0) { n_s("-"); v = -v; }
    do { t[k++] = (char)('0' + v % 10); v /= 10; } while (v);
    while (k) { char c[2]; c[0] = t[--k]; c[1] = 0; n_s(c); }
}
static void n_x(uint32_t v)
{
    unsigned i;
    char c[2]; c[1] = 0;
    for (i = 0; i < 8; ++i) { c[0] = "0123456789ABCDEF"[(v >> (28 - 4 * i)) & 15u]; n_s(c); }
}

static int finish(struct cbvmc_ipl_args *a, int st, int detail)
{
    a->res_status = (uint32_t)st;
    a->res_detail = (uint32_t)detail;
    n_s("result "); n_d(st); n_s(" detail "); n_d(detail); n_s(st == CBVMC_IPLS_OK ? " (virtual cards live)\n" : " (nothing applied: BS2 runs exactly as before)\n");
    note[nn < sizeof(note) ? nn : sizeof(note) - 1] = 0;
    cbvmc_resident_note(note);
    return st;
}

static struct cubeboot_vmc_geometry g_geom;           /* for cards generated later (a layer switch) */
static unsigned g_parked;
static const struct cubeboot_vmc_identity g_id = { 0x4D53484E454C4954ull, 0x00000001u };

/* one slot's card file: open through the resident chain, generate it when missing (the layer's encoding), replay a pending journal, prepare the journal */
static int slot_open(unsigned s, const struct cubeboot_vmc_geometry *geom, const struct cubeboot_vmc_store **top, int *detail)
{
    int rc = cbvmc_resident_open(s, top, detail);
    if (rc == CBVMC_OPEN_ERR_NOFILE && *top) {
        struct cubeboot_vmc_identity gid; uint32_t t0; int g;
        __asm__ volatile("mftb %0" : "=r"(t0));
        gid.time = ((uint64_t)t0 << 8) | s;
        gid.seed = 0x4D53484E454C4954ull ^ ((uint64_t)t0 * 0x9E3779B1u) ^ ((uint64_t)s << 56);
        g = cbvmc_cardgen(*top, cbvmc_resident_card_path(s), s, &gid, geom, 0, 0, 0);
        if (g == CBVMC_GEN_CREATED) { rc = CBVMC_OPEN_OK; n_s(s ? "slot B: card file generated\n" : "slot A: card file generated\n"); }
        else { rc = 200; *detail = g; }
    }
    if (rc == CBVMC_OPEN_OK && cbvmc_resident_recover(s) != 0) { rc = CBVMC_OPEN_ERR_JOURNAL; *detail = 0; }
    if (rc == CBVMC_OPEN_OK && cbvmc_resident_prepare(s) != 0) { rc = 201; *detail = 0; }
    return rc;
}
/* the card model of one slot over its store: validated, mounted (the model's own service), parked when a physical device sat there at boot */
static int slot_mount(unsigned s, const struct cubeboot_vmc_store *top, const struct cubeboot_vmc_geometry *geom, unsigned parked)
{
    struct cubeboot_vmc_port *p = s ? cbvmc_resident_port_b() : &cbvmc_state.port;
    int rc = cubeboot_vmc_port_init(p, s ? top : &cbvmc_state.store, s ? CUBEBOOT_VMC_SLOT_B : CUBEBOOT_VMC_SLOT_A, cbvmc_resident_card_path(s), &g_id, geom);
    if (rc == 0) {
        cubeboot_vmc_port_service(p);
        rc = p->source == CUBEBOOT_VMC_SOURCE_VIRTUAL ? 0 : (p->last_error ? p->last_error : -99);
    }
    if (rc == 0 && (parked & (1u << s))) { p->physical_present = 1; p->source = CUBEBOOT_VMC_SOURCE_PHYSICAL; p->dirty = 0; }
    if (rc == 0 && s) (void)cbvmc_blob_bind_b(p);
    return rc;
}
/* the popup's Layer item: both served cards change to the files of `layer` (generated when missing), the layer file remembers it; the caller re-plugs */
static unsigned bring_up_layer(unsigned layer, unsigned live, int *why)
{
    unsigned s, nlive = 0; int rc, detail = 0;
    struct cubeboot_vmc_geometry geom = g_geom;
    cbvmc_resident_layer_paths(0, layer);
    geom.encoding = layer == CBVMC_LAYER_JAP ? CUBEBOOT_VMC_ENC_SHIFT_JIS : CUBEBOOT_VMC_ENC_WINDOWS_1252;
    for (s = 0; s < 2; ++s) {
        const struct cubeboot_vmc_store *top = 0;
        if (!(live & (1u << s))) continue;
        rc = slot_open(s, &geom, &top, &detail);                /* a missing pair is generated with the layer's encoding, in whatever folder vmc_dir names */
        if (rc == CBVMC_OPEN_OK) rc = slot_mount(s, top, &geom, 0);
        if (rc == 0) nlive |= 1u << s; else if (why && !*why) *why = rc * 1000 + detail;
    }
    return nlive;
}
int cbvmc_ipl_set_layer_c(unsigned layer)
{
    unsigned live = g_live, before = cbvmc_resident_layer(), nlive; int why = 0;
    (void)cbvmc_resident_sync();
    cbvmc_resident_window_discard();
    nlive = bring_up_layer(layer, g_allow ? g_allow : live, &why);     /* every slot we may serve: a slot whose file was unusable at boot comes back with a good pair */
    if ((nlive & live) != live) {                                 /* a card that was there would be lost (SD full, read-only, a bad file): back to the layer that worked */
        nlive = bring_up_layer(before, live, 0);
        cbvmc_resident_menu_bind(nlive); g_live = nlive;
        return why ? why : -1;
    }
    (void)cbvmc_resident_layer_file_write(layer);
    cbvmc_resident_menu_bind(nlive);
    g_live = nlive;
    return 0;
}

static int install(struct cbvmc_ipl_args *a)
{
    struct cubeboot_vmc_geometry geom;                                                /* only for a NEW image (config.ini vmc_blocks / vmc_region, else 2043 blocks, Windows-1252); an existing one keeps its own */
    const struct cubeboot_vmc_store *tops[2] = { 0, 0 };
    unsigned allow = (a->allow | (a->version >= 2u ? a->physical : 0u)) & 3u, parked = a->version >= 2u ? (a->physical & 3u) : 0u, served = 0, live = 0, s, first, bad = 0, tries;
    uint32_t busy = 0;
    int rc, detail, first_err = 0, first_detail = 0;
    uint32_t i, msr, vword;
    volatile uint32_t *vec = (volatile uint32_t *)CBVMC_DSI_VECTOR;

    nn = 0;
    (void)cbvmc_geom_unpack(a->version >= 3u ? a->geom : 0u, &geom);                /* the Cubeboot patch's config.ini vmc_blocks, else the default size */
    {   /* the card folder and the layer: the console's default from the patch, the layer file in the folder overrides; the paths follow */
        unsigned layer = a->version >= 4u ? a->layer : CBVMC_LAYER_USA, filed;
        cbvmc_resident_set_regions(a->version >= 5u ? a->regions : CBVMC_REGIONS_STOCK);
        cbvmc_resident_layer_paths(a->version >= 4u && a->dir[0] ? a->dir : 0, layer);
        if (cbvmc_resident_layer_file_read(&filed) == 0 && filed != layer) { layer = filed; cbvmc_resident_layer_paths(0, layer); }
        geom.encoding = layer == CBVMC_LAYER_JAP ? CUBEBOOT_VMC_ENC_SHIFT_JIS : CUBEBOOT_VMC_ENC_WINDOWS_1252;
        g_geom = geom; g_parked = parked;
        n_s("folder "); n_s(cbvmc_resident_dir()); n_s(" layer "); n_s(layer == CBVMC_LAYER_JAP ? "JAP" : layer == CBVMC_LAYER_EUR ? "EUR" : "USA"); n_s("\n");
    }
    n_s("cbvmc ipl install 2 (read/write)\nallow mask "); n_d((int)allow); n_s(", words "); n_d((int)a->nwords); n_s("\n");
    a->res_vec_before = *vec;
    if (a->nwords == 0 || a->nwords > 64u) return finish(a, CBVMC_IPLS_ARGS, (int)a->nwords);
    if (!allow) return finish(a, CBVMC_IPLS_PHYSICAL, 0);
    if (parked) { n_s("parked (physical device at boot): slots "); n_d((int)parked); n_s("\n"); }
    if (*vec != CBVMC_DSI_ORIG) return finish(a, CBVMC_IPLS_VECTOR, (int)*vec);
    rc = cbvmc_words_check((const uint8_t *)0x80000000u, 0x80000000u, 0x01800000u, a->words, a->nwords, &bad);
    if (rc != CBVMC_WORDS_OK) return finish(a, CBVMC_IPLS_WORDS, (int)bad);

    for (s = 0; s < 2; ++s) {
        if (!(allow & (1u << s))) continue;
        rc = slot_open(s, &geom, &tops[s], &detail);
        a->res_slot_status[s] = (uint32_t)rc;
        a->res_slot_detail[s] = (uint32_t)detail;
        if (rc != CBVMC_OPEN_OK) {
            if (!first_err) { first_err = rc; first_detail = detail; }
            n_s(s ? "slot B not served: open status " : "slot A not served: open status "); n_d(rc); n_s(" detail "); n_d(detail);
            n_s(rc == CBVMC_OPEN_ERR_NOFILE ? " (card file missing and could not be generated)\n" : rc == CBVMC_OPEN_ERR_JOURNAL ? " (the write-back journal could not be replayed)\n" : rc == 200 ? " (card file could not be generated: SD full, read-only, or the path)\n" : rc == 201 ? " (journal file could not be prepared: SD full or read-only)\n" : "\n");
            continue;
        }
        served |= 1u << s;
    }
    if (!served) return finish(a, CBVMC_IPLS_NOCARD, first_err * 1000 + first_detail);

    first = (served & 1u) ? 0u : 1u;
    if (cbvmc_blob_bind_ext(tops[first], cbvmc_resident_tick, cbvmc_resident_tick_context(), cbvmc_resident_stack_top()) != 0) return finish(a, CBVMC_IPLS_BIND, 1);
    for (s = 0; s < 2; ++s) {
        if (!(served & (1u << s))) continue;
        rc = slot_mount(s, tops[s], &geom, parked);
        if (rc == 0) live |= 1u << s;
        if (rc == 0 && (s ? cbvmc_resident_port_b() : &cbvmc_state.port)->last_error == CUBEBOOT_VMC_ERR_CORRUPT) n_s(s ? "slot B: card file unformatted (bad header or tables): served as it is, the IPL offers Format\n" : "slot A: card file unformatted (bad header or tables): served as it is, the IPL offers Format\n");
        else { a->res_slot_status[s] = 100u; a->res_slot_detail[s] = (uint32_t)rc; n_s(s ? "slot B card not usable: model status " : "slot A card not usable: model status "); n_d(rc); n_s(" (invalid header, directory or block map)\n"); }
    }
    a->res_live = live;
    g_live = live & ~parked; g_allow = allow & ~parked;
    if (live & 1u) n_s("slot A: virtual card ok\n");
    if (live & 2u) n_s("slot B: virtual card ok\n");
    if (!live) return finish(a, CBVMC_IPLS_NOCARD, 100 * 1000 + rc);

    /* the atomic part */
    vword = 0x48000002u | (((uint32_t)cbvmc_vec & 0x7FFFFFFFu) & 0x03FFFFFCu);        /* ba cbvmc_vec (physical address below 32 MiB, checked by the blob link) */
    for (tries = 1; tries <= CBVMC_IPL_EXI_TRIES; ++tries) {
        msr = get_msr();
        set_msr(msr & ~MSR_EE);
        busy = exi_busy_mask();
        if (busy == 0 && (a->flags & CBVMC_IPLD_DRY)) {
            set_msr(msr);
            a->res_exi_tries = tries; a->res_vec_after = *vec;
            n_s("DRY: nothing applied (vmc_ipl_diag bit 6)\n");
            return finish(a, CBVMC_IPLS_OK, 0);
        }
        if (busy == 0) {
            *vec = vword;
            flush_range(CBVMC_DSI_VECTOR, 4);
            rc = cbvmc_words_apply((uint8_t *)0x80000000u, 0x80000000u, 0x01800000u, a->words, a->nwords, &bad);        /* cannot fail: checked above, interrupts off since */
            for (i = 0; i < a->nwords; ++i) flush_range(a->words[i].addr, 4);
            if (rc != CBVMC_WORDS_OK) { *vec = CBVMC_DSI_ORIG; flush_range(CBVMC_DSI_VECTOR, 4); set_msr(msr); return finish(a, CBVMC_IPLS_WORDS, 1000 + (int)bad); }
            /* announce the virtual cards like a hot-plug: the port was evaluated above, so the insertion event of cubeboot_vmc_exi_service was already consumed;
               latch its effect (a fresh EXI state and EXTINT) by hand.  EXT itself is derived from the port on every CSR read. */
            if (!(a->flags & CBVMC_IPLD_NO_EXTINT) && (live & ~parked & 1u)) { cbvmc_state.rt.exi.cs0_selected = 0; cbvmc_state.rt.exi.csr = (cbvmc_state.rt.exi.csr & ~(7u << CUBEBOOT_VMC_CSR_CS_SHIFT)) | CUBEBOOT_VMC_CSR_EXTINT; }
            if (!(a->flags & CBVMC_IPLD_NO_EXTINT) && (live & ~parked & 2u)) { cbvmc_state.rt.exi1.cs0_selected = 0; cbvmc_state.rt.exi1.csr = (cbvmc_state.rt.exi1.csr & ~(7u << CUBEBOOT_VMC_CSR_CS_SHIFT)) | CUBEBOOT_VMC_CSR_EXTINT; }
            set_msr(msr);
            a->res_exi_tries = tries;
            a->res_vec_after = *vec;
            n_s("applied after "); n_d((int)tries); n_s(" try(ies), DSI vector "); n_x(a->res_vec_before); n_s(" -> "); n_x(a->res_vec_after); n_s("\n");
            /* the Memory Card page popup extension (docs/virtual_memory_card.md): the page code found by signature, nine words patched; off when not found */
            if (!(a->flags & CBVMC_IPLD_NO_MENU)) {
                const char *why = ""; int m;
                cbvmc_resident_menu_bind(live & ~parked);
                m = cbvmc_menu_install(0x81300000u, 0x81400000u, &why);
                if (m > 0) { const struct cbvmc_menu_found *f = cbvmc_menu_found(); n_s("popup extension: "); n_d(m); n_s(" words patched, page "); n_x(f->browse); n_s(" sda delta "); n_d(f->sda_delta); n_s(" enc variant "); n_d((int)f->enc_variant); n_s("\n"); }
                else { n_s("popup extension: not installed ("); n_s(why); n_s(" "); n_d(m); n_s(")\n"); }
            }
            return finish(a, CBVMC_IPLS_OK, 0);
        }
        set_msr(msr);
        wait_ms(1);
    }
    a->res_exi_tries = CBVMC_IPL_EXI_TRIES;
    return finish(a, CBVMC_IPLS_EXIBUSY, (int)busy);       /* bit 0/2: chip select on channel 0/1, bit 1/3: transfer running */
}

int cbvmc_ipl_install_c(struct cbvmc_ipl_args *a)
{
    uint32_t t0 = tb();
    int rc;
    if (!a || a->magic != CBVMC_IPL_ARGS_MAGIC) return CBVMC_IPLS_ARGS;
    if (a->path_a[0]) { unsigned i; for (i = 0; a->path_a[i] && i < CBVMC_IPL_PATH_MAX - 1u; ++i) cbvmc_card_path[i] = a->path_a[i]; cbvmc_card_path[i] = 0; }
    if (a->path_b[0]) { unsigned i; for (i = 0; a->path_b[i] && i < CBVMC_IPL_PATH_MAX - 1u; ++i) cbvmc_card_path_b[i] = a->path_b[i]; cbvmc_card_path_b[i] = 0; }
    cbvmc_ripl_hb_addr = (a->flags & CBVMC_IPLD_NO_HB) ? 0 : a->hb_addr;
    cbvmc_ripl_diag = a->flags;
    rc = install(a);
    a->res_ticks = tb() - t0;
    return rc;
}

/* diag6: announce the virtual cards again like a hot-plug (fresh EXI state, EXTINT latched), from the Cubeboot patch's bs2tick.  The latch is a read-modify-write of model state that the
   DSI handler also touches, so it is done with interrupts off.  The interrupt itself reaches BS2 only through a trapped PI cause read (the vector must be live). */
void cbvmc_ipl_replug_c(void)
{
    uint32_t msr = get_msr();
    set_msr(msr & ~MSR_EE);
    if (g_live & 1u) { cbvmc_state.rt.exi.cs0_selected = 0; cbvmc_state.rt.exi.csr = (cbvmc_state.rt.exi.csr & ~(7u << CUBEBOOT_VMC_CSR_CS_SHIFT)) | CUBEBOOT_VMC_CSR_EXTINT; }
    if (g_live & 2u) { cbvmc_state.rt.exi1.cs0_selected = 0; cbvmc_state.rt.exi1.csr = (cbvmc_state.rt.exi1.csr & ~(7u << CUBEBOOT_VMC_CSR_CS_SHIFT)) | CUBEBOOT_VMC_CSR_EXTINT; }
    set_msr(msr);
}
