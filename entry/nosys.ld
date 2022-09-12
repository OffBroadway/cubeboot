OUTPUT_FORMAT("elf32-powerpc")
OUTPUT_ARCH(powerpc:common)

ENTRY(_start)

/* memset = _memset; */

EXI = 0xCC006800;

SECTIONS {
	. = 0x81300000;

	.start : { *(.init) *(.init.*) } 
	.text : { *(.text) }
	.rodata : { *(.rodata .rodata.*)}
	.data : { *(.data) }

	__bss_start = .;
	.bss : { *(.bss) }
	__bss_end = .;

	. = ALIGN(0x40);
	.stack : {
		. += 0x400;
		_stack_top = .;
	}

    _end = .;
}