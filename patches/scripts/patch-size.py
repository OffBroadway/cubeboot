#!/usr/bin/env python3

from elftools.elf import elffile

linker_script_template = '''
OUTPUT_FORMAT("elf32-powerpc", "elf32-powerpc", "elf32-powerpc")
OUTPUT_ARCH(powerpc:common)
ENTRY(_start)

PHDRS {{
    {phdrs}
}}

{var_lines}

SECTIONS {{
    . = 0x81300000;

    {sects}
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
    print("missing patch object input")
    exit(1)

file = open(sys.argv[1], 'rb')
elf = elffile.ELFFile(file)

headers = []
sections = []
lines = []
for i in range(elf.num_sections()):
    sect = elf.get_section(i)

    if sect.name == ".symtab":
        addrs = []
        for sym in sect.iter_symbols():
            if sym.name.startswith('_addr_'):
                addr = sym.name.removeprefix('_addr_')
                if addr in addrs:
                    continue
                addrs.append(addr)
                lines.append(f'{sym.name} = 0x{addr};\n')

    if not sect.name.startswith('.patch.'):
        continue

    name = sect.name.removeprefix('.patch.')
    symbol_name = name.removesuffix('_func')

    header = header_template.format(symbol_name).rstrip()
    headers.append(header)

    section = section_template.format(symbol_name)
    sections.append(section)

    size_name = symbol_name + '_size'
    size = sect.data_size

    lines.append(f'{size_name} = 0x{size:08X};\n')

headers_content = "".join(headers).lstrip()
sections_content = "".join(sections).strip()
var_contents = "".join(lines).rstrip()
linker_script = linker_script_template.format(phdrs=headers_content, sects=sections_content, var_lines=var_contents)

with open('patch.ld', 'w') as output:
    output.write(linker_script.lstrip())
