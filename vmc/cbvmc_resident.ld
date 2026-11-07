/* Copyright (C) 2026 Nigzel / Bjerreman. SPDX-License-Identifier: GPL-2.0-only */
/* cubeboot_vmc RESIDENT image: flat, linked at CBVMC_R_BASE (-Ttext), code and read-only data first, writable data and bss emitted as zeros
   (the loader copies the file), the whole thing ends on a 32-byte line.  __cbvmc_res_end is checked against CBVMC_R_SIZE by build_blob.sh. */
SECTIONS
{
  .text : {
    *(.text .text.*)
    . = ALIGN(4);
    *(.rodata .rodata.* .sdata2 .sdata2.*)
    . = ALIGN(32);
  }
  .cbvmc_data : {
    *(.data .data.* .sdata .sdata.*)
    . = ALIGN(32);
    *(.bss .bss.* .sbss .sbss.* COMMON)
    . = ALIGN(32);
    __cbvmc_res_end = .;
  }
  /DISCARD/ : { *(.comment) *(.eh_frame*) *(.note*) *(.gnu*) }
}
