#include <gctypes.h>
#include "elf_abi.h"

u32 get_symbol_value(Elf32_Shdr* symshdr, Elf32_Sym* syment, char* symstringdata, char* sym_name);
void set_patch_value(Elf32_Shdr* symshdr, Elf32_Sym* syment, char* symstringdata, char* sym_name, u32 value);
