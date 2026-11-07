/* SPDX-License-Identifier: GPL-2.0-only */
/* cbvmc_vec_ipl: DSI entry of the IPL install (G4 stage 1).  Original work (PowerPC architecture manual only).  Differs from cbvmc_vec.S in ONE respect:
   where the real-mode part keeps its five words.  The game-path blob sits at 0x80000C00 and addresses its context with a 16-bit absolute displacement
   (rA = 0).  In the IPL that memory is BS2's, so the blob lives in the top of the Cubeboot patch window (cbvmc_ipl_plan.h) and the real-mode part
   needs the context address in a register.  The only registers free at a DSI are r0 and what the vector can save without memory, so:

     mtsprg0 r0           SPRG0 is dead here: the SDK stub that we chain to starts with `mtsprg0 r4` and nothing reads SPRG0 before that
     r0 = &ctx.r1 (physical, a constant)
     stwx r1,0,r0         indexed store: rA = 0 means "no base", the address is rB = r0.  r1 is saved at ctx.r1
     r1 = &ctx            from here on r1 is a base register, r0 is free again
     the rest as in cbvmc_vec.S: r0 (from SPRG0), CR, test DAR; when ours: SRR0, SRR1, rfi into the virtual-mode entry

   When the fault is not ours (DAR outside the 0x0C00xxxx page) r1, CR and r0 are restored exactly and the stub chains to the SDK's code (SPRG0 is overwritten by
   its first instruction, as it always is).  Nothing else is touched: no SPRG1..3, no memory outside the blob's own context. */
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

        .section .text.vec,"ax"
        .globl  cbvmc_vec
cbvmc_vec:
        mtsprg  0, r0
        lis     r0, (cbvmc_ctx_phys + CTX_R1)@h
        ori     r0, r0, (cbvmc_ctx_phys + CTX_R1)@l
        stwx    r1, 0, r0
        mr      r1, r0
        addi    r1, r1, -CTX_R1               /* (addi with rA = r0 would be `li`) */
        mfsprg  r0, 0
        stw     r0, CTX_R0 (r1)
        mfcr    r0                          /* the window test below sets CR0: keep the game's CR first */
        stw     r0, CTX_CR (r1)
        mfdar   r0
        srwi    r0, r0, 16
        cmplwi  r0, 0x0C00
        bne-    cbvmc_not_ours
        mfsrr0  r0
        stw     r0, CTX_SRR0 (r1)
        mfsrr1  r0
        stw     r0, CTX_SRR1 (r1)
        mfmsr   r0
        ori     r0, r0, 0x0032              /* IR | DR | RI */
        mtsrr1  r0
        lis     r0, cbvmc_dsi_entry@h
        ori     r0, r0, cbvmc_dsi_entry@l
        mtsrr0  r0
        rfi

cbvmc_not_ours:
        lwz     r0, CTX_CR (r1)
        mtcr    r0
        lwz     r0, CTX_R0 (r1)
        lwz     r1, CTX_R1 (r1)
        ba      cbvmc_chain_phys

/* the replaced first word of the SDK's DSI stub, then the rest of the stub (the installer checks the first word before it writes the vector) */
        .globl  cbvmc_chain
cbvmc_chain:
        .long   0x7C9043A6                  /* mtsprg 0,r4 */
        ba      0x00000304

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
        mfxer   r3
        stw     r3, CTX_XER (r1)
        mr      r3, r1                      /* argument: the context */
        lis     r1, cbvmc_sp@ha
        lwz     r1, cbvmc_sp@l (r1)         /* top of the handler stack, set by the installer */
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
