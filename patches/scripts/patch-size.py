#!/usr/bin/env python3

from elftools.elf import elffile

linker_script_template = '''
OUTPUT_FORMAT("elf32-powerpc", "elf32-powerpc", "elf32-powerpc")
OUTPUT_ARCH(powerpc:common)
ENTRY(_start)

PHDRS {{
    {}
}}

SECTIONS {{
    . = 0x81300000;

    {}
}}
'''

header_template = '''
    patch.{0}_func PT_LOAD FLAGS(5);
'''


section_template = '''
    .patch.{0}_func ABSOLUTE({0}_address) : {{
        KEEP(*(.patch.{0}_func)); LONG(0);
    }} :patch.{0}_func
'''

import sys
if len(sys.argv) < 2:
    print("missing patch size input")
    exit(1)

file = open(sys.argv[1], 'rb')
elf = elffile.ELFFile(file)

headers = []
sections = []
lines = []
for i in range(elf.num_sections()):
    sect = elf.get_section(i)

    if sect.name == ".symtab":
        for sym in sect.iter_symbols():
            if sym.name.startswith('_addr_'):
                addr = sym.name.removeprefix('_addr_')
                lines.append(f'{sym.name} = 0x{addr};\n')

    if not sect.name.startswith('.patch.'):
        continue

    name = sect.name.removeprefix('.patch.')
    symbol_name = name.removesuffix('_func')

    header = header_template.format(symbol_name)
    headers.append(header)

    section = section_template.format(symbol_name)
    sections.append(section)

    size_name = symbol_name + '_size'
    size = sect.data_size

    lines.append(f'{size_name} = 0x{size:08X};\n')

headers_content = "".join(headers)
sections_content = "".join(sections)
linker_script = linker_script_template.format(headers_content, sections_content)

with open('patch.ld', 'w') as output:
    output.write(linker_script)
    output.writelines(lines)