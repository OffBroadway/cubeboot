OUTPUT_FORMAT("elf32-powerpc", "elf32-powerpc", "elf32-powerpc")
OUTPUT_ARCH(powerpc:common)
ENTRY(_start)

PHDRS {
    text PT_LOAD;
}

/* unknown: needed by compiler */
memset = 0x80000000;

/* debug: __OSGetSystemTime = 0x8136023c; */
/* debug: __OSBusClock = 0x800000f8; */

/* videomode: rmode = 0x8137d9f0; */
/* videomode: heap_init = 0x81307d90; */

ntsc11_PADSync = 0x81368c34;
ntsc11_OSDisableInterrupts = 0x8135bc70;
ntsc11___OSStopAudioSystem = 0x81359580;
ntsc11_run = 0x813007b8;

OSReport = 0x8135a344;

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

        *(.rodata* .data* .bss* .sbss* .gnu.linkonce.r.*);
        . = ALIGN(32);
    } :text = 0

    .reloc : {
        *(.reloc*);
        . = ALIGN(32);
    } :text
}
