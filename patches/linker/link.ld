OUTPUT_FORMAT("elf32-powerpc", "elf32-powerpc", "elf32-powerpc")
OUTPUT_ARCH(powerpc:common)
ENTRY(_start)

PHDRS {
    text PT_LOAD;
}

/* unknown: needed by compiler */
memset = 0x80000000;

EXI = 0xCC006800;

SECTIONS {
    . = 0x81700000;

    .text : {
        *(.text);
        . = ALIGN(32);

        *(.rodata* .data* .bss* .sbss* .gnu.linkonce.r.*);
        . = ALIGN(32);
    } :text = 0

    .reloc : {
        *(.reloc*);
        . = ALIGN(32);
    } :text

    _patches_end = .;
}
