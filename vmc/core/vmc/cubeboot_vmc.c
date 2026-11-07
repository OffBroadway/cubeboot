/* Copyright (C) 2026 Nigzel / Bjerreman. SPDX-License-Identifier: GPL-2.0-only */
/*
 * Portable virtual memory-card core. See include/cubeboot_vmc.h.
 *
 * The EXI command set and its quirks follow the behaviour of a retail
 * Nintendo card as modelled by Dolphin's EXI_DeviceMemoryCard (a behavioural
 * reference only; no code is copied). Flash operations complete
 * synchronously: the store call returns before the shim sees chip-select
 * released, so BUSY is never left set and the completion interrupt is
 * raised at release.
 */
#include <string.h>

#include "cubeboot_vmc.h"

#define BLOCK        CUBEBOOT_VMC_BLOCK_BYTES
#define FLASH_ID     0xc221u

enum {
    CMD_NINTENDO_ID = 0x00, CMD_READ_ARRAY = 0x52, CMD_ARRAY_TO_BUFFER = 0x53,
    CMD_SET_INTERRUPT = 0x81, CMD_WRITE_BUFFER = 0x82, CMD_READ_STATUS = 0x83,
    CMD_READ_ID = 0x85, CMD_READ_ERROR_BUFFER = 0x86, CMD_WAKE_UP = 0x87,
    CMD_SLEEP = 0x88, CMD_CLEAR_STATUS = 0x89, CMD_SECTOR_ERASE = 0xf1,
    CMD_PAGE_PROGRAM = 0xf2, CMD_EXTRA_BYTE_PROGRAM = 0xf3,
    CMD_CHIP_ERASE = 0xf4,
};

/* ---- big-endian helpers and checksums --------------------------------- */

static uint16_t get_be16(const uint8_t *p)
{
    return (uint16_t)(((uint16_t)p[0] << 8) | p[1]);
}
static void put_be16(uint8_t *p, uint16_t v)
{
    p[0] = (uint8_t)(v >> 8); p[1] = (uint8_t)v;
}
static void put_be32(uint8_t *p, uint32_t v)
{
    p[0] = (uint8_t)(v >> 24); p[1] = (uint8_t)(v >> 16);
    p[2] = (uint8_t)(v >> 8); p[3] = (uint8_t)v;
}
static void put_be64(uint8_t *p, uint64_t v)
{
    int i;
    for (i = 7; i >= 0; --i) { p[i] = (uint8_t)v; v >>= 8; }
}

/* Sum of big-endian words and of their complements, 0xffff folded to 0. */
static void checksum(const uint8_t *p, size_t n, uint8_t *out)
{
    uint16_t sum = 0, inv = 0;
    size_t i;
    for (i = 0; i < n; i += 2) {
        uint16_t word = get_be16(p + i);
        sum = (uint16_t)(sum + word);
        inv = (uint16_t)(inv + (uint16_t)(word ^ 0xffffu));
    }
    if (sum == 0xffffu) sum = 0;
    if (inv == 0xffffu) inv = 0;
    put_be16(out, sum);
    put_be16(out + 2, inv);
}

static int checksum_ok(const uint8_t *p, size_t n, const uint8_t *stored)
{
    uint8_t c[4];
    checksum(p, n, c);
    return memcmp(c, stored, 4) == 0;
}

/* ---- path helpers ------------------------------------------------------ */

static int copy_path(char *dst, const char *src)
{
    size_t n = strlen(src);
    if (n == 0 || n >= CUBEBOOT_VMC_PATH_MAX) return CUBEBOOT_VMC_ERR_PATH;
    memcpy(dst, src, n + 1);
    return CUBEBOOT_VMC_OK;
}

static int tmp_path(char *dst, const char *path)
{
    size_t n = strlen(path);
    if (n == 0 || n + 4 >= CUBEBOOT_VMC_PATH_MAX) return CUBEBOOT_VMC_ERR_PATH;
    memcpy(dst, path, n);
    memcpy(dst + n, ".tmp", 5);
    return CUBEBOOT_VMC_OK;
}

/* ---- image: validate / free blocks / format ---------------------------- */

int cubeboot_vmc_mbits_valid(unsigned m)
{
    return m == 4 || m == 8 || m == 16 || m == 32 || m == 64 || m == 128;
}

enum cubeboot_vmc_encoding cubeboot_vmc_region_encoding(
    enum cubeboot_vmc_region region)
{
    return region == CUBEBOOT_VMC_REGION_NTSC_J ? CUBEBOOT_VMC_ENC_SHIFT_JIS
                                                : CUBEBOOT_VMC_ENC_WINDOWS_1252;
}

struct cubeboot_vmc_geometry cubeboot_vmc_geometry_for(
    enum cubeboot_vmc_region region, unsigned mbits)
{
    struct cubeboot_vmc_geometry g;
    g.mbits = (uint16_t)(mbits ? mbits : 128u);
    g.encoding = (uint16_t)cubeboot_vmc_region_encoding(region);
    return g;
}

static int build_card_path(char *out, size_t capacity, const char *dir,
                           enum cubeboot_vmc_region region,
                           enum cubeboot_vmc_slot slot, unsigned mbits)
{
    /* Region strings as in Swiss's table (names only, no source text). */
    static const char *const names[] = { "USA", "JPN", "EUR" };
    const char *parts[5];
    size_t n = 0, i;

    if (!out || !capacity) return CUBEBOOT_VMC_ERR_PATH;
    if (!cubeboot_vmc_mbits_valid(mbits ? mbits : 128u) ||
        (unsigned)region > 2u ||
        (slot != CUBEBOOT_VMC_SLOT_A && slot != CUBEBOOT_VMC_SLOT_B))
        return CUBEBOOT_VMC_ERR_PATH;
    parts[0] = dir;
    parts[1] = slot == CUBEBOOT_VMC_SLOT_B ? "MemoryCardB." : "MemoryCardA.";
    parts[2] = names[region];
    parts[3] = ".raw";
    parts[4] = NULL;
    for (i = 0; parts[i]; ++i) {
        size_t l = strlen(parts[i]);
        if (n + l + 1 > capacity) return CUBEBOOT_VMC_ERR_PATH;
        memcpy(out + n, parts[i], l);
        n += l;
    }
    out[n] = '\0';
    return CUBEBOOT_VMC_OK;
}

int cubeboot_vmc_card_path(char *out, size_t capacity,
                           enum cubeboot_vmc_region region,
                           enum cubeboot_vmc_slot slot, unsigned mbits)
{
    return build_card_path(out, capacity, CUBEBOOT_VMC_CARD_DIR, region, slot,
                           mbits);
}

int cubeboot_vmc_legacy_card_path(char *out, size_t capacity,
                                  enum cubeboot_vmc_region region,
                                  enum cubeboot_vmc_slot slot, unsigned mbits)
{
    return build_card_path(out, capacity, CUBEBOOT_VMC_LEGACY_CARD_DIR, region,
                           slot, mbits);
}

/* Size of the file and, if it is a standard card size, its Mbit count. */
static int image_size(const struct cubeboot_vmc_store *store, const char *path,
                      uint32_t *size, unsigned *mbits)
{
    int r = store->size(store->context, path, size);
    unsigned m;
    if (r != CUBEBOOT_VMC_OK) return r;
    m = *size / CUBEBOOT_VMC_MBIT_BYTES;
    if (*size % CUBEBOOT_VMC_MBIT_BYTES || !cubeboot_vmc_mbits_valid(m))
        return CUBEBOOT_VMC_ERR_SIZE;
    *mbits = m;
    return CUBEBOOT_VMC_OK;
}

static int read_block(const struct cubeboot_vmc_store *store, const char *path,
                      uint8_t *block, unsigned index)
{
    return store->read(store->context, path, index * BLOCK, block, BLOCK) == 0
        ? CUBEBOOT_VMC_OK : CUBEBOOT_VMC_ERR_IO;
}

static int dir_valid(const uint8_t *b) { return checksum_ok(b, 0x1ffc, b + 0x1ffc); }
static int bat_valid(const uint8_t *b)
{
    return checksum_ok(b + 4, BLOCK - 4, b);
}

int cubeboot_vmc_inspect(const struct cubeboot_vmc_store *store,
                         const char *path, struct cubeboot_vmc_geometry *out)
{
    uint8_t block[BLOCK], copy[BLOCK];
    uint32_t size;
    unsigned mbits, enc;
    int r, ok;

    r = image_size(store, path, &size, &mbits);
    if (r != CUBEBOOT_VMC_OK) return r;

    r = read_block(store, path, block, 0);
    if (r != CUBEBOOT_VMC_OK) return r;
    enc = get_be16(block + 0x24);
    if (!checksum_ok(block, 0x1fc, block + 0x1fc) ||
        get_be16(block + 0x22) != mbits || enc > CUBEBOOT_VMC_ENC_SHIFT_JIS)
        return CUBEBOOT_VMC_ERR_CORRUPT;

    /* Directory: blocks 1, 2. Either copy may be the stale one. */
    r = read_block(store, path, copy, 1);
    if (r != CUBEBOOT_VMC_OK) return r;
    ok = dir_valid(copy);
    r = read_block(store, path, block, 2);
    if (r != CUBEBOOT_VMC_OK) return r;
    if (!ok && !dir_valid(block)) return CUBEBOOT_VMC_ERR_CORRUPT;

    /* Block-allocation table: blocks 3, 4. */
    r = read_block(store, path, copy, 3);
    if (r != CUBEBOOT_VMC_OK) return r;
    ok = bat_valid(copy);
    r = read_block(store, path, block, 4);
    if (r != CUBEBOOT_VMC_OK) return r;
    if (!ok && !bat_valid(block)) return CUBEBOOT_VMC_ERR_CORRUPT;

    if (out) {
        out->mbits = (uint16_t)mbits;
        out->encoding = (uint16_t)enc;
    }
    return CUBEBOOT_VMC_OK;
}

int cubeboot_vmc_validate(const struct cubeboot_vmc_store *store,
                          const char *path)
{
    return cubeboot_vmc_inspect(store, path, NULL);
}

int cubeboot_vmc_free_blocks(const struct cubeboot_vmc_store *store,
                             const char *path)
{
    uint8_t a[BLOCK], b[BLOCK];
    uint32_t size;
    unsigned mbits;
    int r, va, vb;

    r = image_size(store, path, &size, &mbits);
    if (r != CUBEBOOT_VMC_OK) return r;
    r = read_block(store, path, a, 3);
    if (r != CUBEBOOT_VMC_OK) return r;
    r = read_block(store, path, b, 4);
    if (r != CUBEBOOT_VMC_OK) return r;
    va = bat_valid(a);
    vb = bat_valid(b);
    if (!va && !vb) return CUBEBOOT_VMC_ERR_CORRUPT;
    if (va && vb)  /* newer update counter wins (16-bit wraparound) */
        return (int16_t)(get_be16(b + 4) - get_be16(a + 4)) > 0
            ? get_be16(b + 6) : get_be16(a + 6);
    return va ? get_be16(a + 6) : get_be16(b + 6);
}

static void fill_header(uint8_t *block, unsigned slot,
                        const struct cubeboot_vmc_identity *id,
                        const struct cubeboot_vmc_geometry *g)
{
    uint64_t seed = id->time ^ id->seed ^ (uint64_t)slot;
    unsigned i;
    memset(block, 0xff, BLOCK);
    for (i = 0; i < 12; ++i) {
        seed = seed * UINT64_C(6364136223846793005) +
               UINT64_C(1442695040888963407);
        block[i] = (uint8_t)(seed >> 56);
    }
    put_be64(block + 0x0c, id->time);
    put_be32(block + 0x14, 0);              /* RTC bias */
    put_be32(block + 0x18, 0);              /* language: English */
    put_be32(block + 0x1c, 0);              /* DTV status */
    put_be16(block + 0x20, (uint16_t)slot);
    put_be16(block + 0x22, g->mbits);
    put_be16(block + 0x24, g->encoding);    /* 0 Windows-1252, 1 Shift-JIS */
    checksum(block, 0x1fc, block + 0x1fc);
}

static void fill_directory(uint8_t *block)
{
    memset(block, 0xff, BLOCK);
    put_be16(block + 0x1ffa, 0);            /* update counter */
    checksum(block, 0x1ffc, block + 0x1ffc);
}

static void fill_bat(uint8_t *block, const struct cubeboot_vmc_geometry *g)
{
    memset(block, 0, BLOCK);
    put_be16(block + 0x04, 0);                              /* update counter */
    put_be16(block + 0x06, (uint16_t)(CUBEBOOT_VMC_BLOCKS_FOR(g->mbits) -
                                      CUBEBOOT_VMC_METADATA_BLOCKS));
    put_be16(block + 0x08, 4);                              /* last allocated */
    checksum(block + 4, BLOCK - 4, block);
}

static int write_formatted(const struct cubeboot_vmc_store *store,
                           const char *path, unsigned slot,
                           const struct cubeboot_vmc_identity *id,
                           const struct cubeboot_vmc_geometry *g)
{
    uint8_t block[BLOCK];
    uint32_t off = 0, i;

#define PUT() do { \
        if (store->write(store->context, path, off, block, BLOCK) != 0) \
            return CUBEBOOT_VMC_ERR_IO; \
        off += BLOCK; } while (0)
    fill_header(block, slot, id, g); PUT();
    fill_directory(block); PUT(); PUT();
    fill_bat(block, g); PUT(); PUT();
    memset(block, 0xff, BLOCK);
    {   /* Fast path: write the LAST block first.  A file system that lets a write land past the end of the file (FatFs behind the FlippyDrive
           API) allocates the space in between without writing it, so a 16 MiB card costs six writes instead of 2048 (seconds instead of a
           minute on the console).  The data blocks are free in the block map, their content never matters.  A store that insists on writing in
           order (the RAM store of the host tests) refuses, and the plain loop below does the job. */
        uint32_t total = CUBEBOOT_VMC_BLOCKS_FOR(g->mbits) * BLOCK, sz = 0;
        if (total > off + BLOCK && store->write(store->context, path, total - BLOCK, block, BLOCK) == 0 &&
            store->size(store->context, path, &sz) == 0 && sz == total)
            return CUBEBOOT_VMC_OK;
    }
    for (i = CUBEBOOT_VMC_METADATA_BLOCKS; i < CUBEBOOT_VMC_BLOCKS_FOR(g->mbits); ++i)
        PUT();
#undef PUT
    return CUBEBOOT_VMC_OK;
}

int cubeboot_vmc_format(const struct cubeboot_vmc_store *store,
                        const char *path, unsigned slot,
                        const struct cubeboot_vmc_identity *identity,
                        const struct cubeboot_vmc_geometry *geometry)
{
    static const struct cubeboot_vmc_geometry dflt = CUBEBOOT_VMC_GEOMETRY_DEFAULT;
    const struct cubeboot_vmc_geometry *g = geometry ? geometry : &dflt;
    char tmp[CUBEBOOT_VMC_PATH_MAX];
    uint32_t size;
    int r;

    if (!cubeboot_vmc_mbits_valid(g->mbits) || g->encoding > CUBEBOOT_VMC_ENC_SHIFT_JIS)
        return CUBEBOOT_VMC_ERR_SIZE;
    r = tmp_path(tmp, path);
    if (r != CUBEBOOT_VMC_OK) return r;
    if (store->size(store->context, path, &size) == CUBEBOOT_VMC_OK)
        return CUBEBOOT_VMC_ERR_EXISTS;

    store->remove(store->context, tmp);     /* stale tmp from a power cut */
    r = store->create(store->context, tmp);
    if (r != CUBEBOOT_VMC_OK) return r;
    r = write_formatted(store, tmp, slot & 1u, identity, g);
    if (r == CUBEBOOT_VMC_OK && store->flush &&
        store->flush(store->context, tmp) != 0)
        r = CUBEBOOT_VMC_ERR_IO;
    if (r == CUBEBOOT_VMC_OK) r = cubeboot_vmc_validate(store, tmp);
    if (r == CUBEBOOT_VMC_OK)
        r = store->rename(store->context, tmp, path) == 0
            ? CUBEBOOT_VMC_OK : CUBEBOOT_VMC_ERR_IO;
    if (r != CUBEBOOT_VMC_OK) store->remove(store->context, tmp);
    return r;
}

/* ---- card -------------------------------------------------------------- */

static void cache_drop(struct cubeboot_vmc_card *c) { c->cache_valid = 0; }

/* Reads through a one-sector cache; failures read as 0xff and latch io_error. */
static int card_read(struct cubeboot_vmc_card *c, uint32_t addr,
                     uint8_t *buf, uint32_t len)
{
    if (addr > c->image_bytes || len > c->image_bytes - addr) {
        memset(buf, 0xff, len);
        return CUBEBOOT_VMC_ERR_STATE;
    }
    if (len > CUBEBOOT_VMC_SECTOR_BYTES) {
        if (c->store->read(c->store->context, c->path, addr, buf, len) != 0) {
            memset(buf, 0xff, len);
            c->io_error = 1;
            if (c->trace) c->trace(c, 'e', addr, len, CUBEBOOT_VMC_ERR_IO, 0);
            return CUBEBOOT_VMC_ERR_IO;
        }
        return CUBEBOOT_VMC_OK;
    }
    while (len != 0) {
        uint32_t base = addr & ~(CUBEBOOT_VMC_SECTOR_BYTES - 1u);
        uint32_t off = addr - base;
        uint32_t n = CUBEBOOT_VMC_SECTOR_BYTES - off;
        if (n > len) n = len;
        if (!c->cache_valid || c->cache_base != base) {
            if (c->store->read(c->store->context, c->path, base, c->cache,
                               CUBEBOOT_VMC_SECTOR_BYTES) != 0) {
                memset(buf, 0xff, len);
                c->io_error = 1;
                return CUBEBOOT_VMC_ERR_IO;
            }
            c->cache_base = base;
            c->cache_valid = 1;
        }
        memcpy(buf, c->cache + off, n);
        buf += n; addr += n; len -= n;
    }
    return CUBEBOOT_VMC_OK;
}

static int card_write(struct cubeboot_vmc_card *c, uint32_t addr,
                      const uint8_t *buf, uint32_t len)
{
    cache_drop(c);
    if (addr > c->image_bytes || len > c->image_bytes - addr)
        return CUBEBOOT_VMC_ERR_STATE;
    if (c->store->write(c->store->context, c->path, addr, buf, len) != 0) {
        c->io_error = 1;
        return CUBEBOOT_VMC_ERR_IO;
    }
    return CUBEBOOT_VMC_OK;
}

static int card_flush(struct cubeboot_vmc_card *c)
{
    if (c->store->flush &&
        c->store->flush(c->store->context, c->path) != 0) {
        c->io_error = 1;
        return CUBEBOOT_VMC_ERR_IO;
    }
    return CUBEBOOT_VMC_OK;
}

static int card_erase(struct cubeboot_vmc_card *c, uint32_t addr, uint32_t len)
{
    uint8_t ff[CUBEBOOT_VMC_SECTOR_BYTES];
    memset(ff, 0xff, sizeof(ff));
    while (len != 0) {
        int r = card_write(c, addr, ff, CUBEBOOT_VMC_SECTOR_BYTES);
        if (r != CUBEBOOT_VMC_OK) return r;
        addr += CUBEBOOT_VMC_SECTOR_BYTES;
        len -= CUBEBOOT_VMC_SECTOR_BYTES;
    }
    return CUBEBOOT_VMC_OK;
}

int cubeboot_vmc_card_open_raw(struct cubeboot_vmc_card *card,
                               const struct cubeboot_vmc_store *store,
                               const char *path, unsigned encoding)
{
    uint32_t size;
    unsigned mbits;
    int r;
    memset(card, 0, sizeof(*card));
    r = copy_path(card->path, path);
    if (r != CUBEBOOT_VMC_OK) return r;
    card->store = store;
    r = image_size(store, path, &size, &mbits);      /* the geometry from the file size: the header is not trusted */
    if (r != CUBEBOOT_VMC_OK) return r;
    card->geometry.mbits = (uint16_t)mbits;
    card->geometry.encoding = (uint16_t)(encoding > CUBEBOOT_VMC_ENC_SHIFT_JIS ? 0u : encoding);
    card->image_bytes = CUBEBOOT_VMC_BYTES_FOR(mbits);
    card->status = CUBEBOOT_VMC_ST_UNLOCKED | CUBEBOOT_VMC_ST_READY;
    return CUBEBOOT_VMC_OK;
}

int cubeboot_vmc_card_open(struct cubeboot_vmc_card *card,
                           const struct cubeboot_vmc_store *store,
                           const char *path)
{
    int r;
    memset(card, 0, sizeof(*card));
    r = copy_path(card->path, path);
    if (r != CUBEBOOT_VMC_OK) return r;
    card->store = store;
    r = cubeboot_vmc_inspect(store, path, &card->geometry);
    if (r != CUBEBOOT_VMC_OK) return r;
    card->image_bytes = CUBEBOOT_VMC_BYTES_FOR(card->geometry.mbits);
    card->status = CUBEBOOT_VMC_ST_UNLOCKED | CUBEBOOT_VMC_ST_READY;
    return CUBEBOOT_VMC_OK;
}

void cubeboot_vmc_card_select(struct cubeboot_vmc_card *c, int selected)
{
    uint32_t addr;
    int r;

    if (selected) {
        c->selected = 1;
        c->position = 0;
        c->programmed_by_dma = 0;
        return;
    }
    c->selected = 0;
    if (c->trace && c->position > 1) c->trace(c, 'M', c->address, c->position, c->status, 0);
    switch (c->command) {
    case CMD_SECTOR_ERASE:
        if (c->position > 2) {
            r = card_erase(c, (c->address & (c->image_bytes - 1u)) & ~(BLOCK - 1u),
                           BLOCK);
            if (r == CUBEBOOT_VMC_OK) r = card_flush(c);
            if (r != CUBEBOOT_VMC_OK) c->status |= CUBEBOOT_VMC_ST_ERASE_ERR;
            c->interrupt_set = 1;
        }
        break;
    case CMD_CHIP_ERASE:
        if (c->position > 2) {
            r = card_erase(c, 0, c->image_bytes);
            if (r == CUBEBOOT_VMC_OK) r = card_flush(c);
            if (r != CUBEBOOT_VMC_OK) c->status |= CUBEBOOT_VMC_ST_ERASE_ERR;
        }
        break;
    case CMD_PAGE_PROGRAM:
        if (c->position >= 5 || c->programmed_by_dma) {
            uint32_t count = c->position >= 5 ? c->position - 5 : 0;
            uint32_t i = 0;
            addr = c->address;
            while (count--) {
                r = card_write(c, addr & (c->image_bytes - 1u),
                               &c->program_buffer[i++ & 127u], 1);
                if (r != CUBEBOOT_VMC_OK) {
                    c->status |= CUBEBOOT_VMC_ST_PROGRAM_ERR;
                    break;
                }
                addr = (addr & ~0x1ffu) | ((addr + 1u) & 0x1ffu);
            }
            if (!(c->status & CUBEBOOT_VMC_ST_PROGRAM_ERR) &&
                card_flush(c) != CUBEBOOT_VMC_OK)
                c->status |= CUBEBOOT_VMC_ST_PROGRAM_ERR;
            c->interrupt_set = 1;
        }
        break;
    default:
        break;
    }
}

static void address_byte(struct cubeboot_vmc_card *c, uint8_t byte)
{
    switch (c->position) {
    case 1: c->address = (uint32_t)byte << 17; break;
    case 2: c->address |= (uint32_t)byte << 9; break;
    case 3: c->address |= (uint32_t)(byte & 3u) << 7; break;
    case 4: c->address |= byte & 0x7fu; break;
    }
}

uint8_t cubeboot_vmc_card_transfer_byte(struct cubeboot_vmc_card *c,
                                        uint8_t byte)
{
    uint8_t out = 0xff;

    if (c->position == 0) {
        c->command = byte;
        if (byte == CMD_CLEAR_STATUS) {
            c->status &= (uint8_t)~(CUBEBOOT_VMC_ST_PROGRAM_ERR |
                                    CUBEBOOT_VMC_ST_ERASE_ERR);
            c->status |= CUBEBOOT_VMC_ST_READY;
            c->interrupt_set = 0;
            c->io_error = 0;
        }
        c->position = 1;
        return 0xff;
    }

    switch (c->command) {
    case CMD_NINTENDO_ID:
        out = c->position == 1
            ? 0x80
            : (uint8_t)((uint32_t)c->geometry.mbits >>
                        (24 - (((c->position - 2) & 3u) * 8)));
        break;
    case CMD_READ_ARRAY:
        if (c->position <= 4) address_byte(c, byte);
        if (c->position > 1) {
            card_read(c, c->address & (c->image_bytes - 1u), &out, 1);
            /* From byte 9 the pointer advances but wraps within a sector. */
            if (c->position >= 9)
                c->address = (c->address & ~0x1ffu) |
                             ((c->address + 1u) & 0x1ffu);
        }
        break;
    case CMD_READ_STATUS:
        out = c->status;
        break;
    case CMD_READ_ID:
        out = c->position == 1 ? (uint8_t)(FLASH_ID >> 8)
            : (uint8_t)((c->position & 1u) ? FLASH_ID : (FLASH_ID >> 8));
        break;
    case CMD_SECTOR_ERASE:
        if (c->position <= 2) address_byte(c, byte);
        break;
    case CMD_SET_INTERRUPT:
        if (c->position == 1) c->interrupt_enable = byte;
        break;
    case CMD_PAGE_PROGRAM:
        if (c->position <= 4) address_byte(c, byte);
        if (c->position >= 5)
            c->program_buffer[(c->position - 5) & 127u] = byte;
        break;
    default:
        break;      /* chip erase and unmodelled commands: tristate 0xff */
    }
    c->position++;
    return out;
}

int cubeboot_vmc_card_dma_read(struct cubeboot_vmc_card *c, void *buffer,
                               uint32_t length)
{
    int r;
    if (c->command != CMD_READ_ARRAY || c->position < 5) {
        if (c->trace) c->trace(c, 'r', c->address, length, CUBEBOOT_VMC_ERR_STATE, 0);
        return CUBEBOOT_VMC_ERR_STATE;
    }
    r = card_read(c, c->address & (c->image_bytes - 1u), buffer, length);
    if (c->trace) c->trace(c, 'R', c->address, length, r, buffer);
    return r;
}

int cubeboot_vmc_card_dma_write(struct cubeboot_vmc_card *c, const void *buffer,
                                uint32_t length)
{
    int r;
    if (c->command != CMD_PAGE_PROGRAM || c->position < 5) {
        if (c->trace) c->trace(c, 'w', c->address, length, CUBEBOOT_VMC_ERR_STATE, 0);
        return CUBEBOOT_VMC_ERR_STATE;
    }
    r = card_write(c, c->address & (c->image_bytes - 1u), buffer, length);
    if (c->trace) c->trace(c, 'W', c->address, length, r, buffer);
    if (r != CUBEBOOT_VMC_OK) c->status |= CUBEBOOT_VMC_ST_PROGRAM_ERR;
    c->programmed_by_dma = 1;
    return r;
}

int cubeboot_vmc_card_interrupt(const struct cubeboot_vmc_card *c)
{
    return c->interrupt_enable ? c->interrupt_set : 0;
}

/* ---- port -------------------------------------------------------------- */

int cubeboot_vmc_port_init(struct cubeboot_vmc_port *port,
                           const struct cubeboot_vmc_store *store,
                           enum cubeboot_vmc_slot slot, const char *path,
                           const struct cubeboot_vmc_identity *identity,
                           const struct cubeboot_vmc_geometry *geometry)
{
    static const struct cubeboot_vmc_geometry dflt = CUBEBOOT_VMC_GEOMETRY_DEFAULT;
    int r;
    memset(port, 0, sizeof(*port));
    r = copy_path(port->path, path);
    if (r != CUBEBOOT_VMC_OK) return r;
    port->store = store;
    port->slot = slot;
    port->identity = *identity;
    port->geometry = geometry ? *geometry : dflt;
    if (!cubeboot_vmc_mbits_valid(port->geometry.mbits) ||
        port->geometry.encoding > CUBEBOOT_VMC_ENC_SHIFT_JIS)
        return CUBEBOOT_VMC_ERR_SIZE;
    port->source = CUBEBOOT_VMC_SOURCE_ERROR;   /* until first service() */
    port->last_error = CUBEBOOT_VMC_ERR_STATE;
    port->dirty = 1;
    return CUBEBOOT_VMC_OK;
}

void cubeboot_vmc_port_observe_physical(struct cubeboot_vmc_port *port,
                                        int present)
{
    present = present != 0;
    if (port->physical_present != present) {
        port->physical_present = (uint8_t)present;
        port->dirty = 1;
    }
}

void cubeboot_vmc_port_retry(struct cubeboot_vmc_port *port)
{
    port->dirty = 1;
}

static enum cubeboot_vmc_event activate(struct cubeboot_vmc_port *port)
{
    int r = cubeboot_vmc_validate(port->store, port->path);

    if (r == CUBEBOOT_VMC_ERR_MISSING) {
        port->source = CUBEBOOT_VMC_SOURCE_CREATING;
        r = cubeboot_vmc_format(port->store, port->path, (unsigned)port->slot,
                                &port->identity, &port->geometry);
        if (r == CUBEBOOT_VMC_OK)
            r = cubeboot_vmc_validate(port->store, port->path);
    }
    if (r == CUBEBOOT_VMC_OK)
        r = cubeboot_vmc_card_open(&port->card, port->store, port->path);
    else if (r == CUBEBOOT_VMC_ERR_CORRUPT) {
        /* A standard-size image with a bad header, directory or block map is served as it is, like a real card gone bad: the
           console's CARDMount reports it broken and the IPL (or the game) offers its own Format, which rewrites the file. */
        r = cubeboot_vmc_card_open_raw(&port->card, port->store, port->path, port->geometry.encoding);
        if (r == CUBEBOOT_VMC_OK) {
            port->source = CUBEBOOT_VMC_SOURCE_VIRTUAL;
            port->last_error = CUBEBOOT_VMC_ERR_CORRUPT;      /* served, but unformatted: for the owner's status line */
            port->encoding_mismatch = 0;
            return CUBEBOOT_VMC_EVENT_VIRTUAL_INSERTED;
        }
    }
    if (r != CUBEBOOT_VMC_OK) {
        /* Present-but-bad images (short, unreadable) are left untouched. */
        port->source = CUBEBOOT_VMC_SOURCE_ERROR;
        port->last_error = r;
        return CUBEBOOT_VMC_EVENT_NONE;
    }
    port->source = CUBEBOOT_VMC_SOURCE_VIRTUAL;
    port->last_error = CUBEBOOT_VMC_OK;
    port->encoding_mismatch = port->card.geometry.encoding != port->geometry.encoding;
    return CUBEBOOT_VMC_EVENT_VIRTUAL_INSERTED;
}

enum cubeboot_vmc_event cubeboot_vmc_port_service(
    struct cubeboot_vmc_port *port)
{
    if (port->source == CUBEBOOT_VMC_SOURCE_VIRTUAL && port->card.selected)
        return CUBEBOOT_VMC_EVENT_NONE;     /* never switch mid-request */
    if (!port->dirty)
        return CUBEBOOT_VMC_EVENT_NONE;
    port->dirty = 0;

    if (port->physical_present) {
        int was_virtual = port->source == CUBEBOOT_VMC_SOURCE_VIRTUAL;
        port->source = CUBEBOOT_VMC_SOURCE_PHYSICAL;
        port->last_error = CUBEBOOT_VMC_OK;
        return was_virtual ? CUBEBOOT_VMC_EVENT_VIRTUAL_REMOVED
                           : CUBEBOOT_VMC_EVENT_NONE;
    }
    if (port->source == CUBEBOOT_VMC_SOURCE_VIRTUAL)
        return CUBEBOOT_VMC_EVENT_NONE;
    return activate(port);
}

struct cubeboot_vmc_card *cubeboot_vmc_port_card(struct cubeboot_vmc_port *port)
{
    return port->source == CUBEBOOT_VMC_SOURCE_VIRTUAL ? &port->card : NULL;
}
