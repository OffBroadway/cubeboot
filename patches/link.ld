OUTPUT_FORMAT("elf32-powerpc", "elf32-powerpc", "elf32-powerpc")
OUTPUT_ARCH(powerpc:common)
ENTRY(_start)

PHDRS {
    text PT_LOAD;
}

SECTIONS {
    . = 0x81700000;

    .text : {
        *(.text);
        . = ALIGN(32);

        *(.rodata* .data* .gnu.linkonce.r.*);
        . = ALIGN(32);
    } :text = 0
}
