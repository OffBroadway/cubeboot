#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "print.h"
#include "elf.h"

u32 get_symbol_value(Elf32_Shdr* symshdr, Elf32_Sym* syment, char* symstringdata, char* sym_name) {
    uint32_t value = 0;

    // find symbol by name
    for (int i = 0; i < (symshdr->sh_size / sizeof(Elf32_Sym)); ++i) {
        if (syment[i].st_name == SHN_UNDEF) {
            continue;
        }

        char *current_symname = symstringdata + syment[i].st_name;
        if (strcmp(current_symname, sym_name) == 0) {
            value = syment[i].st_value;
        }
    }

    return value;
}

void set_patch_value(Elf32_Shdr* symshdr, Elf32_Sym* syment, char* symstringdata, char* sym_name, u32 value) {
    u32 ptr = 0;
    for (int i = 0; i < (symshdr->sh_size / sizeof(Elf32_Sym)); ++i) {
        if (syment[i].st_name == SHN_UNDEF) {
            continue;
        }

        char *symname = symstringdata + syment[i].st_name;
        if (strcmp(symname, sym_name) == 0) {
            ptr = syment[i].st_value;
        }
    }
    
    if (ptr != 0) {
        iprintf("Found var %s = %08x\n", sym_name, ptr);
        *(u32*)ptr = value; 
    } else {
        iprintf("Could not find symbol %s\n", sym_name);
    }
}
