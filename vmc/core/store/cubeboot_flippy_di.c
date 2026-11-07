/* Copyright (C) 2026 Nigzel / Bjerreman. SPDX-License-Identifier: GPL-3.0-or-later */
/* DI transport (register driver, command queue, sync adapter).
 * See include/cubeboot_flippy_di.h. */
#include <string.h>

#include "cubeboot_flippy_di.h"

typedef struct cubeboot_flippy_di_regs regs_t;
typedef struct cubeboot_flippy_queue queue_t;
typedef struct cubeboot_flippy_job job_t;

/* ---- regs --------------------------------------------------------------- */

static uint32_t physical(regs_t *r, const void *p)
{
    return r->physical ? r->physical(r->context, p)
                       : (uint32_t)(uintptr_t)p & 0x1fffffffu;
}

int cubeboot_flippy_di_start(regs_t *r, const uint32_t cmd[3],
                             enum cubeboot_flippy_dir dir, void *buf,
                             uint32_t length)
{
    uint32_t cr = CUBEBOOT_DICR_TSTART;

    if (r->started || (r->read(r->context, CUBEBOOT_DI_CR) & CUBEBOOT_DICR_TSTART))
        return CUBEBOOT_FLIPPY_START_BUSY;  /* a command (the game's own disc read, or ours) is still running: try again later, do NOT fail */
    if (dir != CUBEBOOT_FLIPPY_NO_DATA &&
        (buf == NULL || length == 0 || (length & 31u) || ((uintptr_t)buf & 31u)))
        return -1;                          /* the drive's DMA rules */

    if (r->preserve) {
        /* the block belongs to a running game: remember what it left, change nothing but our command (see the header) */
        r->sv_flags = r->read(r->context, CUBEBOOT_DI_SR) & CUBEBOOT_DISR_CLEAR_ALL;
        r->sv_cmd[0] = r->read(r->context, CUBEBOOT_DI_CMD0);
        r->sv_cmd[1] = r->read(r->context, CUBEBOOT_DI_CMD1);
        r->sv_cmd[2] = r->read(r->context, CUBEBOOT_DI_CMD2);
        r->sv_mar = r->read(r->context, CUBEBOOT_DI_MAR);
        r->sv_len = r->read(r->context, CUBEBOOT_DI_LENGTH);
        r->sv_cr = r->read(r->context, CUBEBOOT_DI_CR) & ~CUBEBOOT_DICR_TSTART;
    } else {
        /* Clear stale flags, leave every interrupt mask off: we poll. */
        r->write(r->context, CUBEBOOT_DI_SR, CUBEBOOT_DISR_CLEAR_ALL);
        r->write(r->context, CUBEBOOT_DI_CVR, 0);
    }
    r->write(r->context, CUBEBOOT_DI_CMD0, cmd[0]);
    r->write(r->context, CUBEBOOT_DI_CMD1, cmd[1]);
    r->write(r->context, CUBEBOOT_DI_CMD2, cmd[2]);

    if (dir != CUBEBOOT_FLIPPY_NO_DATA) {
        if (dir == CUBEBOOT_FLIPPY_TO_DRIVE) {
            if (r->flush) r->flush(r->context, buf, length);
            cr |= CUBEBOOT_DICR_DMA | CUBEBOOT_DICR_WRITE;
        } else {
            if (r->invalidate) r->invalidate(r->context, buf, length);
            cr |= CUBEBOOT_DICR_DMA;
        }
        r->write(r->context, CUBEBOOT_DI_MAR, physical(r, buf));
        r->write(r->context, CUBEBOOT_DI_LENGTH, length);
    }
    r->pending_dir = dir;
    r->pending_buf = buf;
    r->pending_len = length;
    r->started = 1;
    r->write(r->context, CUBEBOOT_DI_CR, cr);
    return 0;
}

enum cubeboot_flippy_poll cubeboot_flippy_di_poll(regs_t *r)
{
    uint32_t sr, cr;

    if (!r->started) return CUBEBOOT_FLIPPY_FAILED;
    sr = r->read(r->context, CUBEBOOT_DI_SR);
    cr = r->read(r->context, CUBEBOOT_DI_CR);
    if (r->preserve) {
        int failed;
        if (cr & CUBEBOOT_DICR_TSTART) return CUBEBOOT_FLIPPY_BUSY;       /* completion = the transfer bit clears; the flags may be the game's */
        sr = r->read(r->context, CUBEBOOT_DI_SR);                         /* read the flags AFTER the bit cleared: the drive sets them as it finishes */
        failed = (sr & CUBEBOOT_DISR_DEINT) && !(r->sv_flags & CUBEBOOT_DISR_DEINT);
        r->started = 0;
        /* masks written back as they are, flags that were clear before cleared again, flags that were set stay set */
        r->write(r->context, CUBEBOOT_DI_SR, (sr & CUBEBOOT_DISR_MASKS) | (sr & CUBEBOOT_DISR_CLEAR_ALL & ~r->sv_flags));
        r->write(r->context, CUBEBOOT_DI_CMD0, r->sv_cmd[0]);
        r->write(r->context, CUBEBOOT_DI_CMD1, r->sv_cmd[1]);
        r->write(r->context, CUBEBOOT_DI_CMD2, r->sv_cmd[2]);
        r->write(r->context, CUBEBOOT_DI_MAR, r->sv_mar);
        r->write(r->context, CUBEBOOT_DI_LENGTH, r->sv_len);
        r->write(r->context, CUBEBOOT_DI_CR, r->sv_cr);
        if (failed) return CUBEBOOT_FLIPPY_FAILED;
        if (r->pending_dir == CUBEBOOT_FLIPPY_FROM_DRIVE && r->invalidate)
            r->invalidate(r->context, r->pending_buf, r->pending_len);
        return CUBEBOOT_FLIPPY_DONE;
    }
    if (!(sr & (CUBEBOOT_DISR_DEINT | CUBEBOOT_DISR_TCINT)) &&
        (cr & CUBEBOOT_DICR_TSTART))
        return CUBEBOOT_FLIPPY_BUSY;

    r->started = 0;
    r->write(r->context, CUBEBOOT_DI_SR, CUBEBOOT_DISR_CLEAR_ALL);
    if (sr & CUBEBOOT_DISR_DEINT) return CUBEBOOT_FLIPPY_FAILED;
    if (r->pending_dir == CUBEBOOT_FLIPPY_FROM_DRIVE && r->invalidate)
        r->invalidate(r->context, r->pending_buf, r->pending_len);
    return CUBEBOOT_FLIPPY_DONE;
}

#if defined(__PPC__) || defined(__powerpc__)
static uint32_t console_read(void *c, unsigned i)
{
    (void)c;
    return ((volatile uint32_t *)CUBEBOOT_DI_BASE)[i];
}
static void console_write(void *c, unsigned i, uint32_t v)
{
    (void)c;
    ((volatile uint32_t *)CUBEBOOT_DI_BASE)[i] = v;
}
static void console_flush(void *c, const void *a, uint32_t n)
{
    uintptr_t p = (uintptr_t)a & ~31u, e = (uintptr_t)a + n;
    (void)c;
    for (; p < e; p += 32) __asm__ volatile("dcbf 0,%0" : : "r"(p) : "memory");
    __asm__ volatile("sync" : : : "memory");
}
static void console_invalidate(void *c, void *a, uint32_t n)
{
    uintptr_t p = (uintptr_t)a & ~31u, e = (uintptr_t)a + n;
    (void)c;
    for (; p < e; p += 32) __asm__ volatile("dcbi 0,%0" : : "r"(p) : "memory");
    __asm__ volatile("sync" : : : "memory");
}

void cubeboot_flippy_di_console_init(regs_t *r)
{
    memset(r, 0, sizeof(*r));
    r->preserve = 1;                       /* the console build always runs next to a game's or the IPL's own DVD driver */
    r->read = console_read;
    r->write = console_write;
    r->flush = console_flush;
    r->invalidate = console_invalidate;
}
#endif

/* ---- queue -------------------------------------------------------------- */

static int hw_start(void *context, const job_t *job)
{
    return cubeboot_flippy_di_start(context, job->cmd, job->dir, job->buf,
                                    job->length);
}
static enum cubeboot_flippy_poll hw_poll(void *context)
{
    return cubeboot_flippy_di_poll(context);
}

#if defined(__powerpc__) || defined(__PPC__)
static uint32_t hw_lock(void *context) { uint32_t m; (void)context; __asm__ volatile("mfmsr %0" : "=r"(m)); __asm__ volatile("mtmsr %0 ; isync" : : "r"(m & ~0x8000u) : "memory"); return m; }
static void hw_unlock(void *context, uint32_t key) { (void)context; __asm__ volatile("mtmsr %0 ; isync" : : "r"(key) : "memory"); }
#endif

void cubeboot_flippy_hw_from_regs(struct cubeboot_flippy_hw *hw, regs_t *regs)
{
    hw->context = regs;
    hw->start = hw_start;
    hw->poll = hw_poll;
#if defined(__powerpc__) || defined(__PPC__)
    hw->lock = hw_lock;
    hw->unlock = hw_unlock;
#else
    hw->lock = NULL;
    hw->unlock = NULL;
#endif
}

void cubeboot_flippy_queue_init(queue_t *q, const struct cubeboot_flippy_hw *hw)
{
    memset(q, 0, sizeof(*q));
    q->hw = *hw;
}

static int enqueue(queue_t *q, const uint32_t cmd[3],
                   enum cubeboot_flippy_dir dir, void *buf, uint32_t length,
                   cubeboot_flippy_done done, void *arg, int exclusive,
                   job_t **out)
{
    unsigned i;
    for (i = 0; i < CUBEBOOT_FLIPPY_QUEUE_DEPTH; ++i) {
        job_t *j = &q->jobs[i];
        if (j->state != JOB_FREE) continue;
        memcpy(j->cmd, cmd, sizeof(j->cmd));
        j->dir = dir;
        j->buf = buf;
        j->length = length;
        j->done = done;
        j->arg = arg;
        j->seq = q->next_seq++;
        j->exclusive = (uint8_t)exclusive;
        j->abandoned = 0;
        j->state = JOB_QUEUED;
        if (out) *out = j;
        return 0;
    }
    return -1;
}

int cubeboot_flippy_queue_push(queue_t *q, const uint32_t cmd[3],
                               enum cubeboot_flippy_dir dir, void *buf,
                               uint32_t length, cubeboot_flippy_done done,
                               void *arg)
{
    unsigned i, non_store = 0;
    for (i = 0; i < CUBEBOOT_FLIPPY_QUEUE_DEPTH; ++i)
        if (q->jobs[i].state != JOB_FREE && !q->jobs[i].exclusive) non_store++;
    if (non_store >= CUBEBOOT_FLIPPY_QUEUE_DEPTH - 1) return -1;   /* slot reserved */
    return enqueue(q, cmd, dir, buf, length, done, arg, 0, NULL);
}

void cubeboot_flippy_queue_begin_exclusive(queue_t *q) { q->exclusive_depth++; }
void cubeboot_flippy_queue_end_exclusive(queue_t *q)
{
    if (q->exclusive_depth) q->exclusive_depth--;
}

static job_t *running(queue_t *q)
{
    unsigned i;
    for (i = 0; i < CUBEBOOT_FLIPPY_QUEUE_DEPTH; ++i)
        if (q->jobs[i].state == JOB_RUNNING) return &q->jobs[i];
    return NULL;
}

/* Oldest queued job allowed to start now. */
static job_t *next_eligible(queue_t *q)
{
    job_t *best = NULL;
    unsigned i;
    for (i = 0; i < CUBEBOOT_FLIPPY_QUEUE_DEPTH; ++i) {
        job_t *j = &q->jobs[i];
        if (j->state != JOB_QUEUED) continue;
        if (q->exclusive_depth && !j->exclusive) continue;
        if (!best || (int32_t)(j->seq - best->seq) < 0) best = j;
    }
    return best;
}

static void retire(job_t *j, int error)
{
    cubeboot_flippy_done done = j->done;
    void *arg = j->arg;
    int skip = j->abandoned;
    j->state = JOB_FREE;                /* free first: callbacks may push */
    if (done && !skip) done(arg, error);
}

void cubeboot_flippy_queue_pump(queue_t *q)
{
    if (q->pumping) return;             /* a callback re-entered; outer loop continues */
    q->pumping = 1;
    for (;;) {
        job_t *j = running(q);
        if (j) {
            enum cubeboot_flippy_poll p = q->hw.poll(q->hw.context);
            if (p == CUBEBOOT_FLIPPY_BUSY) break;
            retire(j, p == CUBEBOOT_FLIPPY_DONE ? 0 : -1);
            continue;
        }
        j = next_eligible(q);
        if (!j) break;
        {
            int rc = q->hw.start(q->hw.context, j);
            if (rc > 0) break;              /* the block is busy (the game is reading its disc): the job stays queued and starts at a later pump */
            if (rc != 0) {
                retire(j, -1);
                continue;
            }
        }
        j->state = JOB_RUNNING;
    }
    q->pumping = 0;
}

/* ---- sync transport ------------------------------------------------------ */

struct sync_wait {
    int finished;
    int error;
};

static void sync_done(void *arg, int error)
{
    struct sync_wait *w = arg;
    w->finished = 1;
    w->error = error;
}

static int sync_command_unlocked(void *context, const uint32_t cmd[3],
                                 enum cubeboot_flippy_dir dir, void *buf, uint32_t length);
static int sync_command(void *context, const uint32_t cmd[3],
                        enum cubeboot_flippy_dir dir, void *buf, uint32_t length)
{
    struct cubeboot_flippy_sync *s = context;
    const struct cubeboot_flippy_hw *hw = &s->queue->hw;
    uint32_t key = 0;
    int r;
    if (hw->lock) key = hw->lock(hw->context);
    r = sync_command_unlocked(context, cmd, dir, buf, length);
    if (hw->unlock) hw->unlock(hw->context, key);
    return r;
}
static int sync_command_unlocked(void *context, const uint32_t cmd[3],
                                 enum cubeboot_flippy_dir dir, void *buf, uint32_t length)
{
    struct cubeboot_flippy_sync *s = context;
    struct sync_wait wait = { 0, 0 };
    job_t *job = NULL;
    uint32_t polls;

    /* The game may be keeping every slot busy: pump until one frees up. */
    for (polls = 0; enqueue(s->queue, cmd, dir, buf, length, sync_done, &wait,
                            1, &job) != 0; ++polls) {
        if (polls >= s->max_polls) return CUBEBOOT_FLIPPY_TIMEOUT;
        cubeboot_flippy_queue_pump(s->queue);
    }
    for (; polls < s->max_polls; ++polls) {
        cubeboot_flippy_queue_pump(s->queue);
        if (wait.finished) return wait.error ? -1 : 0;
    }
    /* Give up. A command that never started is cancelled so it cannot run
     * later; one already on the wire must retire silently (its callback
     * would write to this dead stack frame). */
    if (job->state == JOB_QUEUED) job->state = JOB_FREE;
    else job->abandoned = 1;
    return CUBEBOOT_FLIPPY_TIMEOUT;
}

static void sync_begin(void *context)
{
    cubeboot_flippy_queue_begin_exclusive(
        ((struct cubeboot_flippy_sync *)context)->queue);
}
static void sync_end(void *context)
{
    cubeboot_flippy_queue_end_exclusive(
        ((struct cubeboot_flippy_sync *)context)->queue);
}

void cubeboot_flippy_sync_transport(struct cubeboot_flippy_sync *sync,
                                    queue_t *queue, uint32_t max_polls,
                                    struct cubeboot_flippy_transport *out)
{
    sync->queue = queue;
    sync->max_polls = max_polls;
    out->context = sync;
    out->command = sync_command;
    out->begin = sync_begin;
    out->end = sync_end;
}
