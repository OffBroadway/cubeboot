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
        KEEP(*(.patch.{0}_func));
    }} :patch.{0}_func
'''

overlay_template = '''
    OVERLAY ABSOLUTE({local_name}_address) : NOCROSSREFS {{
        {overlay_sections}
    }} :patch.{global_name}_func
'''

overlay_section_template = '''
        .patch.{0}_func {{
            KEEP(*(.patch.{0}_func));
        }}
'''

import sys
if len(sys.argv) < 2:
    print("missing patch object input")
    exit(1)

file = open(sys.argv[1], 'rb')
elf = elffile.ELFFile(file)

patch_map = {}

headers = []
sections = []
lines = []
# for i in range(elf.num_sections()):
#     sect = elf.get_section(i)

for sect in elf.iter_sections():
    if not isinstance(sect, elffile.SymbolTableSection):
        continue

    addrs = []
    for sym in sect.iter_symbols():
        if sym.name.startswith('_addr_'):
            addr = sym.name.removeprefix('_addr_')
            if addr in addrs:
                continue
            addrs.append(addr)
            lines.append(f'{sym.name} = 0x{addr};\n')
        elif '_VER_' in sym.name and sym.name.endswith("_address"):
            addr = sym['st_value']
            symbol_name = sym.name.removesuffix("_address")
            if addr not in patch_map:
                patch_map[addr] = []
            patch_map[addr].append(symbol_name)

    for addr in sorted(patch_map.keys()):
        names = patch_map[addr]
        if len(names) == 1:
            symbol_name = names[0]

            header = header_template.format(symbol_name).rstrip()
            headers.append(header)

            section = section_template.format(symbol_name)
            sections.append(section)
        else:
            local_name = names[0]
            global_name = local_name.split('_VER_')[0] + '_VER_GLOBAL'

            header = header_template.format(global_name).rstrip()
            headers.append(header)

            overlay_sections = []
            for local_symbol_name in names:
                overlay_section = overlay_section_template.format(local_symbol_name)
                overlay_sections.append(overlay_section.rstrip())

            overlays = "\n".join(overlay_sections).lstrip()
            overlay = overlay_template.format(local_name=local_name, overlay_sections=overlays, global_name=global_name)
            sections.append(overlay)

for sect in elf.iter_sections():
    if not sect.name.startswith('.patch.'):
        continue

    name = sect.name.removeprefix('.patch.')
    symbol_name = name.removesuffix('_func')

    size_name = symbol_name + '_size'
    size = sect.data_size

    lines.append(f'{size_name} = 0x{size:08X};\n')

headers_content = "".join(headers).lstrip()
sections_content = "".join(sections).strip()
var_contents = "".join(lines).rstrip()
linker_script = linker_script_template.format(phdrs=headers_content, sects=sections_content, var_lines=var_contents)

with open('patch.ld', 'w') as output:
    output.write(linker_script.lstrip())
