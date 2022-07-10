OUTPUT_FORMAT("elf32-powerpc", "elf32-powerpc", "elf32-powerpc")
OUTPUT_ARCH(powerpc:common)
ENTRY(_start)

PHDRS {
    text PT_LOAD;
}

/* compiler */
memset = 0x813001c8;
memcpy = 0x813002bc;

OSReport = 0x8135a344;
__OSGetSystemTime = 0x8136023c;
__OSBusClock = 0x800000f8;

cube_init = 0x813107b0;
cube_model = 0x81489f28;
cube_state = 0x81489e58;

logo_model = 0x81483c18;
bg_outer_model = 0x81483cc8;
bg_inner_model = 0x81483ab8;
gc_text_model = 0x81483d40;

SECTIONS {
    . = 0x81700000;

    .text : {
        *(.text);
        . = ALIGN(32);

        *(.rodata* .data* .gnu.linkonce.r.*);
        . = ALIGN(32);
    } :text = 0
}
