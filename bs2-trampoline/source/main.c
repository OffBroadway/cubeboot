#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <ogcsys.h>
#include <gccore.h>
#include <unistd.h>

#include <asndlib.h>
#include <ogc/lwp_threads.h>

#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>

#include "patches_elf.h"
#include "elf_abi.h"

#define IPL_SIZE 0x200000
#define BS2_START_OFFSET 0x800
#define BS2_CODE_OFFSET (BS2_START_OFFSET + 0x20)
#define BS2_BASE_ADDR 0x81300000

extern void __exi_init(void);
extern void __SYS_ReadROM(void *buf,u32 len,u32 offset);

static void (*bs2entry)(void) = (void(*)(void))BS2_BASE_ADDR;

static char stringBuffer[0x80];

void *gc_text_tex_data_ptr;
extern void render_logo();

int main() {
    // enable printf
    CON_EnableGecko(1, true);

    u32 size = IPL_SIZE - BS2_CODE_OFFSET;
    u8 *bs2 = (u8*)(BS2_BASE_ADDR);

    __SYS_ReadROM(bs2, size, BS2_CODE_OFFSET);

    Elf32_Ehdr* ehdr;
    Elf32_Shdr* shdr;
    unsigned char* image;

    void * addr = (void*)patches_elf;
    ehdr = (Elf32_Ehdr *)addr;

    // get section string table
    Elf32_Shdr* shstr = &((Elf32_Shdr *)(addr + ehdr->e_shoff))[ehdr->e_shstrndx];
    char* stringdata = (char*)(addr + shstr->sh_offset);

    // get symbol string table
    Elf32_Shdr* symshdr = SHN_UNDEF;
    char* symstringdata = SHN_UNDEF;
    for (int i = 0; i < ehdr->e_shnum; ++i) {
        shdr = (Elf32_Shdr *)(addr + ehdr->e_shoff + (i * sizeof(Elf32_Shdr)));
        if (shdr->sh_type == SHT_SYMTAB) {
            symshdr = shdr;
        }
        if (shdr->sh_type == SHT_STRTAB && strcmp(stringdata + shdr->sh_name, ".strtab") == 0) {
            symstringdata = (char*)(addr + shdr->sh_offset);
        }
    }
    
    // get symbols
    Elf32_Sym* syment = (Elf32_Sym*) (addr + symshdr->sh_offset);

    // Patch each appropriate section
    for (int i = 0; i < ehdr->e_shnum; ++i) {
        shdr = (Elf32_Shdr *)(addr + ehdr->e_shoff + (i * sizeof(Elf32_Shdr)));

        if (!(shdr->sh_flags & SHF_ALLOC) || shdr->sh_addr == 0 || shdr->sh_size == 0) {
            continue;
        }

        shdr->sh_addr &= 0x3FFFFFFF;
        shdr->sh_addr |= 0x80000000;
        // shdr->sh_size &= 0xfffffffc;

        if (shdr->sh_type == SHT_NOBITS) {
            memset((void*)shdr->sh_addr, 0, shdr->sh_size);
        } else {
            // check if this is a patch section
            uint32_t sh_size = 0;
            char *sh_name = stringdata + shdr->sh_name;
            char *prefix = ".patch.";
            uint32_t prefix_len = strlen(prefix);
            uint32_t sh_name_len = strlen(sh_name);
            if (strncmp(prefix, sh_name, prefix_len) == 0) {
                // create symbol name for section size
                (sh_name + prefix_len)[sh_name_len - prefix_len - 5] = '\x00';
                sprintf(&stringBuffer[0], "%s_size", sh_name + prefix_len);

                // find symbol by name
                for (int i = 0; i < (symshdr->sh_size / sizeof(Elf32_Sym)); ++i) {
                    if (syment[i].st_name == SHN_UNDEF) {
                        continue;
                    }

                    char *symname = symstringdata + syment[i].st_name;
                    if (strcmp(symname, stringBuffer) == 0) {
                        sh_size = syment[i].st_value;
                    }
                }
            }

            // set section size from header if it is not provided as a symbol
            if (sh_size == 0) sh_size = shdr->sh_size;

            image = (unsigned char*)addr + shdr->sh_offset;
            memcpy((void*)shdr->sh_addr, (const void*)image, sh_size);

            printf("patching ptr=%x size=%04x orig=%08x val=%08x [%s]\n", shdr->sh_addr, sh_size, *(u32*)shdr->sh_addr, *(u32*)image, sh_name);
        }
    }

    /*** Shutdown libOGC ***/
    GX_AbortFrame();
    ASND_End();
    u32 bi2Addr = *(volatile u32*)0x800000F4;
    u32 osctxphys = *(volatile u32*)0x800000C0;
    u32 osctxvirt = *(volatile u32*)0x800000D4;
    SYS_ResetSystem(SYS_SHUTDOWN, 0, 0);
    *(volatile u32*)0x800000F4 = bi2Addr;
    *(volatile u32*)0x800000C0 = osctxphys;
    *(volatile u32*)0x800000D4 = osctxvirt;
    /*** Shutdown all threads and exit to this method ***/
    __lwp_thread_stopmultitasking(bs2entry);

    __builtin_unreachable();
}
