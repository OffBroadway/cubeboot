/* SPDX-License-Identifier: GPL-2.0-only */
/* IPL resident + installer image: flat, linked at CBVMC_IPL_RES_BASE (-Ttext), the entry table first, code and read-only data, then data and bss as zeros
   (the Cubeboot patch copies the file), ends on a 32-byte line.  __cbvmc_res_end is checked against CBVMC_IPL_RES_SIZE by build_blob.sh. */
SECTIONS
{
  .text : {
    KEEP(*(.text.cbvmc_ipl_entry))
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
