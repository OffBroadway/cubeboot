/* Copyright (C) 2026 Nigzel / Bjerreman. SPDX-License-Identifier: GPL-2.0-only */
/* cbvmc_vec: DSI entry of the cubeboot_vmc runtime.  Original work (PowerPC architecture manual only).

   Flow.  The installer replaces the FIRST word of the SDK's DSI exception stub at 0x80000300 (`mtsprg 0,r4`) with
   `ba cbvmc_vec`.  The CPU enters at the vector in real mode (IR=DR=0), EE=0, SRR0/SRR1 = the faulting instruction and MSR.
   1. cbvmc_vec (real mode, only r0 is used, CR is saved before the compare): if DAR is not in the 0x0C00xxxx hardware page this is somebody else's fault:
      restore r0 and go to cbvmc_chain, which executes the replaced first word and continues at 0x80000304, so the SDK's stub
      runs exactly as if nothing had been installed.  Otherwise save r0, r1, SRR0, SRR1 in the context and `rfi` into
      virtual mode (IR|DR|RI, EE still 0) at cbvmc_dsi_entry.
   2. cbvmc_dsi_entry (virtual): save r2..r31, DAR, DSISR, LR, CTR, CR, XER; switch to the handler stack (high reserve, cbvmc_sp); call cbvmc_dsi_c
      (cbvmc_blob.c); restore everything; `rfi` back to the game with SRR0 advanced by the C code.
   No SDK state is touched: the OS context, SPRGs and the game's stack are never used.  Interrupts stay off for the whole
   handler (EE=0), so it cannot be re-entered; the C code never touches 0x0C00xxxx, only 0xCC00xxxx.
   Absolute addressing with rA=0 below 0x8000 is used in real mode: the blob's state sits at physical 0xC00..0x2FFF. */
        .set    CTX_R0,    0x00
        .set    CTX_R1,    0x04
        .set    CTX_R2,    0x08
        .set    CTX_SRR0,  0x80
        .set    CTX_SRR1,  0x84
        .set    CTX_DAR,   0x88
        .set    CTX_DSISR, 0x8C
        .set    CTX_LR,    0x90
        .set    CTX_CTR,   0x94
        .set    CTX_CR,    0x98
        .set    CTX_XER,   0x9C

/* The blob starts at 0x80000C00, the system-call vector.  The SDK's own handler there sets HID0[ABE] (address broadcast enable) around a
   sync so that a `sc` broadcasts the caller's cache operations, then returns; the game's __OSInitSystemCall writes its own copy over this
   whenever it runs after us.  Ours below does the same job in our own words (scratch r11/r12; 8 words, 0x20 bytes, like the original). */
        .section .text.sysvec,"ax"
        .globl  cbvmc_blob_start
cbvmc_blob_start:
        mfspr   r11, 1008                   /* HID0 */
        ori     r12, r11, 0x0008            /* ABE */
        mtspr   1008, r12
        isync
        sync
        mtspr   1008, r11
        rfi
        nop

        .section .text.vec,"ax"
        .globl  cbvmc_vec
cbvmc_vec:
        stw     r0, cbvmc_ctx_phys + CTX_R0 (0)
        mfcr    r0                          /* the window test below sets CR0: keep the game's CR first */
        stw     r0, cbvmc_ctx_phys + CTX_CR (0)
#ifdef CBVMC_T2
        /* T2 (cbvmc_t2.h): a new executable's OSExceptionInit has rewritten the vectors inside the blob since the last entry: repair first.  This entry,
           the chain word, the repair and the context all live outside those six ranges (cbvmc_blob.ld puts .text.vec and the data above 0x80001800). */
        lwz     r0, cbvmc_fix_pending_phys (0)
        cmpwi   r0, 0
        bne-    cbvmc_fix
#endif
cbvmc_vec_dispatch:
        mfdar   r0
        srwi    r0, r0, 16
        cmplwi  r0, 0x0C00
#ifdef CBVMC_T2
        beq+    cbvmc_ours
        mfdsisr r0                          /* the breakpoint on the vector doubleword (DSISR bit 9, DAR in 0x300..0x307 of any segment alias) is ours too */
        andis.  r0, r0, 0x0040
        beq+    cbvmc_not_ours
        mfdar   r0
        rlwinm  r0, r0, 0, 2, 28
        cmplwi  r0, 0x0300
        bne-    cbvmc_not_ours
cbvmc_ours:
#else
        bne-    cbvmc_not_ours
#endif
        stw     r1, cbvmc_ctx_phys + CTX_R1 (0)
        mfsrr0  r0
        stw     r0, cbvmc_ctx_phys + CTX_SRR0 (0)
        mfsrr1  r0
        stw     r0, cbvmc_ctx_phys + CTX_SRR1 (0)
        mfmsr   r0
        ori     r0, r0, 0x0032              /* IR | DR | RI */
        mtsrr1  r0
        lis     r0, cbvmc_dsi_entry@h
        ori     r0, r0, cbvmc_dsi_entry@l
        mtsrr0  r0
        rfi

cbvmc_not_ours:
        lwz     r0, cbvmc_ctx_phys + CTX_CR (0)
        mtcr    r0
        lwz     r0, cbvmc_ctx_phys + CTX_R0 (0)
        ba      cbvmc_chain_phys

/* the replaced first word of the SDK's DSI stub, then the rest of the stub (patched by the installer) */
        .globl  cbvmc_chain
cbvmc_chain:
        .long   0x7C9043A6                  /* mtsprg 0,r4 (checked by the installer before it writes the vector) */
        ba      0x00000304

#ifdef CBVMC_T2
/* Real mode, r0 and CR saved (context slots).  Copy the six 256-byte ranges back from the resident snapshot (physical address in cbvmc_fix_src), only the
   words that differ.  If any word was copied: push the six ranges out of the data cache and the instruction cache and clear the flag (the clobber has
   happened).  If none differed the flag stays set (the clobber may still come) and nothing else is done.  Either way continue with the normal dispatch
   below (cbvmc_vec_dispatch) for the exception that brought us here.  Uses r3..r8 (kept in the context slots, restored before the dispatch; the
   virtual-mode entry overwrites those slots with the same values), never CTR, XER or LR.
   2026-10-14 fix: the first version returned with `rfi` to the faulting instruction when nothing differed, without clearing the flag, so the same
   exception came back to this code for ever (a hang whenever the doubleword at 0x300 was rewritten without the six ranges). */
cbvmc_fix:
        stw     r3, cbvmc_ctx_phys + 0x0C (0)
        stw     r4, cbvmc_ctx_phys + 0x10 (0)
        stw     r5, cbvmc_ctx_phys + 0x14 (0)
        stw     r6, cbvmc_ctx_phys + 0x18 (0)
        stw     r7, cbvmc_ctx_phys + 0x1C (0)
        stw     r8, cbvmc_ctx_phys + 0x20 (0)
        lwz     r3, cbvmc_fix_src_phys (0)
        li      r6, 0
        li      r7, 0
1:      lwz     r4, cbvmc_fix_tab_phys (r6)
        li      r8, 64
2:      lwz     r5, 0 (r3)
        lwz     r0, 0 (r4)
        cmpw    r0, r5
        beq     3f
        stw     r5, 0 (r4)
        li      r7, 1
3:      addi    r3, r3, 4
        addi    r4, r4, 4
        addi    r8, r8, -1
        cmpwi   r8, 0
        bne     2b
        addi    r6, r6, 4
        cmplwi  r6, 24
        blt     1b
        cmpwi   r7, 0
        beq     5f
        li      r6, 0
6:      lwz     r4, cbvmc_fix_tab_phys (r6)
        li      r8, 8
4:      dcbst   0, r4
        sync
        icbi    0, r4
        addi    r4, r4, 32
        addi    r8, r8, -1
        cmpwi   r8, 0
        bne     4b
        addi    r6, r6, 4
        cmplwi  r6, 24
        blt     6b
        sync
        isync
        li      r0, 0
        stw     r0, cbvmc_fix_pending_phys (0)
5:      lwz     r3, cbvmc_ctx_phys + 0x0C (0)
        lwz     r4, cbvmc_ctx_phys + 0x10 (0)
        lwz     r5, cbvmc_ctx_phys + 0x14 (0)
        lwz     r6, cbvmc_ctx_phys + 0x18 (0)
        lwz     r7, cbvmc_ctx_phys + 0x1C (0)
        lwz     r8, cbvmc_ctx_phys + 0x20 (0)
        b       cbvmc_vec_dispatch

/* physical addresses of the six vector ranges inside the blob (= cbvmc_t2_ranges & 0x7FFFFFFF; the host test compares the two) */
        .balign 4
        .globl  cbvmc_fix_tab
cbvmc_fix_tab:
        .long   0x00000C00, 0x00000D00, 0x00000F00, 0x00001300, 0x00001400, 0x00001700
#endif

        .globl  cbvmc_dsi_entry
cbvmc_dsi_entry:
        lis     r1, cbvmc_ctx@ha
        addi    r1, r1, cbvmc_ctx@l
        stmw    r2, CTX_R2 (r1)
        mfdar   r3
        stw     r3, CTX_DAR (r1)
        mfdsisr r3
        stw     r3, CTX_DSISR (r1)
        mflr    r3
        stw     r3, CTX_LR (r1)
        mfctr   r3
        stw     r3, CTX_CTR (r1)
                                            /* CR was saved by the real-mode part, before the window test touched CR0 */
        mfxer   r3
        stw     r3, CTX_XER (r1)
        mr      r3, r1                      /* argument: the context */
        lis     r1, cbvmc_sp@ha
        lwz     r1, cbvmc_sp@l (r1)         /* top of the handler stack: in the high reserve, set by the installer */
        li      r0, 0
        stw     r0, 0 (r1)                  /* terminate the back chain */
        bl      cbvmc_dsi_c
        lis     r1, cbvmc_ctx@ha
        addi    r1, r1, cbvmc_ctx@l
        lwz     r0, CTX_SRR0 (r1)
        mtsrr0  r0
        lwz     r0, CTX_SRR1 (r1)
        mtsrr1  r0
        lwz     r0, CTX_LR (r1)
        mtlr    r0
        lwz     r0, CTX_CTR (r1)
        mtctr   r0
        lwz     r0, CTX_CR (r1)
        mtcr    r0
        lwz     r0, CTX_XER (r1)
        mtxer   r0
        lmw     r2, CTX_R2 (r1)
        lwz     r0, CTX_R0 (r1)
        lwz     r1, CTX_R1 (r1)
        rfi
