#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <malloc.h>

#include <ogcsys.h>
#include <gccore.h>
#include <unistd.h>

#include <asndlib.h>
#include <ogc/lwp_threads.h>

#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>

#include <sdcard/gcsd.h>
#include "ffshim.h"
#include "fatfs/ff.h"
#include "utils.h"
#include "crc32.h"
#include "dol.h"

#include "patches_elf.h"
#include "elf_abi.h"

#define VIDEO_ENABLE
// #define CONSOLE_ENABLE
#define PRINT_PATCHES

#define IPL_ROM_FONT_SJIS	0x1AFF00
#define DECRYPT_START		0x100

#define IPL_SIZE 0x200000
#define BS2_START_OFFSET 0x800
#define BS2_CODE_OFFSET (BS2_START_OFFSET + 0x20)
#define BS2_BASE_ADDR 0x81300000

#define PROG_ADDR 0x80100000

static u8 *prog_buf = (u8*)(PROG_ADDR);

void dol_alloc(int size);
u8 *dol_buf = NULL;

static DOLHEADER *dolhdr;
static u32 minaddress = 0;
static u32 maxaddress = 0;
static u32 _entrypoint, _dst, _src, _len;

extern void __exi_init(void);
extern void __SYS_ReadROM(void *buf,u32 len,u32 offset);

static void (*bs2entry)(void) = (void(*)(void))BS2_BASE_ADDR;
// static void (*stubentry)(void) = (void(*)(void))PROG_ADDR;

static char stringBuffer[0x80];
static u8 bios_buffer[IPL_SIZE];

void *gc_text_tex_data_ptr;
extern void render_logo();

void set_patch_value(Elf32_Shdr* symshdr, Elf32_Sym* syment, char* symstringdata, char* sym_name, u32 value);
int load_fat_ipl(const char *slot_name, const DISC_INTERFACE *iface_, char *path);
int load_fat_swiss(const char *slot_name, const DISC_INTERFACE *iface_);
void Descrambler(unsigned char* data, unsigned int size);

char *bios_path = "/ipl.bin";
char *swiss_paths[] = {
    "/AUTOEXEC.DOL",
    "/BOOT.DOL",
    "/BOOT2.DOL",
    "/IGR.DOL",
    "/IPL.DOL"
};

static u32 bs2_size = IPL_SIZE - BS2_CODE_OFFSET;
static u8 *bs2 = (u8*)(BS2_BASE_ADDR);

u32 bios_crc[] = {
    0xa8325e47, // gc-ntsc-10
    0x1082fbc9, // gc-pal-12
    0xbf225e4d, // gc-ntsc-12
    0x05196b74, // gc-pal-11
    0xf1ebeb95, // gc-ntsc-11
    0x5c3445d0, // gc-pal-10
};

int main() {
    SYS_SetArenaHi((void*)0x81300000);
    // SYS_SetArenaHi((void*)0x81700000);

#ifdef VIDEO_ENABLE
	VIDEO_Init();
	GXRModeObj *rmode = VIDEO_GetPreferredMode(NULL);
	void *xfb = MEM_K0_TO_K1(SYS_AllocateFramebuffer(rmode));
#ifdef CONSOLE_ENABLE
	console_init(xfb,20,20,rmode->fbWidth,rmode->xfbHeight,rmode->fbWidth*VI_DISPLAY_PIX_SZ);
#endif
	VIDEO_Configure(rmode);
	VIDEO_SetNextFramebuffer(xfb);
    VIDEO_ClearFrameBuffer(rmode, xfb, COLOR_BLACK);
	VIDEO_SetBlack(FALSE);
	VIDEO_Flush();
	VIDEO_WaitVSync();
	if(rmode->viTVMode&VI_NON_INTERLACE) VIDEO_WaitVSync();
    // // debug above
#endif

    // enable printf
    CON_EnableGecko(1, true);

    __SYS_ReadROM(bs2, bs2_size, BS2_CODE_OFFSET);
    u32 crc = csp_crc32_memory(bs2, bs2_size);
    printf("Read BS2 crc=%08x\n", crc);

    bool valid = FALSE;
    for(int i = 0; i < sizeof(bios_crc) / sizeof(bios_crc[0]); i++) {
        if(bios_crc[i] == crc) {
            valid = TRUE;
            break;
        }
    }

    printf("BS2 is valid? = %d\n", valid);
    
    // // TEST ONLY
    // valid = 0;

    if (!valid) {
        if (load_fat_ipl("sdb", &__io_gcsdb, bios_path)) goto found;
        if (load_fat_ipl("sda", &__io_gcsda, bios_path)) goto found;
        if (load_fat_ipl("sd2", &__io_gcsd2, bios_path)) goto found;

        printf("Failed to find %s\n", bios_path);
    }

found:
    printf("IPL loaded...\n");

    if (load_fat_swiss("sdb", &__io_gcsdb)) goto load;
    if (load_fat_swiss("sda", &__io_gcsda)) goto load;
    if (load_fat_swiss("sd2", &__io_gcsd2)) goto load;

load:
    printf("Program loaded... [%08x]\n", *(u32*)prog_buf);

    printf("maxaddress = 0x%08x\n", maxaddress);
    printf("minaddress = 0x%08x\n", minaddress);

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

        char *prefix = ".patch.";
        uint32_t prefix_len = strlen(prefix);

        if (!(shdr->sh_flags & SHF_ALLOC) || shdr->sh_addr == 0 || shdr->sh_size == 0) {
            printf("Skipping ALLOC %s!!\n", stringdata + shdr->sh_name);
            continue;
        }

        shdr->sh_addr &= 0x3FFFFFFF;
        shdr->sh_addr |= 0x80000000;
        // shdr->sh_size &= 0xfffffffc;

        if (shdr->sh_type == SHT_NOBITS && strncmp(prefix, stringdata + shdr->sh_name, prefix_len) != 0) {
            printf("Skipping NOBITS %s!!\n", stringdata + shdr->sh_name);
            memset((void*)shdr->sh_addr, 0, shdr->sh_size);
        } else {
            // check if this is a patch section
            uint32_t sh_size = 0;
            char *sh_name = stringdata + shdr->sh_name;
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
#ifdef PRINT_PATCHES
            printf("patching ptr=%x size=%04x orig=%08x val=%08x [%s]\n", shdr->sh_addr, sh_size, *(u32*)shdr->sh_addr, *(u32*)image, sh_name);
#endif
            memcpy((void*)shdr->sh_addr, (const void*)image, sh_size);
        }
    }

    // Copy program metadata into place
    set_patch_value(symshdr, syment, symstringdata, "prog_entrypoint", _entrypoint);
    set_patch_value(symshdr, syment, symstringdata, "prog_dst", _dst);
    set_patch_value(symshdr, syment, symstringdata, "prog_src", _src);
    set_patch_value(symshdr, syment, symstringdata, "prog_len", _len);

    // while(1);

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
        printf("Found %s = %08x\n", sym_name, ptr);

        *(u32*)ptr = value; 
    } else {
        printf("Could not find symbol %s\n", sym_name);
    }
}

int load_fat_ipl(const char *slot_name, const DISC_INTERFACE *iface_, char *path) {
    int res = 0;

    printf("Trying %s\n", slot_name);

    FATFS fs;
    iface = iface_;
    FRESULT mount_result = f_mount(&fs, "", 1);
    if (mount_result != FR_OK) {
        printf("Couldn't mount %s: %s\n", slot_name, get_fresult_message(mount_result));
        goto end;
    }

    char name[256];
    f_getlabel(slot_name, name, NULL);
    printf("Mounted %s as %s\n", name, slot_name);

    printf("Reading %s\n", path);
    FIL file;
    FRESULT open_result = f_open(&file, path, FA_READ);
    if (open_result != FR_OK) {
        printf("Failed to open file: %s\n", get_fresult_message(open_result));
        goto unmount;
    }

    size_t size = f_size(&file);
    if (size != IPL_SIZE) {
        printf("File %s is the wrong size %x\n", path, size);
        goto unmount;
    }
    u32 unused;
    f_read(&file, bios_buffer, size, &unused);
    f_close(&file);

    Descrambler(bios_buffer + DECRYPT_START, IPL_ROM_FONT_SJIS - DECRYPT_START);
    memcpy(bs2, bios_buffer + BS2_CODE_OFFSET, bs2_size);

    u32 crc = csp_crc32_memory(bs2, bs2_size);
    printf("New BS2 crc=%08x\n", crc);

    res = 1;

unmount:
    printf("Unmounting %s\n", slot_name);
    iface->shutdown();
    iface = NULL;

end:
    return res;
}

int load_fat_swiss(const char *slot_name, const DISC_INTERFACE *iface_) {
    int res = 0;

    printf("Trying %s\n", slot_name);

    FATFS fs;
    iface = iface_;
    FRESULT mount_result = f_mount(&fs, "", 1);
    if (mount_result != FR_OK) {
        printf("Couldn't mount %s: %s\n", slot_name, get_fresult_message(mount_result));
        goto end;
    }

    char name[256];
    f_getlabel(slot_name, name, NULL);
    printf("Mounted %s as %s\n", name, slot_name);

    for (int f = 0; f < (sizeof(swiss_paths) / sizeof(char *)); f++) {
        char *path = swiss_paths[f];

        printf("Reading %s\n", path);
        FIL file;
        FRESULT open_result = f_open(&file, path, FA_READ);
        if (open_result != FR_OK)
        {
            printf("Failed to open file: %s\n", get_fresult_message(open_result));
            continue;
        }

        size_t size = f_size(&file);
        dol_alloc(size);
        if (!dol_buf) {
            printf("Failed to allocate memory\n");
            goto unmount;
        }

        u32 unused;
        f_read(&file, dol_buf, size, &unused);
        f_close(&file);

        printf("Loaded DOL into %p\n", dol_buf);

        DOLtoMRAM(dol_buf);

        res = 1;
        break;
    }

unmount:
    printf("Unmounting %s\n", slot_name);
    iface->shutdown();
    iface = NULL;

end:
    return res;
}

void dol_alloc(int size) {
    int mram_size = (SYS_GetArenaHi() - SYS_GetArenaLo());
    printf("Memory available: %iB\n", mram_size);

    printf("DOL size is %iB\n", size);

    if (size <= 0) {
        printf("Empty DOL\n");
        return;
    }

    dol_buf = (u8*)memalign(32, size);

    if (!dol_buf) {
        printf("Couldn't allocate memory\n");
    }
}

// bootrom descrambler reversed by segher
// Copyright 2008 Segher Boessenkool <segher@kernel.crashing.org>
void Descrambler(unsigned char* data, unsigned int size) {
	unsigned char acc = 0;
	unsigned char nacc = 0;

	unsigned short t = 0x2953;
	unsigned short u = 0xd9c2;
	unsigned short v = 0x3ff1;

	unsigned char x = 1;
	unsigned int it;
	for (it = 0; it < size; )
	{
		int t0 = t & 1;
		int t1 = (t >> 1) & 1;
		int u0 = u & 1;
		int u1 = (u >> 1) & 1;
		int v0 = v & 1;

		x ^= t1 ^ v0;
		x ^= (u0 | u1);
		x ^= (t0 ^ u1 ^ v0) & (t0 ^ u0);

		if (t0 == u0)
		{
			v >>= 1;
			if (v0)
				v ^= 0xb3d0;
		}

		if (t0 == 0)
		{
			u >>= 1;
			if (u0)
				u ^= 0xfb10;
		}

		t >>= 1;
		if (t0)
			t ^= 0xa740;

		nacc++;
		acc = 2*acc + x;
		if (nacc == 8)
		{
			data[it++] ^= acc;
			nacc = 0;
		}
	}
}

/*--- DOL Decoding functions -----------------------------------------------*/
/****************************************************************************
* DOLMinMax
*
* Calculate the DOL minimum and maximum memory addresses
****************************************************************************/
static void DOLMinMax(DOLHEADER * dol)
{
    int i;

    maxaddress = 0;
    minaddress = 0x87100000;

    /*** Go through DOL sections ***/
    /*** Text sections ***/
    for (i = 0; i < MAXTEXTSECTION; i++)
    {
        if (dol->textAddress[i] && dol->textLength[i])
        {
            if (dol->textAddress[i] < minaddress)
                minaddress = dol->textAddress[i];
            if ((dol->textAddress[i] + dol->textLength[i]) > maxaddress) 
                maxaddress = dol->textAddress[i] + dol->textLength[i];
        }
    }

    /*** Data sections ***/
    for (i = 0; i < MAXDATASECTION; i++)
    {
        if (dol->dataAddress[i] && dol->dataLength[i])
        {
            if (dol->dataAddress[i] < minaddress)
                minaddress = dol->dataAddress[i];
            if ((dol->dataAddress[i] + dol->dataLength[i]) > maxaddress)
                maxaddress = dol->dataAddress[i] + dol->dataLength[i];
        }
    }

    /*** And of course, any BSS section ***/
    if (dol->bssAddress)
    {
        if ((dol->bssAddress + dol->bssLength) > maxaddress)
            maxaddress = dol->bssAddress + dol->bssLength;
    }

    /*** Some OLD dols, Xrick in particular, require ~128k clear memory ***/
    maxaddress += 0x20000;
}

u32 DOLSize(DOLHEADER *dol)
{
    u32 sizeinbytes;
    int i;

    sizeinbytes = DOLHDRLENGTH;

    /*** Go through DOL sections ***/
    /*** Text sections ***/
    for (i = 0; i < MAXTEXTSECTION; i++)
    {
        if (dol->textOffset[i])
        {
            if ((dol->textOffset[i] + dol->textLength[i]) > sizeinbytes)
                sizeinbytes = dol->textOffset[i] + dol->textLength[i];
        }
    }

    /*** Data sections ***/
    for (i = 0; i < MAXDATASECTION; i++)
    {
        if (dol->dataOffset[i])
        {
            if ((dol->dataOffset[i] + dol->dataLength[i]) > sizeinbytes)
                sizeinbytes = dol->dataOffset[i] + dol->dataLength[i];
        }
    }

    /*** Return DOL size ***/
    return sizeinbytes;
}

/****************************************************************************
* DOLtoMRAM
*
* Moves the DOL from main memory to MRAM
*
* Pass in a memory pointer to a previously loaded DOL
****************************************************************************/
int DOLtoMRAM(unsigned char *dol) {
    u32 sizeinbytes;
    int i;

    /*** Get DOL header ***/
    dolhdr = (DOLHEADER *) dol;

    /*** First, does this look like a DOL? ***/
    if (dolhdr->textOffset[0] != DOLHDRLENGTH && dolhdr->textOffset[0] != 0x0620)	// DOLX style 
        return 0;

    /*** Get DOL stats ***/
    DOLMinMax(dolhdr);
    sizeinbytes = maxaddress - minaddress;

    /*** Move all DOL sections into ARAM ***/
    /*** Move text sections ***/
    for (i = 0; i < MAXTEXTSECTION; i++) {
        /*** This may seem strange, but in developing d0lLZ we found some with section addresses with zero length ***/
        if (dolhdr->textAddress[i] && dolhdr->textLength[i]) {
            memcpy((char *) ((dolhdr->textAddress[i] - minaddress) + PROG_ADDR), dol + dolhdr->textOffset[i], dolhdr->textLength[i]);
        }
    }

    /*** Move data sections ***/
    for (i = 0; i < MAXDATASECTION; i++) {
        if (dolhdr->dataAddress[i] && dolhdr->dataLength[i]) {
            memcpy((char *) ((dolhdr->dataAddress[i] - minaddress) + PROG_ADDR), dol + dolhdr->dataOffset[i], dolhdr->dataLength[i]);
        }
    }

    printf("LOADCMD entry=%08x, minaddr=%08x, size=%08x\n", dolhdr->entryPoint, minaddress, sizeinbytes);

	_entrypoint = dolhdr->entryPoint;
	_dst = minaddress;
	_src = PROG_ADDR;
	_len = sizeinbytes;

    return 0;
}