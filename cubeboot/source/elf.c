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

// cubeboot_vmc: copy a string into a char array of the patch image (at most capacity-1 characters, always terminated); a value that does not fit is not copied at all
void set_patch_string(Elf32_Shdr* symshdr, Elf32_Sym* syment, char* symstringdata, char* sym_name, const char *value, u32 capacity) {
    u32 ptr = 0;
    for (int i = 0; i < (symshdr->sh_size / sizeof(Elf32_Sym)); ++i) {
        if (syment[i].st_name == SHN_UNDEF) {
            continue;
        }
        if (strcmp(symstringdata + syment[i].st_name, sym_name) == 0) {
            ptr = syment[i].st_value;
        }
    }
    if (ptr == 0 || value == NULL) {
        iprintf("Could not set string %s\n", sym_name);
        return;
    }
    if (strlen(value) >= capacity) {
        iprintf("String %s too long, ignored\n", sym_name);
        return;
    }
    strcpy((char*)ptr, value);
}
