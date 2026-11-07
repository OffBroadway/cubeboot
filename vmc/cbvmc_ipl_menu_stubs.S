/* SPDX-License-Identifier: GPL-2.0-only */
/* Stubs of the Memory Card page popup extension (cbvmc_ipl_menu.c).  Each replaces ONE instruction of BS2 with a `bl` here, does its work and
   then performs the replaced instruction, so the condition register the IPL tests afterwards is set exactly as before.  Original work.

   Three sites need C (guard, choice, confirm): they go through the trampoline, which saves every register and the condition register on the
   IPL thread's stack (0x100 bytes), switches to the extension's own stack, calls cbvmc_menu_dispatch(regs, site) which may change the saved
   r0 / r9, restores everything and continues at the instruction after the `bl cbvmc_menu_tramp` of the stub (through CTR; LR carries the
   return address into BS2 again).  r11 and r12 are free at every site (checked in the four known builds). */

        .section .text,"ax"

        .globl  cbvmc_menu_tramp
cbvmc_menu_tramp:                       /* r12 = return address into BS2, r11 = site index, LR = continuation in the stub */
        stwu    r1,-0x100(r1)
        stmw    r0,0x10(r1)             /* r0..r31 at 0x10..0x8f */
        mfcr    r0
        stw     r0,0x90(r1)
        mfxer   r0
        stw     r0,0x94(r1)
        stw     r12,0x9c(r1)            /* BS2 return address */
        mflr    r0
        stw     r0,0xa0(r1)             /* continuation */
        stw     r11,0xa4(r1)            /* site */
        addi    r3,r1,0x10              /* regs */
        mr      r4,r11                  /* site */
        lis     r6,cbvmc_menu_sp@ha
        lwz     r6,cbvmc_menu_sp@l(r6)  /* top of the extension's stack */
        stw     r1,0(r6)                /* back chain to the IPL frame */
        mr      r1,r6
        bl      cbvmc_menu_dispatch
        lwz     r1,0(r1)                /* the IPL frame again */
        lwz     r0,0xa0(r1)
        mtctr   r0
        lwz     r0,0x9c(r1)
        mtlr    r0
        lwz     r0,0x94(r1)
        mtxer   r0
        lwz     r0,0x90(r1)
        mtcr    r0
        lwz     r0,0x10(r1)
        lmw     r2,0x18(r1)             /* r2..r31 back (r1 is this frame already) */
        addi    r1,r1,0x100
        bctr

/* S1  MC_StateBrowse+0x5b4   cmplwi r0,0   (r7 slot, r3 slot*1524 + fileNo*12, r0 present) */
        .globl  cbvmc_menu_stub_guard
cbvmc_menu_stub_guard:
        mflr    r12
        li      r11,0
        bl      cbvmc_menu_tramp
        cmplwi  r0,0
        blr

/* S4  MC_StatePopup+0x108    cmpwi r9,1    (r9 item, r8 slot, r30 fileNo; r0 is live and kept) */
        .globl  cbvmc_menu_stub_choice
cbvmc_menu_stub_choice:
        mflr    r12
        li      r11,1
        bl      cbvmc_menu_tramp
        cmpwi   r9,1
        blr

/* S5  MC_StateConfirm+0xe8   cmpwi r0,1    (r0 item, r30 slot, r28 fileNo) */
        .globl  cbvmc_menu_stub_confirm
cbvmc_menu_stub_confirm:
        mflr    r12
        li      r11,2
        bl      cbvmc_menu_tramp
        cmpwi   r0,1
        blr

/* S2  the item count: MC_PopupDrawItems+0x3f0 `cmplwi r0,3`, MC_StatePopup+0x7c `cmpwi r0,3` (r3 free at both) */
        .globl  cbvmc_menu_stub_cmp_count
cbvmc_menu_stub_cmp_count:
        lis     r3,cbvmc_menu_count@ha
        lwz     r3,cbvmc_menu_count@l(r3)
        cmplw   r0,r3
        blr

/* S2  MC_PopupMeasureLabels+0x88 `cmplwi r0,3`: the measure loop runs over cbvmc_menu_mcount records (the list, or on the Yes/No column the one item on show) */
        .globl  cbvmc_menu_stub_cmp_mcount
cbvmc_menu_stub_cmp_mcount:
        lis     r3,cbvmc_menu_mcount@ha
        lwz     r3,cbvmc_menu_mcount@l(r3)
        cmplw   r0,r3
        blr

/* S2  MC_StatePopup+0x50  `li r0,2`: the cursor wraps to the last item (r3 free) */
        .globl  cbvmc_menu_stub_count_m1
cbvmc_menu_stub_count_m1:
        lis     r3,cbvmc_menu_count@ha
        lwz     r3,cbvmc_menu_count@l(r3)
        addi    r0,r3,-1                /* never `addi r0,r0,..`: r0 as the source reads as zero (that was `li r0,-1`: the cursor left the list) */
        lis     r3,cbvmc_menu_wrap_val@ha
        stw     r0,cbvmc_menu_wrap_val@l(r3)   /* the last wrap value (the choice note prints it) */
        blr

/* S7  MC_StateMessage+0xb8 `rlwinm. r0,r0,0,23,23` (r0 = the pad's pressed bits, bit 0x100 = A): site 3 of the dispatcher may add the A bit
       (a successful Format's message closes by itself after a while).  r11/r12 free, the condition register is set by the re-done instruction. */
        .globl  cbvmc_menu_stub_msg
cbvmc_menu_stub_msg:
        mflr    r12
        li      r11,3
        bl      cbvmc_menu_tramp
        rlwinm. r0,r0,0,23,23
        blr

/* S8  MC_PopupDrawItems+0x22c `cmplwi r31,1` (r31 = the item): items 0 and 1 draw with the Move / Copy availability fade, the rest plain.
       Site 4 of the dispatcher sets cbvmc_menu_item_plain for the item (ours: plain, and dims the frame's colours when the action is unavailable);
       with it set the compare answers "greater", the plain path.  r11/r12 free. */
        .globl  cbvmc_menu_stub_dim
cbvmc_menu_stub_dim:
        mflr    r12
        li      r11,4
        bl      cbvmc_menu_tramp
        lis     r12,cbvmc_menu_item_plain@ha
        lbz     r12,cbvmc_menu_item_plain@l(r12)
        cmplwi  r12,0
        bnelr                           /* 1 > 0: the plain path */
        cmplwi  r31,1
        blr

/* S9  MC_PopupDraw+0x174 `lbz r31,3(r27)`: just before it r28 is the popup box's top (the icon's top for grid rows 0 and 1, above the icon for rows 2
       and 3; r5 = the open animation's offset, r24 = the popup record, +30 its full height).  With more than three items the box reaches the info panel
       (rows 0/1) or the top of the screen (rows 2/3): the top is kept within [cbvmc_menu_box_top, cbvmc_menu_box_lim - height] (1/16 pixel units of the
       page's 640x480 layout).  r11/r12 free, r0/r5 live, the condition register is not live here (cr7 used anyway). */
        .globl  cbvmc_menu_stub_place
cbvmc_menu_stub_place:
        lis     r11,cbvmc_menu_box_on@ha
        lbz     r11,cbvmc_menu_box_on@l(r11)
        cmpwi   cr7,r11,0
        beq     cr7,2f                  /* the Yes/No column: stock placement */
        lhz     r12,30(r24)
        lis     r11,cbvmc_menu_box_lim@ha
        lhz     r11,cbvmc_menu_box_lim@l(r11)
        subf    r12,r12,r11             /* the lowest top that keeps the box above the panel */
        cmpw    cr7,r28,r12
        ble     cr7,1f
        mr      r28,r12
1:      lis     r11,cbvmc_menu_box_top@ha
        lhz     r11,cbvmc_menu_box_top@l(r11)
        cmpw    cr7,r28,r11
        bge     cr7,2f
        mr      r28,r11
2:      lbz     r31,3(r27)
        blr

/* S5b MC_StateConfirm+0x8c `cmpwi r0,0` (r0 = the Yes/No cursor): while the Region choice is open both rows go on (the confirm site reads the row) */
        .globl  cbvmc_menu_stub_yes
cbvmc_menu_stub_yes:
        lis     r12,cbvmc_menu_layer2@ha
        lbz     r12,cbvmc_menu_layer2@l(r12)
        cmpwi   r12,0
        beq     1f
        cmpw    r12,r12                 /* equal: the Yes path */
        blr
1:      cmpwi   r0,0
        blr

/* S6  CARDFormatAsync: the card header's encoding store, overridden once per channel when the popup's Format item asked for it (the encoding of the card's region).
       USA/JAP: `sth r0,36(r27)` (two sites), PAL: `sth r3,36(r27)`; r22 = channel in all builds; r11/r12 free. */
        .globl  cbvmc_menu_stub_enc_r0
cbvmc_menu_stub_enc_r0:
        lis     r12,cbvmc_menu_enc@ha
        addi    r12,r12,cbvmc_menu_enc@l
        lbzx    r11,r12,r22
        cmpwi   r11,0
        beq     1f
        addi    r0,r11,-1
        li      r11,0
        stbx    r11,r12,r22
1:      sth     r0,36(r27)
        blr

        .globl  cbvmc_menu_stub_enc_r3
cbvmc_menu_stub_enc_r3:
        lis     r12,cbvmc_menu_enc@ha
        addi    r12,r12,cbvmc_menu_enc@l
        lbzx    r11,r12,r22
        cmpwi   r11,0
        beq     1f
        addi    r3,r11,-1
        li      r11,0
        stbx    r11,r12,r22
1:      sth     r3,36(r27)
        blr

/* The popup box geometry (MC_PopupMeasureLabels / MC_PopupDrawItems; r11/r12 free, the condition register is not live at these sites).
   Height: stock `lh << 5` (full, +0xa4 r0, +0xd0 r29 masked to 16 bits, +0x100 r3) is 3 items: the box grows by (items - 1) lines, so the
   factor is (count - 1) * 16.  The half height `lh << 4` (+0x144) sizes the Yes/No box and stays stock (two rows). */
        .globl  cbvmc_menu_stub_h_r0
cbvmc_menu_stub_h_r0:
        lis     r12,cbvmc_menu_count@ha
        lwz     r12,cbvmc_menu_count@l(r12)
        addi    r12,r12,-1
        slwi    r12,r12,4
        mullw   r0,r0,r12
        blr
        .globl  cbvmc_menu_stub_h_r29
cbvmc_menu_stub_h_r29:
        lis     r12,cbvmc_menu_count@ha
        lwz     r12,cbvmc_menu_count@l(r12)
        addi    r12,r12,-1
        slwi    r12,r12,4
        mullw   r29,r0,r12
        clrlwi  r29,r29,16
        blr
        .globl  cbvmc_menu_stub_h_r3
cbvmc_menu_stub_h_r3:
        lis     r12,cbvmc_menu_count@ha
        lwz     r12,cbvmc_menu_count@l(r12)
        addi    r12,r12,-1
        slwi    r12,r12,4
        mullw   r3,r3,r12
        blr
/* Width: MC_PopupMeasureLabels+0x150 `sth r31,0(r26)` stores the widest label; kept in cbvmc_menu_maxw too.
   MC_PopupDrawItems+0xdc / +0xf4 `lhz r0,24(r24)` is the item text box width (the stock one fits "Erase"): the widest label plus a margin when wider. */
        .globl  cbvmc_menu_stub_maxw
cbvmc_menu_stub_maxw:
        lis     r12,cbvmc_menu_maxw@ha
        sth     r31,cbvmc_menu_maxw@l(r12)
        sth     r31,0(r26)
        blr
        .globl  cbvmc_menu_stub_w_r0
cbvmc_menu_stub_w_r0:
        lhz     r0,24(r24)
        lis     r12,cbvmc_menu_wide@ha
        lbz     r12,cbvmc_menu_wide@l(r12)
        cmpwi   cr7,r12,0
        beq     cr7,1f
        lis     r12,cbvmc_menu_maxw@ha
        lhz     r12,cbvmc_menu_maxw@l(r12)
        addi    r0,r12,16               /* (not `addi r0,r0,16`, which is `li r0,16`) */
1:      blr

/* The first label record (cbvmc_menu_rec0: 2 until our string block is in place, 64 after): MC_PopupMeasureLabels+0x4 `li r7,2`, +0x40 `addi r3,r3,2`
   (r12 free; these two take cbvmc_menu_mrec0, the measure's first record: the list's, or the one item on show), MC_PopupDrawItems+0x1e8 `li r4,2`.  The page measures the popup before it ever opens, so the stock records must stay in use until then. */
        .globl  cbvmc_menu_stub_rec0_r7
cbvmc_menu_stub_rec0_r7:
        lis     r7,cbvmc_menu_mrec0@ha
        lwz     r7,cbvmc_menu_mrec0@l(r7)
        blr
        .globl  cbvmc_menu_stub_rec0_add_r3
cbvmc_menu_stub_rec0_add_r3:
        lis     r12,cbvmc_menu_mrec0@ha
        lwz     r12,cbvmc_menu_mrec0@l(r12)
        add     r3,r3,r12
        blr
        .globl  cbvmc_menu_stub_rec0_r4
cbvmc_menu_stub_rec0_r4:
        lis     r4,cbvmc_menu_rec0@ha
        lwz     r4,cbvmc_menu_rec0@l(r4)
        blr
