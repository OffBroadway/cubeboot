/* Copyright (C) 2026 Nigzel / Bjerreman. SPDX-License-Identifier: GPL-2.0-only */
/* cubeboot_vmc INSTALL image: flat, linked at CBVMC_B_BASE; entry (the OSInit hook target) at offset 0. __cbvmc_img_end is where the installer
   stack area starts (cbvmc_tramp.S computes the stack top from it). */
ENTRY(cbvmc_entry)
SECTIONS
{
  .text : {
    KEEP(*(.text.cbvmc_entry))
    *(.text .text.*)
    . = ALIGN(4);
    *(.rodata .rodata.* .sdata2 .sdata2.*)
    . = ALIGN(32);
  }
  .cbvmc_blob : {
    *(.cbvmc_blob)
    . = ALIGN(32);
  }
  .cbvmc_data : {
    *(.data .data.* .sdata .sdata.*)
    . = ALIGN(32);
    *(.bss .bss.* .sbss .sbss.* COMMON)
    . = ALIGN(32);
    __cbvmc_img_end = .;
  }
  /DISCARD/ : { *(.comment) *(.eh_frame*) *(.note*) *(.gnu*) }
}
ASSERT(cbvmc_entry == ADDR(.text), "entry not at image offset 0")
