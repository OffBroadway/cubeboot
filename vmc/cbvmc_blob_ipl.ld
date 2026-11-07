/* SPDX-License-Identifier: GPL-2.0-only */
/* cubeboot_vmc runtime blob of the IPL install: flat image linked at CBVMC_IPL_BLOB_BASE (cbvmc_ipl_plan.h), passed with --defsym __cbvmc_ipl_base.
   Writable data and bss are emitted as zeros in the flat file.  Physical aliases (address & 0x7FFFFFFF) are what the real-mode vector uses.
   No Dolphin constraints here (the 0x80001800 rule belongs to the low-memory blob). */
ENTRY(cbvmc_vec)
SECTIONS
{
  . = __cbvmc_ipl_base;
  .blob : {
    KEEP(*(.text.vec))
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
    . = ALIGN(32);
    __cbvmc_blob_end = .;
  }
  /DISCARD/ : { *(.comment) *(.eh_frame*) *(.note*) *(.gnu*) }
}
cbvmc_ctx_phys = cbvmc_ctx & 0x7FFFFFFF;
cbvmc_chain_phys = cbvmc_chain & 0x7FFFFFFF;
ASSERT(__cbvmc_blob_end <= __cbvmc_ipl_base + 0x2000, "IPL blob does not fit its 8 KiB slot (cbvmc_ipl_plan.h CBVMC_IPL_BLOB_SIZE)")
ASSERT((cbvmc_vec & 0x7FFFFFFF) <= 0x01FFFFFC, "cbvmc_vec is not reachable by `ba`")
ASSERT((cbvmc_ctx & 0x1F) == 0, "ctx must be 32-byte aligned")
