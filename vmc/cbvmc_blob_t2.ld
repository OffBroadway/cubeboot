/* Copyright (C) 2026 Nigzel / Bjerreman. SPDX-License-Identifier: GPL-2.0-only */
/* cubeboot_vmc runtime blob, T2 layout (lean loader blob, built with -DCBVMC_T2): flat image linked at 0x80000C00 (cbvmc_blob.h).
   A second executable's OSExceptionInit rewrites the exception vectors 0xC00, 0xD00, 0xF00, 0x1300, 0x1400 and 0x1700 (one SDK stub each, 0x98 bytes;
   0x100 is assumed).  They lie inside the blob, so (cbvmc_t2.h):
     - only CODE and read-only data may sit in [0x80000C00, 0x80001800): it is repaired from the resident snapshot at the next entry;
     - the live state (data, bss: the card model, the context, the repair flag) starts at 0x80001800 (also Dolphin's rule: no instruction at
       0x80001800, its Gecko handler address ends the run);
     - the real-mode entry, the chain word and the repair code (.text.vec) follow the data, outside every rewritten range.
   Writable data and bss are emitted as zeros in the flat file so the installer copies one contiguous range and nothing stale survives.
   Physical aliases (address & 0x7FFFFFFF) are what the real-mode vector uses. */
ENTRY(cbvmc_blob_start)
SECTIONS
{
  . = 0x80000C00;
  .blob : {
    KEEP(*(.text.sysvec))
    *rt.o(.text .text.*)
    *libc.o(.text .text.*)
    . = ALIGN(4);
    *(.rodata .rodata.* .sdata2 .sdata2.*)
    __cbvmc_lo_end = .;
    . = 0x80001800 - 0x80000C00;          /* inside an output section "." is the offset from its start (0x80000C00) */
    __cbvmc_data = .;
    *(.data .data.* .sdata .sdata.*)
    . = ALIGN(32);
    *(.bss .bss.* .sbss .sbss.* COMMON)
    . = ALIGN(32);
    __cbvmc_data_end = .;
    KEEP(*(.text.vec))
    *(.text .text.*)
    . = ALIGN(4);
    __cbvmc_blob_end = .;
  }
  /DISCARD/ : { *(.comment) *(.eh_frame*) *(.note*) *(.gnu*) }
}
cbvmc_ctx_phys = cbvmc_ctx & 0x7FFFFFFF;
cbvmc_chain_phys = cbvmc_chain & 0x7FFFFFFF;
cbvmc_fix_pending_phys = cbvmc_fix_pending & 0x7FFFFFFF;
cbvmc_fix_src_phys = cbvmc_fix_src & 0x7FFFFFFF;
cbvmc_fix_tab_phys = cbvmc_fix_tab & 0x7FFFFFFF;
ASSERT(__cbvmc_blob_end <= 0x80003000, "blob does not fit below the OS globals")
ASSERT(__cbvmc_lo_end <= 0x80001800, "code below the data does not fit under 0x80001800")
ASSERT(__cbvmc_data == 0x80001800 && __cbvmc_data_end > 0x80001800, "data must start at 0x80001800")
ASSERT(cbvmc_vec >= __cbvmc_data_end && cbvmc_chain >= __cbvmc_data_end && cbvmc_fix_tab >= __cbvmc_data_end, "T2 entry must lie outside the rewritten vector ranges")
ASSERT(cbvmc_ctx >= 0x80001800 && cbvmc_fix_pending >= 0x80001800 && cbvmc_ctx_phys < 0x8000, "state must lie outside the rewritten vector ranges, below 0x8000")
