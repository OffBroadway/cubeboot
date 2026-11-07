/* Copyright (C) 2026 Nigzel / Bjerreman. SPDX-License-Identifier: GPL-2.0-only */
/* cubeboot_vmc runtime blob: flat image linked at 0x80000C00 (cbvmc_blob.h).  Writable data and bss are emitted as zeros in the
   flat file so the installer copies one contiguous range and nothing stale survives.  Physical aliases (address & 0x7FFFFFFF)
   are what the real-mode vector uses. */
ENTRY(cbvmc_blob_start)
SECTIONS
{
  . = 0x80000C00;
  .blob : {
    KEEP(*(.text.sysvec))
    KEEP(*(.text.vec))
    /* Dolphin treats execution at exactly 0x80001800 (its Gecko code handler address) as the end of the CPU run (found in
       Dolphin 2606a: single-stepping onto that PC closes the emulation).  No instruction may live there, so the code is split:
       the entry code and the register runtime first (blob.o's glue moved behind the data when slot B grew rt.o), then the data (which straddles 0x80001800), then the rest of the code.  Harmless on a console. */
    *rt.o(.text .text.*)
    . = ALIGN(4);
    *(.rodata .rodata.* .sdata2 .sdata2.*)
    . = ALIGN(32);
    __cbvmc_data = .;
    *(.data .data.* .sdata .sdata.*)
    . = ALIGN(32);
    *(.bss .bss.* .sbss .sbss.* COMMON)
    . = ALIGN(32);
    __cbvmc_data_end = .;
    *(.text .text.*)
    . = ALIGN(4);
    __cbvmc_blob_end = .;
  }
  /DISCARD/ : { *(.comment) *(.eh_frame*) *(.note*) *(.gnu*) }
}
cbvmc_ctx_phys = cbvmc_ctx & 0x7FFFFFFF;
cbvmc_chain_phys = cbvmc_chain & 0x7FFFFFFF;
ASSERT(__cbvmc_blob_end <= 0x80003000, "blob does not fit below the OS globals")
ASSERT(__cbvmc_data <= 0x80001800 && __cbvmc_data_end > 0x80001800, "data must straddle 0x80001800 (no code there: Dolphin ends the run at that PC)")
