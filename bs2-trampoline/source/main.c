#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <ogcsys.h>
#include <gccore.h>
#include <unistd.h>

#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>

typedef unsigned int u32;
extern volatile u32 EXI[3][5];

#define EXI_READ_WRITE				2			/*!< EXI transfer type read-write */
#define EXI_CHANNEL_1				1			/*!< EXI channel 1 (memory card slot B) */
#define EXI_DEVICE_0				0			/*!< EXI device 0 */
#define EXI_SPEED_32MHZ				5			/*!< EXI device frequency 32MHz */

static void exi_select(void) {
	EXI[EXI_CHANNEL_1][0] = (EXI[EXI_CHANNEL_1][0] & 0x405) | ((1 << EXI_DEVICE_0) << 7) | (EXI_SPEED_32MHZ << 4);
}

static void exi_deselect(void) {
	EXI[EXI_CHANNEL_1][0] &= 0x405;
}

static uint32_t exi_imm_read_write(uint32_t data, uint32_t len) {
	EXI[EXI_CHANNEL_1][4] = data;
	EXI[EXI_CHANNEL_1][3] = ((len - 1) << 4) | (EXI_READ_WRITE << 2) | 0b01;
	while (EXI[EXI_CHANNEL_1][3] & 0b01);
	return EXI[EXI_CHANNEL_1][4] >> ((4 - len) * 8);
}

static bool usb_transmit_byte(const uint8_t *data) {
	uint16_t val;

	exi_select();
	val = exi_imm_read_write(0xB << 28 | *data << 20, 2);
	exi_deselect();

	return !(val & 0x400);
}

static bool usb_transmit_check(void) {
	uint8_t val;

	exi_select();
	val = exi_imm_read_write(0xC << 28, 1);
	exi_deselect();

	return !(val & 0x4);
}

static int usb_transmit(const void *data, int size, int minsize) {
	int i = 0, j = 0, check = 1;

	while (i < size) {
		if ((check && usb_transmit_check()) ||
			(check = usb_transmit_byte(data + i))) {
			j = i % 128;
			if (i < minsize)
				continue;
			else break;
		}

		i++;
		check = i % 128 == j;
	}

	return i;
}

#include "patches_elf.h"
#include "elf_abi.h"

#define ppc_nop 0x60000000
#define ppc_blr 0x4e800020

#define IPL_SIZE 0x200000
#define BS2_START_OFFSET 0x800
#define BS2_CODE_OFFSET (BS2_START_OFFSET + 0x20)
#define BS2_BASE_ADDR 0x81300000

extern void __exi_init(void);
extern void __SYS_ReadROM(void *buf,u32 len,u32 offset);

static void (*bs2entry)(void) = (void(*)(void))BS2_BASE_ADDR;

static char geckoBuffer[0x100];
static char stringBuffer[0x80];

void start() {
    __exi_init();

	u32 size = IPL_SIZE - BS2_CODE_OFFSET;
    u8 *bs2 = (u8*)(BS2_BASE_ADDR);

	__SYS_ReadROM(bs2, size, BS2_CODE_OFFSET);

    const char* str = "bopD\n";
    usb_transmit(str, strlen(str), strlen(str));

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

            sprintf(geckoBuffer, "patching ptr=%x size=%04x orig=%08x val=%08x\n", shdr->sh_addr, sh_size, *(u32*)shdr->sh_addr, *(u32*)image);
            usb_transmit(geckoBuffer, strlen(geckoBuffer), strlen(geckoBuffer));    
		}
    }

	bs2entry();

    __builtin_unreachable();
}

// unused
int main() {
	return 0;
}
