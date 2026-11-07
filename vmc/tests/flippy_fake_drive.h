/* Copyright (C) 2026 Nigzel / Bjerreman. SPDX-License-Identifier: GPL-2.0-only */
/*
 * Strict simulated FlippyDrive for host tests (header-only, include once per
 * test). Enforces the DI transport rules, logs every command, and can behave
 * like a drive that does not create on open or does not grow files on write.
 */
#ifndef CUBEBOOT_VMC_TEST_FLIPPY_FAKE_DRIVE_H
#define CUBEBOOT_VMC_TEST_FLIPPY_FAKE_DRIVE_H

#include <assert.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include "cubeboot_flippy_api.h"

struct fake {
    char root[200];
    int create_on_write_open;
    int extend_on_write;
    int fd[32];                 /* handle -> fd (index 1..31) */
    uint8_t wflag[32];
    uint8_t status[32];
    unsigned violations;        /* transport-rule breaches */
    unsigned forbidden;         /* commands outside the allowed set */
    unsigned closes, opens, writes, reads;
    int fail_after;             /* >=0: fail every command once this hits 0 */
    unsigned game_cmds;         /* game DI reads (0xA8) served */
    unsigned inquiries;
    unsigned disc_sets, disc_fd1, disc_fd2;   /* 0xB5 <fd1> <fd2> 0x01 seen */
    unsigned interleaved;       /* a command slipped between an op and its status read */
    int expect_status;
};

static uint32_t rd32(const uint8_t *p)
{ return ((uint32_t)p[0] << 24) | (p[1] << 16) | (p[2] << 8) | p[3]; }
static void wr32(uint8_t *p, uint32_t v)
{ p[0] = v >> 24; p[1] = v >> 16; p[2] = v >> 8; p[3] = v; }
static void wr64(uint8_t *p, uint64_t v) { wr32(p, v >> 32); wr32(p + 4, (uint32_t)v); }

static void host_path(struct fake *f, const uint8_t *rec, char *out, size_t n)
{
    const char *name = (const char *)rec;
    assert(rec[255] == 0);
    snprintf(out, n, "%s/%s", f->root, name[0] == '/' ? name + 1 : name);
}

/* The status record's result (u32 @0) and size (u64 @4) are LITTLE-endian on the real drive (RP2040 native; upstream Cubeboot byte-swaps fsize;
   confirmed by the console probe run 1: a 16384-byte file read as 0x0040000000000000 big-endian). fd is one byte @12. The fake does the same. */
static void set_status(struct fake *f, uint32_t result, uint64_t size, unsigned handle)
{
    int i;
    memset(f->status, 0, 32);
    for (i = 0; i < 4; ++i) f->status[i] = (uint8_t)(result >> (8 * i));
    for (i = 0; i < 8; ++i) f->status[4 + i] = (uint8_t)(size >> (8 * i));
    f->status[12] = (uint8_t)handle;
}

enum { FR_GOOD = 0, FR_NOFILE = 4, FR_NOPATH = 5, FR_EXISTS = 8, FR_DENIED = 7 };

static int fake_command(void *ctx, const uint32_t cmd[3],
                        enum cubeboot_flippy_dir dir, void *buf, uint32_t len)
{
    struct fake *f = ctx;
    unsigned sub = cmd[0] & 0xff, h = (cmd[0] >> 16) & 0xff;
    char path[512], path2[512];
    struct stat st;

    if (f->fail_after == 0) return -1;
    if (f->fail_after > 0) f->fail_after--;
    if (f->expect_status && !((cmd[0] >> 24) == 0xb5 && sub == 0x00))
        f->interleaved++;
    if ((cmd[0] >> 24) == 0xa8) {                  /* a game's own disc read */
        if (dir != CUBEBOOT_FLIPPY_FROM_DRIVE || ((uintptr_t)buf & 31) || (len & 31)) {
            f->violations++; return -1;
        }
        memset(buf, 0xa8, len);
        f->game_cmds++;
        return 0;
    }
    if ((cmd[0] >> 24) == 0x12) {                  /* DI inquiry only */
        uint8_t *o = buf;
        if (cmd[0] != 0x12000000u || cmd[1] != 0 || cmd[2] != 32 ||
            dir != CUBEBOOT_FLIPPY_FROM_DRIVE || len != 32) {
            f->forbidden++;                        /* e.g. boot/update magic */
            return -1;
        }
        memset(o, 0, 32);
        wr32(o + 4, 0x20220426u);
        o[12] = 1; o[13] = 4; o[14] = 0; o[15] = (uint8_t)(2 << 1);
        f->inquiries++;
        return 0;
    }
    if ((cmd[0] >> 24) != 0xb5) { f->forbidden++; return -1; }
    f->expect_status = (sub == 0x0a || sub == 0x0b || sub == 0x07 ||
                        sub == 0x0e || sub == 0x13);
    switch (sub) {
    case 0x00: case 0x01: case 0x07: case 0x08: case 0x09: case 0x0a: case 0x0b:
    case 0x0e: case 0x13: break;
    default: f->forbidden++; return -1;      /* flash/reset/mount/bypass/... */
    }
    if (dir != CUBEBOOT_FLIPPY_NO_DATA) {
        if (((uintptr_t)buf & 31) || len == 0 || (len & 31)) { f->violations++; return -1; }
    }

    switch (sub) {
    case 0x01:                                      /* serve file fd1 (and fd2) as the disc */
        if (dir != CUBEBOOT_FLIPPY_NO_DATA || (cmd[0] >> 16 & 0xff) < 1 || (cmd[0] >> 16 & 0xff) > 31 || !f->fd[cmd[0] >> 16 & 0xff]) return -1;
        f->disc_sets++; f->disc_fd1 = cmd[0] >> 16 & 0xff; f->disc_fd2 = cmd[0] >> 8 & 0xff;
        return 0;
    case 0x00:
        assert(dir == CUBEBOOT_FLIPPY_FROM_DRIVE && len == 32);
        memcpy(buf, f->status, 32);
        return 0;
    case 0x0a: {                                   /* open */
        const uint8_t *rec = buf;
        unsigned flags = rec[257], hh;
        int fd, oflags;
        assert(dir == CUBEBOOT_FLIPPY_TO_DRIVE && len == 288 && rec[256] == 0);
        host_path(f, rec, path, sizeof(path));
        oflags = (flags & 8) ? O_RDWR : O_RDONLY;
        if ((flags & 8) && f->create_on_write_open) oflags |= O_CREAT;
        fd = open(path, oflags, 0644);
        if (fd < 0) { set_status(f, FR_NOFILE, 0, 0); return 0; }
        for (hh = 1; hh <= 31 && f->fd[hh]; ++hh) {}
        assert(hh <= 31);
        f->fd[hh] = fd; f->wflag[hh] = (flags & 8) != 0;
        fstat(fd, &st);
        set_status(f, FR_GOOD, (uint64_t)st.st_size, hh);
        f->opens++;
        return 0;
    }
    case 0x0b:                                      /* close */
        if (h < 1 || h > 31 || !f->fd[h]) { set_status(f, 9, 0, h); return 0; }
        close(f->fd[h]); f->fd[h] = 0; f->closes++;
        set_status(f, FR_GOOD, 0, h);
        return 0;
    case 0x08: {                                    /* read */
        uint32_t off = cmd[1], n = cmd[2];
        assert(h >= 1 && h <= 31 && f->fd[h]);
        fstat(f->fd[h], &st);
        if (n != len || (uint64_t)off + n > (uint64_t)st.st_size) { f->violations++; return -1; }
        f->reads++;
        return pread(f->fd[h], buf, n, off) == (ssize_t)n ? 0 : -1;
    }
    case 0x09: {                                    /* write */
        uint32_t off = cmd[1], n = cmd[2];
        assert(h >= 1 && h <= 31 && f->fd[h]);
        if (!f->wflag[h]) return -1;
        if (n > 16384 - 32 || len != ((n ? n : 1) + 31u & ~31u)) { f->violations++; return -1; }
        fstat(f->fd[h], &st);
        if (!f->extend_on_write && (uint64_t)off + n > (uint64_t)st.st_size) return -1;
        f->writes++;
        return pwrite(f->fd[h], buf, n, off) == (ssize_t)n ? 0 : -1;
    }
    case 0x07:                                      /* mkdir */
        host_path(f, buf, path, sizeof(path));
        if (mkdir(path, 0755) == 0) set_status(f, FR_GOOD, 0, 0);
        else set_status(f, stat(path, &st) == 0 ? FR_EXISTS : FR_NOPATH, 0, 0);
        return 0;
    case 0x0e:                                      /* unlink */
        host_path(f, buf, path, sizeof(path));
        set_status(f, unlink(path) == 0 ? FR_GOOD : FR_NOFILE, 0, 0);
        return 0;
    case 0x13:                                      /* rename */
        assert(len == 576);
        host_path(f, buf, path, sizeof(path));
        host_path(f, (const uint8_t *)buf + 288, path2, sizeof(path2));
        if (stat(path, &st) != 0) set_status(f, FR_NOFILE, 0, 0);
        else if (stat(path2, &st) == 0) set_status(f, FR_EXISTS, 0, 0);
        else set_status(f, rename(path, path2) == 0 ? FR_GOOD : FR_NOPATH, 0, 0);
        return 0;
    }
    return -1;
}


static void fake_new_root(struct fake *f, int create, int extend)
{
    const char *tmp = getenv("TMPDIR");
    memset(f, 0, sizeof(*f));
    f->fail_after = -1;
    snprintf(f->root, sizeof(f->root), "%s/cubeboot-flippy-XXXXXX", tmp ? tmp : "/tmp");
    assert(mkdtemp(f->root));
    f->create_on_write_open = create;
    f->extend_on_write = extend;
}

static void fake_clean(struct fake *f)
{
    char cmd[300];
    snprintf(cmd, sizeof(cmd), "rm -rf '%s'", f->root);
    if (system(cmd) != 0) {}
}

#endif
