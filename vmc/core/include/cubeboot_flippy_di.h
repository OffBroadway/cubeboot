/* Copyright (C) 2026 Nigzel / Bjerreman. SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef CUBEBOOT_VMC_FLIPPY_DI_H
#define CUBEBOOT_VMC_FLIPPY_DI_H

/*
 * DI transport for the FlippyDrive file API.
 *
 *   regs   - programs the DI register block for one command and polls it to
 *            completion. Register access, address translation and cache
 *            maintenance go through hooks, so the same code drives the real
 *            block (cubeboot_flippy_di_console_init, PowerPC only) or a model
 *            on the host.
 *   queue  - one command on the wire at a time, FIFO, with completion
 *            callbacks. The resident stage pushes the game's own DI commands
 *            through it as well, so the store never talks over a game disc
 *            read. An "exclusive" section keeps a multi-command file
 *            operation (open + status read) together, because the status
 *            record belongs to the preceding command.
 *   sync   - adapts the queue to struct cubeboot_flippy_transport: push, then
 *            pump the hardware until that command finishes or a poll bound
 *            is exceeded.
 *
 * No interrupts: completion is polled (DISR/DICR) with the DI interrupt
 * masks left off, so it works both under a game that owns the DI interrupt
 * and from the resident stage's exception context.
 */

#include "cubeboot_flippy_api.h"

/* DI register block, word indexes from the base (0xCC006000 on a console). */
enum {
    CUBEBOOT_DI_SR = 0, CUBEBOOT_DI_CVR = 1, CUBEBOOT_DI_CMD0 = 2,
    CUBEBOOT_DI_CMD1 = 3, CUBEBOOT_DI_CMD2 = 4, CUBEBOOT_DI_MAR = 5,
    CUBEBOOT_DI_LENGTH = 6, CUBEBOOT_DI_CR = 7, CUBEBOOT_DI_IMM = 8,
};
#define CUBEBOOT_DI_BASE        0xcc006000u

/* DISR: masks are read/write, interrupt flags are write-1-to-clear. */
#define CUBEBOOT_DISR_DEINTMASK 0x02u
#define CUBEBOOT_DISR_DEINT     0x04u
#define CUBEBOOT_DISR_TCINTMASK 0x08u
#define CUBEBOOT_DISR_TCINT     0x10u
#define CUBEBOOT_DISR_BRKMASK   0x20u
#define CUBEBOOT_DISR_BRKINT    0x40u
#define CUBEBOOT_DISR_CLEAR_ALL (CUBEBOOT_DISR_DEINT | CUBEBOOT_DISR_TCINT | CUBEBOOT_DISR_BRKINT)
#define CUBEBOOT_DISR_MASKS     (CUBEBOOT_DISR_DEINTMASK | CUBEBOOT_DISR_TCINTMASK | CUBEBOOT_DISR_BRKMASK)

/* DICR */
#define CUBEBOOT_DICR_TSTART    0x01u
#define CUBEBOOT_DICR_DMA       0x02u
#define CUBEBOOT_DICR_WRITE     0x04u    /* DMA direction: 1 = memory -> drive */

enum cubeboot_flippy_poll {
    CUBEBOOT_FLIPPY_BUSY = 0,
    CUBEBOOT_FLIPPY_DONE = 1,
    CUBEBOOT_FLIPPY_FAILED = -1,
};

/* ---- regs --------------------------------------------------------------- */

struct cubeboot_flippy_di_regs {
    void *context;
    uint32_t (*read)(void *context, unsigned index);
    void (*write)(void *context, unsigned index, uint32_t value);
    /* Physical address for MAR (NULL: low 29 bits of the pointer). */
    uint32_t (*physical)(void *context, const void *address);
    /* Cache maintenance (may be NULL on a cache-coherent/uncached buffer). */
    void (*flush)(void *context, const void *address, uint32_t length);
    void (*invalidate)(void *context, void *address, uint32_t length);
    /* In-flight transfer, for post-DMA invalidation. */
    enum cubeboot_flippy_dir pending_dir;
    void *pending_buf;
    uint32_t pending_len;
    uint8_t started;
    /* preserve = 1 (the resident store, which runs inside a game's DSI trap while the game's own DVD driver owns the block): the game's DISR
       interrupt masks, its pending flags, the cover register and its last command/DMA registers are left exactly as found.  Our command is
       started and polled without touching the masks (the old behaviour wrote the masks to zero: the game then never saw another DI interrupt
       and ended on the 'An error has occurred' screen, console run 8), flags that were clear before are cleared again afterwards so the
       game gets no interrupt for our command, and flags that were set (its own pending completion) stay set. */
    uint8_t preserve;
    uint32_t sv_flags, sv_cmd[3], sv_mar, sv_len, sv_cr;
};

/* Program and start one command. 0; CUBEBOOT_FLIPPY_START_BUSY (1) if the block is still busy (a game command in flight: retry later); -1 for bad arguments.
   Console run 10 (2026-10-10): treating busy as a failure made card reads fail with -1 whenever the game had a disc read in flight, and the model then handed the game a constant buffer. */
#define CUBEBOOT_FLIPPY_START_BUSY 1
int cubeboot_flippy_di_start(struct cubeboot_flippy_di_regs *regs,
                             const uint32_t cmd[3],
                             enum cubeboot_flippy_dir dir, void *buf,
                             uint32_t length);
enum cubeboot_flippy_poll cubeboot_flippy_di_poll(
    struct cubeboot_flippy_di_regs *regs);

#if defined(__PPC__) || defined(__powerpc__)
/* Real register block at CUBEBOOT_DI_BASE with dcbf/dcbi cache maintenance. */
void cubeboot_flippy_di_console_init(struct cubeboot_flippy_di_regs *regs);
#endif

/* ---- queue -------------------------------------------------------------- */

/* One slot is reserved for the store: game pushes are refused once only that
 * slot is left. Otherwise a queue full of game commands (which an exclusive
 * section holds back) would leave the store nowhere to put its command. */
#define CUBEBOOT_FLIPPY_QUEUE_DEPTH 4u

struct cubeboot_flippy_job;
typedef void (*cubeboot_flippy_done)(void *arg, int error);

struct cubeboot_flippy_hw {
    void *context;
    int (*start)(void *context, const struct cubeboot_flippy_job *job);
    enum cubeboot_flippy_poll (*poll)(void *context);
    /* optional: one whole synchronous command (start .. done) runs between lock and unlock.  The console binding turns external
       interrupts off there, so a caller on a thread (the IPL menu) cannot have its transfer interleaved with the drive polling of the
       system's own DVD thread (NULL: no locking, the host). */
    uint32_t (*lock)(void *context);
    void (*unlock)(void *context, uint32_t key);
};

enum cubeboot_flippy_job_state { JOB_FREE = 0, JOB_QUEUED, JOB_RUNNING };

struct cubeboot_flippy_job {
    uint32_t cmd[3];
    enum cubeboot_flippy_dir dir;
    void *buf;
    uint32_t length;
    cubeboot_flippy_done done;
    void *arg;
    uint32_t seq;
    uint8_t state;
    uint8_t exclusive;     /* issued by the store inside an exclusive section */
    uint8_t abandoned;     /* waiter gave up; retire silently */
};

struct cubeboot_flippy_queue {
    struct cubeboot_flippy_hw hw;
    struct cubeboot_flippy_job jobs[CUBEBOOT_FLIPPY_QUEUE_DEPTH];
    uint32_t next_seq;
    uint8_t pumping;
    unsigned exclusive_depth;
};

/* Hardware ops that drive a cubeboot_flippy_di_regs. */
void cubeboot_flippy_hw_from_regs(struct cubeboot_flippy_hw *hw,
                                  struct cubeboot_flippy_di_regs *regs);

void cubeboot_flippy_queue_init(struct cubeboot_flippy_queue *q,
                                const struct cubeboot_flippy_hw *hw);

/* Enqueue a command (e.g. one of the game's). 0, or -1 if the queue is full
 * (QUEUE_DEPTH - 1 non-store commands at most).
 * `done` runs from cubeboot_flippy_queue_pump(), never from push. */
int cubeboot_flippy_queue_push(struct cubeboot_flippy_queue *q,
                               const uint32_t cmd[3],
                               enum cubeboot_flippy_dir dir, void *buf,
                               uint32_t length, cubeboot_flippy_done done,
                               void *arg);

/* Retire finished work and start the next eligible command. Never blocks. */
void cubeboot_flippy_queue_pump(struct cubeboot_flippy_queue *q);

/* While exclusive, only exclusive (store) commands are started; others wait. */
void cubeboot_flippy_queue_begin_exclusive(struct cubeboot_flippy_queue *q);
void cubeboot_flippy_queue_end_exclusive(struct cubeboot_flippy_queue *q);

#define CUBEBOOT_FLIPPY_TIMEOUT (-2)

/* ---- sync transport ----------------------------------------------------- */

struct cubeboot_flippy_sync {
    struct cubeboot_flippy_queue *queue;
    uint32_t max_polls;          /* pump iterations before giving up */
};

/* Fills a transport that blocks (polling) on the queue. */
void cubeboot_flippy_sync_transport(struct cubeboot_flippy_sync *sync,
                                    struct cubeboot_flippy_queue *queue,
                                    uint32_t max_polls,
                                    struct cubeboot_flippy_transport *out);

#endif
